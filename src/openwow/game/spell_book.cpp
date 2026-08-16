
#include "openwow/game/spell_book.h"
#include "openwow/game/character_map_runtime.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"
#include "openwow/game/actions/application/action_assignment_runtime.h"

#include "openwow/core/client_misc.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/ui/game/api/game_lua_api_action.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/streaming_init.h"
#include "openwow/game/spell_visual_pipeline.h"
#include "openwow/game/spellbook_catalog.h"
#include "openwow/game/spellbook_frame.h"
#include "openwow/game/spellbook_system.h"
#include "openwow/game/object_manager.h"
#include "openwow/net/wotlk/spell_packets.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/game/script_event_dispatch.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>

namespace openwow::game {

namespace {

using MultiCastSlotSpellLists = std::array<std::vector<std::uint32_t>, 4>;
constexpr std::uint32_t kDestLocSpellCastCacheLifetimeMs = 10000;

constexpr std::uint32_t kSpellAttr0RequiresAmmo = 0x00000002u;
constexpr std::uint32_t kSpellAttr0Ability = 0x00000010u;

constexpr std::uint32_t kSpellAttr0DisabledWhileActive = 0x02000000u;

constexpr std::uint32_t kSpellAttr2NoRangedAttackTimeCooldown = 0x00020000u;

constexpr std::uint32_t kSpellAttr6NoCategoryCooldownMods = 0x80000000u;

constexpr std::uint32_t kSpellCategoryFlagScalesWithWeaponSpeed = 0x00000001u;

constexpr std::uint32_t kStandardStartRecoveryCategory = 133u;
constexpr std::uint32_t kStandardStartRecoveryTimeMs = 1500u;
constexpr std::uint32_t kMinHastedGlobalCooldownMs = 1000u;
constexpr std::uint32_t kSpellDmgClassRanged = 2u;
constexpr std::uint32_t kSpellDmgClassMelee = 3u;

constexpr std::uint32_t kCooldownOnHoldBit = 0x80000000u;

[[nodiscard]] bool IsHastedStandardGlobalCooldown(
    const data::dbc::SpellEntry& spell) {
  return spell.start_recovery_category == kStandardStartRecoveryCategory &&
         spell.start_recovery_time == kStandardStartRecoveryTimeMs &&
         spell.dmg_class != kSpellDmgClassRanged &&
         spell.dmg_class != kSpellDmgClassMelee &&
         (spell.attributes &
          (kSpellAttr0RequiresAmmo | kSpellAttr0Ability)) == 0u;
}

[[nodiscard]] std::int32_t HasteGlobalCooldownMs(const std::int32_t duration_ms,
                                                 const float haste) {
  return static_cast<std::int32_t>(std::nearbyint(
      static_cast<double>(duration_ms) * static_cast<double>(haste) - 0.5));
}

[[nodiscard]] std::int32_t ClampGlobalCooldownMs(const std::int32_t duration_ms) {
  if (duration_ms > static_cast<std::int32_t>(kStandardStartRecoveryTimeMs)) {
    return static_cast<std::int32_t>(kStandardStartRecoveryTimeMs);
  }
  if (duration_ms < static_cast<std::int32_t>(kMinHastedGlobalCooldownMs)) {
    return static_cast<std::int32_t>(kMinHastedGlobalCooldownMs);
  }
  return duration_ms;
}

std::uint32_t GetMultiCastSlotMaskForSpell(const data::dbc::DbcLoader* dbc_loader,
                                           const std::uint32_t spell_id) {
  if (dbc_loader == nullptr || spell_id == 0) {
    return 0;
  }

  const auto* spell = dbc_loader->spell().LookupEntry(spell_id);
  if (spell == nullptr) {
    return 0;
  }

  std::uint32_t slot_mask = 0;
  for (const auto category : spell->totem_category) {
    slot_mask |= SpellBookFrame::MultiCastTotemCategoryToSlotMask(category);
  }

  return slot_mask;
}

bool ReadDestLocSpellCastRecord(PacketReader& reader, DestLocSpellCast& out) {
  std::uint64_t raw_guid = 0;
  if (!reader.ReadU64(raw_guid)) return false;
  out.record_guid = ObjectGuid(raw_guid);
  if (!reader.ReadU32(out.payload_word_2)) return false;
  if (!reader.ReadU32(out.payload_word_3)) return false;
  if (!reader.ReadU32(out.spell_id)) return false;
  if (!reader.ReadFloat(out.source_x)) return false;
  if (!reader.ReadFloat(out.source_y)) return false;
  if (!reader.ReadFloat(out.source_z)) return false;
  if (!reader.ReadFloat(out.destination_x)) return false;
  if (!reader.ReadFloat(out.destination_y)) return false;
  if (!reader.ReadFloat(out.destination_z)) return false;
  if (!reader.ReadFloat(out.trajectory_pitch)) return false;
  if (!reader.ReadFloat(out.trajectory_speed)) return false;
  if (!reader.ReadU32(out.duration_ms)) return false;
  if (!reader.ReadU8(out.progression_rank)) return false;
  if (!reader.ReadU8(out.missile_cast_count)) return false;
  return reader.ReadBytes(out.trailing_bytes.data(), out.trailing_bytes.size());
}

bool HasExpiredTick(const std::uint32_t now_tick,
                    const std::uint32_t expiry_tick) {
  return static_cast<std::int32_t>(now_tick - expiry_tick) >= 0;
}

MultiCastSlotSpellLists CaptureMultiCastSlotSpellLists(
    const data::dbc::DbcLoader* dbc_loader,
    const std::uint32_t slot_mask) {
  MultiCastSlotSpellLists spell_lists;
  if (dbc_loader == nullptr || slot_mask == 0) {
    return spell_lists;
  }

  for (std::size_t slot_index = 0; slot_index < spell_lists.size(); ++slot_index) {
    if ((slot_mask & (1U << slot_index)) == 0) {
      continue;
    }
    spell_lists[slot_index] =
        SpellBookFrame::GetMultiCastTotemSpells(
            static_cast<std::uint8_t>(slot_index), dbc_loader);
  }

  return spell_lists;
}

bool DidMultiCastSlotSpellListsChange(const MultiCastSlotSpellLists& before,
                                      const MultiCastSlotSpellLists& after,
                                      const std::uint32_t slot_mask) {
  for (std::size_t slot_index = 0; slot_index < before.size(); ++slot_index) {
    if ((slot_mask & (1U << slot_index)) == 0) {
      continue;
    }
    if (before[slot_index] != after[slot_index]) {
      return true;
    }
  }

  return false;
}

template <typename LearnSpellFn>
void ApplyLearnedMultiCastSpellUpdates(SpellBook& book,
                                       const std::uint32_t spell_id,
                                       LearnSpellFn&& learn_spell) {
  const auto* dbc_loader = book.dbc_loader();
  const auto slot_mask = GetMultiCastSlotMaskForSpell(dbc_loader, spell_id);
  const auto before = CaptureMultiCastSlotSpellLists(dbc_loader, slot_mask);

  learn_spell();

  if (slot_mask == 0) {
    return;
  }

  const auto after = CaptureMultiCastSlotSpellLists(dbc_loader, slot_mask);
  if (DidMultiCastSlotSpellListsChange(before, after, slot_mask)) {
    book.NotifyMultiCastSlotMaskChanged(slot_mask);
  }
}

std::string FormatPetSpellDisplayName(std::string_view name,
                                      std::string_view rank) {
  if (!rank.empty()) {
    std::string result;
    result.reserve(name.size() + rank.size() + 3);
    result.append(name);
    result.append(" (");
    result.append(rank);
    result.push_back(')');
    return result;
  }
  return std::string(name);
}

constexpr int kErrSpellUnlearnedS  = 352;

constexpr int kErrPetLearnSpell   = 639;
constexpr int kErrPetLearnAbility = 640;
constexpr int kErrPetSpellUnlearned = 641;

constexpr std::uint32_t kSpellAttr0TradeSpell       = 0x20;

constexpr std::array<std::size_t, 12> kSpellVisualStreamingKitOffsets = {
    8u, 16u, 24u, 88u, 92u, 100u, 12u, 56u, 60u, 96u, 4u, 20u,
};

void PreloadLearnedSpellStreamingVisuals(
    const data::dbc::DbcLoader& dbc_loader,
    const std::uint32_t spell_id) {
  if (!data::IsStreamingInitialized() || openwow::core::LoadingScreen_HasRenderLayer()) {
    return;
  }

  const auto* spell = dbc_loader.spell().LookupEntry(spell_id);
  if (spell == nullptr) {
    return;
  }

  const auto& visuals = dbc_loader.spell_visual();
  const auto& kits = dbc_loader.spell_visual_kit();
  const auto& effects = dbc_loader.spell_visual_effect_name();

  static_assert(sizeof(data::dbc::SpellVisualEntry) == 32u * sizeof(std::uint32_t));

  for (const std::uint32_t visual_id : spell->spell_visual) {
    if (visual_id == 0) {
      continue;
    }

    const auto* visual = visuals.LookupEntry(visual_id);
    if (visual == nullptr) {
      continue;
    }

    if (visual->has_missile != 0 && visual->missile_model > 0) {
      const auto* missile_effect =
          effects.LookupEntry(static_cast<std::uint32_t>(visual->missile_model));
      if (missile_effect != nullptr) {
        (void)RequestSpellVisualEffectModelPreload(*missile_effect);
      }
    }

    std::array<std::uint32_t, 32> raw_fields{};
    std::memcpy(raw_fields.data(), visual, sizeof(*visual));
    for (const std::size_t byte_offset : kSpellVisualStreamingKitOffsets) {
      const std::uint32_t kit_id = raw_fields[byte_offset / sizeof(std::uint32_t)];
      if (kit_id == 0) {
        continue;
      }

      const auto* kit = kits.LookupEntry(kit_id);
      if (kit == nullptr) {
        continue;
      }

      const std::array<std::uint32_t, 12> effect_ids = {
          kit->head_effect,        kit->chest_effect,
          kit->base_effect,        kit->left_hand_effect,
          kit->right_hand_effect,  kit->breath_effect,
          kit->left_weapon_effect, kit->right_weapon_effect,
          kit->special1_effect,    kit->special2_effect,
          kit->special3_effect,    kit->world_effect,
      };
      for (const std::uint32_t effect_id : effect_ids) {
        if (effect_id == 0) {
          continue;
        }
        const auto* effect = effects.LookupEntry(effect_id);
        if (effect != nullptr) {
          (void)RequestSpellVisualEffectModelPreload(*effect);
        }
      }
    }
  }
}

}

SpellBook::SpellBook(
    const openwow::data::dbc::DbcLoader& dbc_loader,
    CharacterMapRuntime& map_runtime,
    SpellCastRuntime& spell_cast_runtime,
    ActionAssignmentRuntime& action_assignments,
    SpellBookEffects effects)
    : dbc_loader_(dbc_loader),
      map_runtime_(map_runtime),
      spell_cast_runtime_(spell_cast_runtime),
      action_assignments_(action_assignments),
      effects_(std::move(effects)) {}

void SpellBook::NotifyMultiCastSlotMaskChanged(
    const std::uint32_t slot_mask) const {
  if (effects_.multi_cast_slot_mask_changed) {
    effects_.multi_cast_slot_mask_changed(slot_mask);
  }
}

bool SpellBook::HandleInitialSpells(const std::uint8_t* data, std::size_t len) {
  PacketReader reader(data, len);
  std::vector<PendingInitialSpell> initial_spells;
  std::unordered_set<std::uint32_t> parsed_spells;
  std::vector<SpellCooldown> parsed_cooldowns;

  std::uint8_t talent_spec = 0;
  (void)reader.ReadU8(talent_spec);

  std::uint16_t spell_count = 0;
  (void)reader.ReadU16(spell_count);

  initial_spells.reserve(spell_count);
  parsed_spells.reserve(spell_count);
  bool truncated = false;
  for (std::uint16_t i = 0; i < spell_count; ++i) {
    std::uint32_t spell_id = 0;
    std::uint16_t slot_id = 0;
    if (!reader.ReadU32(spell_id) || !reader.ReadU16(slot_id)) {
      truncated = true;
      break;
    }
    parsed_spells.insert(spell_id);
    initial_spells.push_back(PendingInitialSpell{spell_id, slot_id});
  }

  std::uint16_t cooldown_count = 0;
  if (!truncated && !reader.ReadU16(cooldown_count)) {
    truncated = true;
  }

  for (std::uint16_t i = 0; !truncated && i < cooldown_count; ++i) {
    SpellCooldown cd;
    if (!reader.ReadU32(cd.spell_id) || !reader.ReadU16(cd.item_id) ||
        !reader.ReadU16(cd.category) || !reader.ReadU32(cd.cooldown_ms) ||
        !reader.ReadU32(cd.category_cooldown_ms)) {
      truncated = true;
      break;
    }
    cd.start_time_s = core::GameClock::GetTickCountSeconds();
    parsed_cooldowns.push_back(cd);
  }

  spells_ = std::move(parsed_spells);

  for (const auto& parsed : parsed_cooldowns) {
    InsertInitialSpellCooldown(parsed);
  }
  pending_initial_spells_ = std::move(initial_spells);

  spellbook_reinitialization_pending_ = true;

  auto& system = SpellbookSystem::Get();
  for (const auto& parsed : parsed_cooldowns) {
    if (const auto merged = cooldowns_.find(parsed.spell_id);
        merged != cooldowns_.end()) {
      system.SetCooldown(merged->second.spell_id, merged->second.cooldown_ms,
                         merged->second.category_cooldown_ms);
    }
  }

  return true;
}

bool SpellBook::HandleLearnedSpell(const std::uint8_t* data, std::size_t len) {
  if (len < 6) return false;

  PacketReader reader(data, len);

  std::uint32_t spell_id;
  std::uint16_t unk;
  if (!reader.ReadU32(spell_id)) return false;
  if (!reader.ReadU16(unk)) return false;

  PreloadLearnedSpellStreamingVisuals(dbc_loader_, spell_id);

  spells_.insert(spell_id);
  ApplyLearnedMultiCastSpellUpdates(
      *this, spell_id, [this, spell_id, unk]() {
        if (effects_.spell_learned) {
          effects_.spell_learned(spell_id, unk != 0, 0);
        }
      });
  if (auto* active_player = map_runtime_.objects().GetActivePlayer();
      active_player != nullptr) {
    active_player->Casts().HandleSpellCast(
        *active_player, spell_cast_runtime_, spell_id, true, true);
    if (effects_.refresh_active_player_mutation_ui) {
      effects_.refresh_active_player_mutation_ui();
    }
  } else {
    pending_initial_spells_.push_back(PendingInitialSpell{spell_id, unk});
  }
  return true;
}

bool SpellBook::HandleRemovedSpell(const std::uint8_t* data, std::size_t len) {
  if (len < 4) return false;

  PacketReader reader(data, len);

  std::uint32_t spell_id;
  if (!reader.ReadU32(spell_id)) return false;

  spells_.erase(spell_id);
  cooldowns_.erase(spell_id);
  if (effects_.spell_forgotten) {
    effects_.spell_forgotten(spell_id);
  }
  SpellbookSystem::Get().ClearCooldown(spell_id);
  if (auto* held_cursor =
          effects_.held_cursor ? effects_.held_cursor() : nullptr;
      held_cursor != nullptr) {
    const auto* held_spell =
        held_cursor->get_if<actions::held_cursor::Spell>();
    if (held_spell != nullptr && held_spell->spell_id == spell_id) {
      held_cursor->Clear();
    }
  }
  const auto* active_player = map_runtime_.objects().GetActivePlayer();
  if (active_player != nullptr) {

    const auto* spell = dbc_loader_.spell().LookupEntry(spell_id);
    if (spell != nullptr &&
        (spell->attributes & kSpellAttr0TradeSpell) == 0 &&
        (spell->attributes & kSpellAttr0HiddenClientside) == 0) {
      const std::string display_name =
          FormatPetSpellDisplayName(spell->spell_name, spell->rank);
      ui::game::DisplaySystemMessage(kErrSpellUnlearnedS,
                                     display_name.c_str());
    }

    const auto changed_slots =
        action_assignments_.ClearSpellActionReferences(spell_id);
    auto& dispatch = ui::game::ScriptEventDispatch::Get();
    for (const auto slot_index : changed_slots) {
      dispatch.FireActionbarSlotChanged(
          static_cast<std::uint8_t>(slot_index + 1));
    }
    if (effects_.trainer_spellbook_changed) {
      effects_.trainer_spellbook_changed();
    }
  } else {
    pending_initial_spells_.erase(
        std::remove_if(pending_initial_spells_.begin(),
                       pending_initial_spells_.end(),
                       [spell_id](const PendingInitialSpell& entry) {
                         return entry.spell_id == spell_id;
                       }),
        pending_initial_spells_.end());
  }
  return true;
}

bool SpellBook::HandleSupercededSpell(const std::uint8_t* data, std::size_t len) {
  if (len < 8) return false;

  PacketReader reader(data, len);

  std::uint32_t old_spell_id, new_spell_id;
  if (!reader.ReadU32(old_spell_id)) return false;
  if (!reader.ReadU32(new_spell_id)) return false;

  spells_.erase(old_spell_id);
  cooldowns_.erase(old_spell_id);
  spells_.insert(new_spell_id);
  ApplyLearnedMultiCastSpellUpdates(
      *this, new_spell_id, [this, old_spell_id, new_spell_id]() {
    if (effects_.spell_learned) {
      effects_.spell_learned(new_spell_id, true, old_spell_id);
    }
  });
  SpellbookSystem::Get().ClearCooldown(old_spell_id);
  if (auto* active_player = map_runtime_.objects().GetActivePlayer();
      active_player != nullptr) {
    active_player->Casts().HandleSpellCast(
        *active_player, spell_cast_runtime_, new_spell_id, false, false);
    if (effects_.refresh_active_player_mutation_ui) {
      effects_.refresh_active_player_mutation_ui();
    }
  } else {
    auto pending_it = std::find_if(
        pending_initial_spells_.begin(), pending_initial_spells_.end(),
        [old_spell_id](const PendingInitialSpell& entry) {
          return entry.spell_id == old_spell_id;
        });
    if (pending_it != pending_initial_spells_.end()) {
      pending_it->spell_id = new_spell_id;
    } else {
      pending_initial_spells_.push_back(PendingInitialSpell{new_spell_id, 0});
    }
  }
  return true;
}

bool SpellBook::HandleSpellCooldown(const std::uint8_t* data, std::size_t len) {

  PacketReader reader(data, len);

  std::uint64_t unit_guid;
  std::uint8_t flags;
  if (!reader.ReadU64(unit_guid)) return false;
  if (!reader.ReadU8(flags)) return false;

  bool is_pet = false;
  {
    const ObjectGuid player_guid =
        map_runtime_.objects().GetLocalPlayerGuid();
    if (unit_guid == player_guid.GetRawValue()) {
      is_pet = false;
    } else {

      ObjectGuid pet_guid;
      if (const auto* player =
              map_runtime_.objects().GetPlayer(player_guid)) {
        pet_guid = player->State().GetPetGUID();
      }
      if (!pet_guid.IsEmpty() && unit_guid == pet_guid.GetRawValue()) {
        is_pet = true;
      } else {

        return true;
      }
    }
  }

  while (reader.Remaining() >= 8) {
    std::uint32_t spell_id;
    std::uint32_t cooldown_ms;
    if (!reader.ReadU32(spell_id)) break;
    if (!reader.ReadU32(cooldown_ms)) break;

    const double now_s = core::GameClock::GetTickCountSeconds();
    const auto* const spell = dbc_loader_.spell().LookupEntry(spell_id);

    auto duration_ms = static_cast<std::int32_t>(cooldown_ms);
    std::int32_t category_duration_ms = 0;
    if (cooldown_ms == 0u && spell != nullptr) {
      duration_ms = ApplySpellModifier(
          *spell, SpellModOp::kCooldown,
          static_cast<std::int32_t>(spell->recovery_time));

      category_duration_ms = ConditionCategoryCooldown(
          *spell, static_cast<std::int32_t>(spell->category_recovery_time),
          is_pet);
    }

    const bool on_hold =
        (flags & static_cast<std::uint8_t>(
                     SpellCooldownFlag::kIncludeEventCooldowns)) == 0u &&
        spell != nullptr &&
        (spell->attributes & kSpellAttr0DisabledWhileActive) != 0u;

    std::int32_t gcd_ms = 0;
    if (spell != nullptr && !on_hold &&
        (flags & static_cast<std::uint8_t>(SpellCooldownFlag::kIncludeGcd)) !=
            0u) {
      gcd_ms = ApplySpellModifier(
          *spell, SpellModOp::kGlobalCooldown,
          static_cast<std::int32_t>(spell->start_recovery_time));
    }

    if (is_pet) {
      PetCooldown node{};
      node.spell_id = spell_id;
      node.category = spell != nullptr
                          ? static_cast<std::uint16_t>(spell->category)
                          : std::uint16_t{0};
      node.cooldown_ms = static_cast<std::uint32_t>(std::max(duration_ms, 0));
      node.category_cooldown_ms =
          static_cast<std::uint32_t>(std::max(category_duration_ms, 0));
      if (gcd_ms > 0 && spell != nullptr) {
        node.gcd_category = spell->start_recovery_category;
        node.gcd_duration_ms = static_cast<std::uint32_t>(gcd_ms);
      }
      node.start_time_s = now_s;
      node.enabled = !on_hold;
      if (effects_.insert_pet_cooldown) {
        effects_.insert_pet_cooldown(node);
      }
      continue;
    }

    auto& entry = cooldowns_[spell_id];
    entry.spell_id = spell_id;
    entry.category = spell != nullptr
                         ? static_cast<std::uint16_t>(spell->category)
                         : std::uint16_t{0};
    entry.cooldown_ms = static_cast<std::uint32_t>(std::max(duration_ms, 0));
    entry.category_cooldown_ms =
        static_cast<std::uint32_t>(std::max(category_duration_ms, 0));
    entry.start_time_s = now_s;

    if (gcd_ms > 0 && spell != nullptr) {
      InsertGlobalCooldown(spell_id, spell->start_recovery_category,
                           static_cast<std::uint32_t>(gcd_ms), now_s);
    }

    if (on_hold) {
      entry.category_cooldown_ms |= kCooldownOnHoldBit;
    }

    if (entry.category != 0u && entry.category_cooldown_ms != 0u) {
      category_cooldowns_[entry.category] = entry;
    }

    SpellbookSystem::Get().SetCooldown(spell_id, entry.cooldown_ms, 0);
  }

  auto& dispatch = ui::game::ScriptEventDispatch::Get();
  dispatch.FireActionbarSpellAndShapeshiftCooldownUpdates(
      false);
  if (is_pet) {
    dispatch.FirePetBarUpdateCooldown();
  } else {
    dispatch.FireSpellUpdateCooldown();
  }

  return true;
}

std::int32_t SpellBook::ApplySpellModifier(const data::dbc::SpellEntry& spell,
                                           const SpellModOp op,
                                           const std::int32_t value) const {
  if (!effects_.apply_spell_modifier) {
    return value;
  }
  return effects_.apply_spell_modifier(spell, op, value);
}

void SpellBook::InsertInitialSpellCooldown(const SpellCooldown& parsed) {

  constexpr std::uint32_t kCooldownDurationMask = 0x7FFFFFFFu;
  const auto expires_at = [](const SpellCooldown& window) {
    const auto duration_ms =
        std::max(window.cooldown_ms,
                 window.category_cooldown_ms & kCooldownDurationMask);
    return window.start_time_s + static_cast<double>(duration_ms) / 1000.0;
  };

  if (const auto existing = cooldowns_.find(parsed.spell_id);
      existing == cooldowns_.end() ||
      expires_at(existing->second) <= expires_at(parsed)) {
    cooldowns_[parsed.spell_id] = parsed;
  }

  if (parsed.category == 0u ||
      (parsed.category_cooldown_ms & kCooldownDurationMask) == 0u) {
    return;
  }
  const SpellCooldown category_window{
      .spell_id = parsed.spell_id,
      .category = parsed.category,
      .cooldown_ms = parsed.category_cooldown_ms & kCooldownDurationMask,
      .category_cooldown_ms = parsed.category_cooldown_ms,
      .start_time_s = parsed.start_time_s,
  };
  if (const auto existing = category_cooldowns_.find(parsed.category);
      existing == category_cooldowns_.end() ||
      expires_at(existing->second) <= expires_at(category_window)) {
    category_cooldowns_[parsed.category] = category_window;
  }
}

void SpellBook::InsertGlobalCooldown(const std::uint32_t spell_id,
                                     const std::uint32_t category,
                                     const std::uint32_t duration_ms,
                                     const double start_time_s) {
  const SpellCooldown window{
      .spell_id = spell_id,
      .category = static_cast<std::uint16_t>(category),
      .cooldown_ms = duration_ms,
      .start_time_s = start_time_s,
  };

  const auto existing = global_cooldowns_.find(category);
  if (existing != global_cooldowns_.end() &&
      existing->second.start_time_s +
              static_cast<double>(existing->second.cooldown_ms) / 1000.0 >
          start_time_s + static_cast<double>(duration_ms) / 1000.0) {
    return;
  }
  global_cooldowns_[category] = window;
}

void SpellBook::StartGlobalCooldown(const std::uint32_t spell_id) {

  const auto* const spell = dbc_loader_.spell().LookupEntry(spell_id);
  if (spell == nullptr) return;

  auto duration_ms =
      ApplySpellModifier(*spell, SpellModOp::kGlobalCooldown,
                         static_cast<std::int32_t>(spell->start_recovery_time));

  if (IsHastedStandardGlobalCooldown(*spell)) {
    float haste = 1.0f;
    if (const auto* const caster = map_runtime_.objects().GetActivePlayer();
        caster != nullptr) {
      haste = caster->State().GetSpellHaste();
    }
    duration_ms = ClampGlobalCooldownMs(HasteGlobalCooldownMs(duration_ms, haste));
  }

  if (duration_ms <= 0) return;

  InsertGlobalCooldown(spell_id, spell->start_recovery_category,
                       static_cast<std::uint32_t>(duration_ms),
                       core::GameClock::GetTickCountSeconds());
  ui::game::ScriptEventDispatch::Get()
      .FireActionbarSpellAndShapeshiftCooldownUpdates(false);
}

float SpellBook::PetSpellHaste(const ObjectGuid& pet_guid) const {

  const auto* const pet = map_runtime_.objects().GetUnit(pet_guid);
  return pet != nullptr ? pet->State().GetSpellHaste() : 1.0f;
}

void SpellBook::StartPetGlobalCooldown(const std::uint32_t spell_id,
                                       const ObjectGuid& pet_guid) {
  if (!effects_.insert_pet_cooldown) return;
  const auto* const spell = dbc_loader_.spell().LookupEntry(spell_id);
  if (spell == nullptr) return;

  if (spell->start_recovery_category == 0u && spell->start_recovery_time == 0u) {
    return;
  }
  auto duration_ms =
      ApplySpellModifier(*spell, SpellModOp::kGlobalCooldown,
                         static_cast<std::int32_t>(spell->start_recovery_time));
  if (IsHastedStandardGlobalCooldown(*spell)) {
    duration_ms = ClampGlobalCooldownMs(
        HasteGlobalCooldownMs(duration_ms, PetSpellHaste(pet_guid)));
  }
  if (duration_ms <= 0) return;

  PetCooldown node{};
  node.spell_id = spell_id;
  node.gcd_category = spell->start_recovery_category;
  node.gcd_duration_ms = static_cast<std::uint32_t>(duration_ms);
  node.start_time_s = core::GameClock::GetTickCountSeconds();
  node.enabled = (spell->attributes & kSpellAttr0DisabledWhileActive) == 0u;
  effects_.insert_pet_cooldown(node);

  auto& dispatch = ui::game::ScriptEventDispatch::Get();
  dispatch.FireActionbarSpellAndShapeshiftCooldownUpdates(false);
  dispatch.FirePetBarUpdateCooldown();
}

void SpellBook::RecordPetSpellGoCooldown(const std::uint32_t spell_id,
                                         const ObjectGuid& pet_guid,
                                         const std::uint32_t cast_flags) {
  if (!effects_.insert_pet_cooldown) return;
  const auto* const spell = dbc_loader_.spell().LookupEntry(spell_id);
  if (spell == nullptr) return;

  PetCooldown node{};
  node.spell_id = spell_id;

  node.category = static_cast<std::uint16_t>(spell->category);
  node.start_time_s = core::GameClock::GetTickCountSeconds();
  node.enabled = (spell->attributes & kSpellAttr0DisabledWhileActive) == 0u;

  const auto cooldown_ms = ApplySpellModifier(
      *spell, SpellModOp::kCooldown,
      static_cast<std::int32_t>(spell->recovery_time));
  node.cooldown_ms = static_cast<std::uint32_t>(std::max(cooldown_ms, 0));

  const auto category_cooldown_ms = ConditionCategoryCooldown(
      *spell, static_cast<std::int32_t>(spell->category_recovery_time),
      true);
  node.category_cooldown_ms =
      static_cast<std::uint32_t>(std::max(category_cooldown_ms, 0));

  const bool suppress_gcd =
      (cast_flags &
       static_cast<std::uint32_t>(
           net::wotlk::SpellCastFlags::kNoPetGlobalCooldown)) != 0u;
  if (!suppress_gcd &&
      (spell->start_recovery_category != 0u || spell->start_recovery_time != 0u)) {
    auto gcd_ms = static_cast<std::int32_t>(spell->start_recovery_time);

    if (IsHastedStandardGlobalCooldown(*spell)) {
      gcd_ms = HasteGlobalCooldownMs(gcd_ms, PetSpellHaste(pet_guid));
    }
    gcd_ms = ClampGlobalCooldownMs(
        ApplySpellModifier(*spell, SpellModOp::kGlobalCooldown, gcd_ms));
    node.gcd_category = spell->start_recovery_category;
    node.gcd_duration_ms = static_cast<std::uint32_t>(std::max(gcd_ms, 0));
  }

  effects_.insert_pet_cooldown(node);

  auto& dispatch = ui::game::ScriptEventDispatch::Get();
  dispatch.FireActionbarSpellAndShapeshiftCooldownUpdates(false);
  dispatch.FirePetBarUpdateCooldown();
}

std::int32_t SpellBook::ConditionCategoryCooldown(
    const data::dbc::SpellEntry& spell, std::int32_t duration_ms,
    const bool pet_list) const {

  if (!pet_list) {

    const auto* const category =
        dbc_loader_.spell_category().LookupEntry(spell.category);
    if (category != nullptr &&
        (category->flags & kSpellCategoryFlagScalesWithWeaponSpeed) != 0u &&
        effects_.main_hand_weapon_delay_ms) {
      if (const auto delay_ms = effects_.main_hand_weapon_delay_ms();
          delay_ms != 0u) {
        duration_ms = static_cast<std::int32_t>(
            static_cast<std::int64_t>(duration_ms) * delay_ms / 1000);
      }
    }

    if ((spell.attributes & kSpellAttr0RequiresAmmo) != 0u &&
        (spell.attributes_ex2 & kSpellAttr2NoRangedAttackTimeCooldown) == 0u) {
      if (const auto* const player = map_runtime_.objects().GetActivePlayer();
          player != nullptr) {
        duration_ms += static_cast<std::int32_t>(
            player->State().GetRangedAttackTime());
      }
    }
  }

  if ((spell.attributes_ex6 & kSpellAttr6NoCategoryCooldownMods) == 0u) {
    duration_ms = ApplySpellModifier(spell, SpellModOp::kCooldown, duration_ms);
  }
  return duration_ms;
}

void SpellBook::CancelGlobalCooldown(const std::uint32_t spell_id) {

  bool changed = false;
  for (auto it = global_cooldowns_.begin(); it != global_cooldowns_.end();) {
    if (it->second.spell_id == spell_id) {
      it = global_cooldowns_.erase(it);
      changed = true;
    } else {
      ++it;
    }
  }
  if (!changed) return;
  ui::game::ScriptEventDispatch::Get()
      .FireActionbarSpellAndShapeshiftCooldownUpdates(false);
}

void SpellBook::RecordSuccessfulCastRecovery(
    const std::uint32_t spell_id) {
  const auto* const spell = dbc_loader_.spell().LookupEntry(spell_id);
  if (spell == nullptr) return;

  const double now_s = core::GameClock::GetTickCountSeconds();
  if (spell->recovery_time != 0u || spell->category_recovery_time != 0u) {
    cooldowns_[spell_id] = SpellCooldown{
        .spell_id = spell_id,
        .category = static_cast<std::uint16_t>(spell->category),
        .cooldown_ms = spell->recovery_time,
        .category_cooldown_ms = spell->category_recovery_time,
        .start_time_s = now_s,
    };
  }

  if (spell->category != 0u && spell->category_recovery_time != 0u) {
    category_cooldowns_[spell->category] = SpellCooldown{
        .spell_id = spell_id,
        .category = static_cast<std::uint16_t>(spell->category),
        .cooldown_ms = spell->category_recovery_time,
        .start_time_s = now_s,
    };
  }

  ui::game::ScriptEventDispatch::Get()
      .FireActionbarSpellAndShapeshiftCooldownUpdates(false);
}

void SpellBook::ClearCooldown(const std::uint32_t spell_id) {
  cooldowns_.erase(spell_id);
  if (const auto* const spell = dbc_loader_.spell().LookupEntry(spell_id);
      spell != nullptr && spell->category != 0u) {
    category_cooldowns_.erase(spell->category);
  }
  CancelGlobalCooldown(spell_id);
  SpellbookSystem::Get().ClearCooldown(spell_id);
  ui::game::ScriptEventDispatch::Get()
      .FireActionbarSpellAndShapeshiftCooldownUpdates(false);
}

void SpellBook::ModifyCooldown(const std::uint32_t spell_id,
                               const std::int32_t delta_ms) {
  const auto it = cooldowns_.find(spell_id);
  if (it == cooldowns_.end()) return;
  const double delta_s = static_cast<double>(delta_ms) / 1000.0;
  it->second.start_time_s += delta_s;
  if (it->second.category != 0u) {
    if (const auto category =
            category_cooldowns_.find(it->second.category);
        category != category_cooldowns_.end() &&
        category->second.spell_id == spell_id) {
      category->second.start_time_s += delta_s;
    }
  }
  for (auto& [category, window] : global_cooldowns_) {
    if (window.spell_id == spell_id) {
      window.start_time_s += delta_s;
    }
  }
  ui::game::ScriptEventDispatch::Get()
      .FireActionbarSpellAndShapeshiftCooldownUpdates(false);
}

net::wotlk::WorldPacket SpellBook::BuildCastSpell(
    std::uint32_t spell_id,
    const ObjectGuid& target) {
  SpellPacketTargets targets;
  if (target.IsEmpty()) {
    targets.target_mask = SpellTargetFlag::kNone;
  } else {
    targets.target_mask = SpellTargetFlag::kUnit;
    targets.unit_target = target;
  }
  return BuildCastSpellFull(spell_id, 0, targets);
}

net::wotlk::WorldPacket SpellBook::BuildCastSpellFull(
    std::uint32_t spell_id,
    std::uint8_t cast_count,
    const SpellPacketTargets& targets) {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_CAST_SPELL);

  pkt.AppendU8(cast_count);
  pkt.AppendU32(spell_id);
  pkt.AppendU8(0);

  WriteTargets(pkt, targets);

  return pkt;
}

void SpellBook::WriteTargets(net::wotlk::WorldPacket& pkt,
                              const SpellPacketTargets& targets) {
  pkt.AppendU32(static_cast<std::uint32_t>(targets.target_mask));

  auto needs_object = SpellTargetFlag::kUnit | SpellTargetFlag::kUnitMinipet |
                      SpellTargetFlag::kGameobject | SpellTargetFlag::kCorpseEnemy |
                      SpellTargetFlag::kCorpseAlly;
  if (HasFlag(targets.target_mask, needs_object)) {
    auto packed = targets.unit_target.Pack();
    pkt.AppendBytes(packed.data(), packed.size());
  }

  auto needs_item = SpellTargetFlag::kItem | SpellTargetFlag::kTradeItem;
  if (HasFlag(targets.target_mask, needs_item)) {
    auto packed = targets.item_target.Pack();
    pkt.AppendBytes(packed.data(), packed.size());
  }

  if (HasFlag(targets.target_mask, SpellTargetFlag::kSourceLocation)) {
    auto packed = targets.src_transport.Pack();
    pkt.AppendBytes(packed.data(), packed.size());
    pkt.AppendFloat(targets.src_x);
    pkt.AppendFloat(targets.src_y);
    pkt.AppendFloat(targets.src_z);
  }

  if (HasFlag(targets.target_mask, SpellTargetFlag::kDestLocation)) {
    auto packed = targets.dst_transport.Pack();
    pkt.AppendBytes(packed.data(), packed.size());
    pkt.AppendFloat(targets.dst_x);
    pkt.AppendFloat(targets.dst_y);
    pkt.AppendFloat(targets.dst_z);
  }

  if (HasFlag(targets.target_mask, SpellTargetFlag::kString)) {
    std::array<std::uint8_t, net::wotlk::kSpellTargetStringBlockBytes> block{};
    const std::size_t length =
        std::min(targets.string_target.size(), block.size() - 1u);
    std::memcpy(block.data(), targets.string_target.data(), length);
    pkt.AppendBytes(block.data(), block.size());
  }
}

bool SpellBook::ReadTargets(PacketReader& reader, SpellPacketTargets& out) {
  std::uint32_t mask_raw;
  if (!reader.ReadU32(mask_raw)) return false;
  out.target_mask = static_cast<SpellTargetFlag>(mask_raw);

  auto needs_object = SpellTargetFlag::kUnit | SpellTargetFlag::kUnitMinipet |
                      SpellTargetFlag::kGameobject | SpellTargetFlag::kCorpseEnemy |
                      SpellTargetFlag::kCorpseAlly;
  if (HasFlag(out.target_mask, needs_object)) {
    if (!reader.ReadPackedGuid(out.unit_target)) return false;
  }

  auto needs_item = SpellTargetFlag::kItem | SpellTargetFlag::kTradeItem;
  if (HasFlag(out.target_mask, needs_item)) {
    if (!reader.ReadPackedGuid(out.item_target)) return false;
  }

  if (HasFlag(out.target_mask, SpellTargetFlag::kSourceLocation)) {
    if (!reader.ReadPackedGuid(out.src_transport)) return false;
    if (!reader.ReadFloat(out.src_x)) return false;
    if (!reader.ReadFloat(out.src_y)) return false;
    if (!reader.ReadFloat(out.src_z)) return false;
  }

  if (HasFlag(out.target_mask, SpellTargetFlag::kDestLocation)) {
    if (!reader.ReadPackedGuid(out.dst_transport)) return false;
    if (!reader.ReadFloat(out.dst_x)) return false;
    if (!reader.ReadFloat(out.dst_y)) return false;
    if (!reader.ReadFloat(out.dst_z)) return false;
  }

  if (HasFlag(out.target_mask, SpellTargetFlag::kString)) {
    std::array<std::uint8_t, net::wotlk::kSpellTargetStringBlockBytes> block{};
    if (!reader.ReadBytes(block.data(), block.size())) return false;
    const auto* const nul = static_cast<const std::uint8_t*>(
        std::memchr(block.data(), 0, block.size()));
    out.string_target.assign(
        reinterpret_cast<const char*>(block.data()),
        nul != nullptr ? static_cast<std::size_t>(nul - block.data())
                       : block.size());
  }

  return true;
}

bool SpellBook::HasSpell(std::uint32_t spell_id) const {
  return spells_.count(spell_id) > 0;
}

bool SpellBook::IsOnCooldown(std::uint32_t spell_id) const {
  auto it = cooldowns_.find(spell_id);
  if (it == cooldowns_.end()) return false;
  return it->second.cooldown_ms > 0 || it->second.category_cooldown_ms > 0;
}

bool SpellBook::HandleTotemCreated(const std::uint8_t* data, std::size_t len,
                                   const std::uint32_t client_time_ms) {
  PacketReader reader(data, len);
  TotemCreated tc;
  if (!reader.ReadU8(tc.slot)) return false;
  if (!reader.ReadU64(tc.totem_guid)) return false;
  if (!reader.ReadU32(tc.duration_ms)) return false;
  if (!reader.ReadU32(tc.spell_id)) return false;

  if (tc.slot >= totem_slots_.size() || tc.totem_guid == 0) {
    return true;
  }

  last_totem_created_ = tc;
  auto& slot = totem_slots_[tc.slot];
  slot.totem_guid = tc.totem_guid;
  slot.spell_id = tc.spell_id;
  slot.start_time_ms = client_time_ms;
  slot.duration_ms = tc.duration_ms;
  return true;
}

std::optional<TotemSlotState> SpellBook::GetTotemSlot(
    const std::uint8_t slot) const {
  if (slot >= totem_slots_.size()) {
    return std::nullopt;
  }

  return totem_slots_[slot];
}

std::optional<std::uint8_t> SpellBook::FindTotemSlotByGuid(
    const std::uint64_t guid) const {
  if (guid == 0) {
    return std::nullopt;
  }

  for (std::uint8_t slot = 0; slot < totem_slots_.size(); ++slot) {
    if (totem_slots_[slot].totem_guid == guid) {
      return slot;
    }
  }

  return std::nullopt;
}

bool SpellBook::ClearTotemSlot(const std::uint8_t slot) {
  if (slot >= totem_slots_.size()) {
    return false;
  }

  auto& state = totem_slots_[slot];
  if (!state.has_totem()) {
    return false;
  }

  state.Reset();
  return true;
}

std::optional<std::uint8_t> SpellBook::ClearTotemByGuid(
    const std::uint64_t guid) {
  const auto slot = FindTotemSlotByGuid(guid);
  if (!slot.has_value()) {
    return std::nullopt;
  }

  totem_slots_[*slot].Reset();
  return slot;
}

bool SpellBook::HandleResumeCastBar(const std::uint8_t* data, std::size_t len) {
  PacketReader reader(data, len);
  ResumeCastBar rcb;
  if (!reader.ReadPackedGuid(rcb.caster)) return false;
  if (!reader.ReadPackedGuid(rcb.target)) return false;
  if (!reader.ReadU32(rcb.spell_id)) return false;
  if (!reader.ReadU32(rcb.time_remaining)) return false;
  if (!reader.ReadU32(rcb.cast_time)) return false;
  last_resume_cast_bar_ = rcb;
  return true;
}

bool SpellBook::HandleTalentWipeConfirm(const std::uint8_t* data,
                                        std::size_t len) {
  PacketReader reader(data, len);
  TalentWipeConfirm twc;
  if (!reader.ReadU64(twc.npc_guid)) return false;
  if (!reader.ReadU32(twc.cost)) return false;
  last_talent_wipe_confirm_ = twc;
  return true;
}

bool SpellBook::HandleSummonCancel() {
  summon_cancelled_ = true;
  return true;
}

bool SpellBook::HandleSpellUpdateChainTargets(const std::uint8_t* data,
                                              std::size_t len) {
  PacketReader reader(data, len);
  SpellChainTargets sct;
  if (!reader.ReadU64(sct.caster_guid)) return false;
  if (!reader.ReadU32(sct.spell_id)) return false;
  std::uint32_t target_count;
  if (!reader.ReadU32(target_count)) return false;
  sct.targets.reserve(target_count);
  for (std::uint32_t i = 0; i < target_count; ++i) {
    std::uint64_t tgt;
    if (!reader.ReadU64(tgt)) return false;
    sct.targets.push_back(tgt);
  }
  last_chain_targets_ = std::move(sct);
  return true;
}

void SpellBook::Update(const std::uint32_t client_time_ms) {
  for (auto it = dest_loc_spell_cast_cache_.begin();
       it != dest_loc_spell_cast_cache_.end();) {
    if (HasExpiredTick(client_time_ms, it->second.expires_at_ms)) {
      it = dest_loc_spell_cast_cache_.erase(it);
      continue;
    }
    ++it;
  }
}

bool SpellBook::HandleNotifyDestLocSpellCast(const std::uint8_t* data,
                                             std::size_t len,
                                             const std::uint32_t client_time_ms,
                                             std::optional<DestLocSpellCast>* dispatched_record) {
  PacketReader reader(data, len);
  DestLocSpellCast record;
  if (!ReadDestLocSpellCastRecord(reader, record)) return false;
  if (dispatched_record != nullptr) {
    dispatched_record->reset();
  }

  const auto effective_client_time_ms =
      client_time_ms != 0 ? client_time_ms
                          : openwow::core::GameClock::GetTickCount32();
  Update(effective_client_time_ms);

  if (!record.record_guid.IsEmpty()) {
    const auto raw_guid = record.record_guid.GetRawValue();
    const auto cache_it = dest_loc_spell_cast_cache_.find(raw_guid);
    if (cache_it != dest_loc_spell_cast_cache_.end() &&
        static_cast<std::int8_t>(static_cast<std::uint8_t>(
            record.progression_rank -
            cache_it->second.progression_rank)) < 1) {

      return true;
    }

    auto& cache_entry = dest_loc_spell_cast_cache_[raw_guid];
    cache_entry.progression_rank = record.progression_rank;
    cache_entry.expires_at_ms =
        effective_client_time_ms + kDestLocSpellCastCacheLifetimeMs;
  }

  last_dest_loc_cast_ = record;
  if (dispatched_record != nullptr) {
    *dispatched_record = record;
  }
  return true;
}

bool SpellBook::HandlePetLearnedSpell(const std::uint8_t* data,
                                      std::size_t len) {
  PacketReader reader(data, len);
  if (!reader.ReadU32(last_pet_learned_spell_)) return false;

  if (const auto* spell =
          dbc_loader_.spell().LookupEntry(last_pet_learned_spell_);
      spell != nullptr) {
    const std::string display_name =
        FormatPetSpellDisplayName(spell->spell_name, spell->rank);
    const int msg = (spell->attributes & 0x10) != 0
                        ? kErrPetLearnAbility
                        : kErrPetLearnSpell;
    ui::game::DisplaySystemMessage(msg, display_name.c_str());
  }
  return true;
}

bool SpellBook::HandlePetUnlearnedSpell(const std::uint8_t* data,
                                        std::size_t len) {
  PacketReader reader(data, len);
  if (!reader.ReadU32(last_pet_unlearned_spell_)) return false;

  if (const auto* spell =
          dbc_loader_.spell().LookupEntry(last_pet_unlearned_spell_);
      spell != nullptr) {
    const std::string display_name =
        FormatPetSpellDisplayName(spell->spell_name, spell->rank);
    ui::game::DisplaySystemMessage(kErrPetSpellUnlearned,
                                   display_name.c_str());
  }
  return true;
}

bool SpellBook::HasPendingInitialSpell(const std::uint32_t spell_id) const {
  return std::any_of(
      pending_initial_spells_.begin(), pending_initial_spells_.end(),
      [spell_id](const PendingInitialSpell& entry) {
        return entry.spell_id == spell_id;
      });
}

void SpellBook::ApplyPendingInitialSpellSideEffects() {
  if (map_runtime_.objects().GetActivePlayer() == nullptr) {
    return;
  }
  ApplyPendingSpellbookReinitialization();
}

void SpellBook::ApplyPendingSpellbookReinitialization() {
  if (!spellbook_reinitialization_pending_) {
    return;
  }
  spellbook_reinitialization_pending_ = false;

  auto& system = SpellbookSystem::Get();
  system.ResetSpellbookData();
  SpellBookFrame::ClearSpellGroups();

  for (const auto& initial_spell : pending_initial_spells_) {
    ApplyLearnedMultiCastSpellUpdates(
        *this,
        initial_spell.spell_id,
        [this, spell_id = initial_spell.spell_id]() {
          if (effects_.spell_learned) {
            effects_.spell_learned(spell_id, false, 0);
          }
        });
  }
  if (effects_.finalize_initial_companion_catalog) {
    effects_.finalize_initial_companion_catalog();
  }

  for (const auto& [spell_id, cooldown] : cooldowns_) {
    system.SetCooldown(spell_id, cooldown.cooldown_ms,
                       cooldown.category_cooldown_ms);
  }

  auto* active_player = map_runtime_.objects().GetActivePlayer();
  if (active_player == nullptr) {

    spellbook_reinitialization_pending_ = true;
    return;
  }

  for (const auto& entry : pending_initial_spells_) {
    active_player->Casts().HandleSpellCast(
        *active_player, spell_cast_runtime_, entry.spell_id, false, true);
  }

  pending_initial_spells_.clear();

  SpellbookSystem::Get().RefreshDisplayState(map_runtime_.objects());
  if (effects_.refresh_active_player_mutation_ui) {
    effects_.refresh_active_player_mutation_ui();
  }
}

void SpellBook::Clear() {
  spells_.clear();
  cooldowns_.clear();
  category_cooldowns_.clear();
  global_cooldowns_.clear();
  pending_initial_spells_.clear();
  spellbook_reinitialization_pending_ = false;
  if (auto* active_player = map_runtime_.objects().GetActivePlayer();
      active_player != nullptr) {
    active_player->Casts().ClearOffhandWeaponOverrideSpell();
  }
  SpellbookSystem::Get().Reset();

  last_totem_created_.reset();
  for (auto& slot : totem_slots_) {
    slot.Reset();
  }
  last_resume_cast_bar_.reset();
  last_talent_wipe_confirm_.reset();
  summon_cancelled_ = false;
  last_chain_targets_.reset();
  last_dest_loc_cast_.reset();
  dest_loc_spell_cast_cache_.clear();
  last_pet_learned_spell_ = 0;
  last_pet_unlearned_spell_ = 0;
}

}

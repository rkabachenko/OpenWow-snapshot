#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"

#include "openwow/game/spell_cost_and_range.h"
#include "openwow/game/spell_runtime_detail.h"
#include "openwow/game/spell_cast_diagnostics.h"

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/core/storm_intrusive_list.h"
#include "openwow/data/formats/dbc/dbc_entries_gameplay.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/data/formats/dbc/dbc_table_registry.h"
#include "openwow/game/active_player_environment.h"
#include "openwow/game/cooldown_tracker.h"
#include "openwow/game/inventory/search/action_item_inventory_search.h"
#include "openwow/game/group_system.h"
#include "openwow/game/localization.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/items/item_on_use_spell.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgcorpse.h"
#include "openwow/game/objects/cgitem.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/proc_manager.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/aura_application.h"
#include "openwow/game/profession_system.h"
#include "openwow/game/recent_cast_tracker.h"
#include "openwow/game/spell_action.h"
#include "openwow/game/spell_c_internals.h"
#include "openwow/game/spell_cast_lifecycle.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/spell_query_bridge.h"
#include "openwow/game/spell_usability.h"
#include "openwow/game/spellbook_system.h"
#include "openwow/game/spell_target_validation.h"
#include "openwow/game/update_fields.h"
#include "openwow/game/world_environment_state.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/script_event_dispatch.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <string>

namespace openwow::game {

std::uint32_t GetCastFailureMessageId(std::uint32_t error_code,
                                       std::uintptr_t spell_rec,
                                       std::int32_t extra_param) {
  constexpr std::uint32_t kDefault = 48;

  constexpr std::uint32_t kSpellAttrUsesAlternateFailureMessage = 0x10u;

  const auto* fields =
      reinterpret_cast<const std::uint32_t*>(spell_rec);

  auto category_sub_switch = [&]() -> std::uint32_t {
    if (!fields) return kDefault;
    switch (fields[1]) {
      case 4: case 9:   return 50;
      case 10: case 11: return 51;
      default:           return 49;
    }
  };

  switch (error_code) {
    case 2:   return 430;
    case 3:   return 431;
    case 4:   return 432;
    case 11:  return 199;
    case 12: {
      if (!fields) return kDefault;
      const std::uint32_t targets = fields[16];
      if (targets & 0x10)    return 335;
      if (targets & 0x20000) return 636;
      return kDefault;
    }
    case 28:  return 227;
    case 29:
    case 30:
    case 31:  return 228;
    case 45:  return category_sub_switch();
    case 52:  return 326;
    case 67: {
      std::uint32_t result = kDefault;
      if (fields) {
        switch (fields[1]) {
          case 4: case 9:   result = 50; break;
          case 10: case 11: result = 51; break;
          default:

            result = (fields[4] & kSpellAttrUsesAlternateFailureMessage) != 0u
                         ? 53u
                         : 52u;
            break;
        }
      }
      return result;
    }
    case 72:  return 339;
    case 94:  return 229;
    case 97:  return 324;
    case 100: return 225;
    case 129: return (extra_param > 0) ? 626 : kDefault;
    case 131: return 224;
    case 148: return 489;
    case 170: return 623;
    case 174: return 625;
    case 178: return 637;
    case 184: return 664;
    default:  return kDefault;
  }
}

void PlaySpellSchoolFizzleSound(const data::dbc::DbcLoader& dbc,
                                std::uint32_t spell_id,
                                const CGObject_C* unit) {
  if (unit == nullptr) {
    return;
  }

  const auto* spell_entry = dbc.spell().LookupEntry(spell_id);
  if (spell_entry == nullptr) {
    return;
  }

  const std::uint32_t school_mask = spell_entry->school_mask;

  constexpr std::uint32_t kMaxSchools = 7;
  std::uint32_t fizzle_sound_id = 0;

  for (std::uint32_t school = 0; school < kMaxSchools; ++school) {
    if ((school_mask & (1u << school)) == 0) {
      continue;
    }

    const auto* resistance = dbc.resistances().LookupEntry(school);
    const std::uint32_t sound_id =
        resistance != nullptr ? resistance->fizzle_sound_id : 0u;
    if (sound_id != 0) {
      fizzle_sound_id = sound_id;
      break;
    }
  }

  if (fizzle_sound_id == 0) {
    return;
  }

  const auto position = unit->GetPosition();
  const float pos[3] = {position.x, position.y, position.z};

  (void)unit->sound_runtime().PlaySoundKit(fizzle_sound_id, pos);
}

void HandleCastFailure(WorldSession& session,
                        std::uintptr_t spell_entry,
                        std::uintptr_t spell_rec,
                        std::uint32_t error_code,
                        std::int32_t extra1,
                        std::int32_t extra2,
                        bool is_auto_repeat) {

  (void)extra2;

  auto* player = session.objects().GetActivePlayer();
  if (player == nullptr) return;

  const auto* dbc = session.GetDbcLoader();
  const std::uint32_t now = core::GameClock::GetTickCount32();

  std::uint32_t spell_id = 0;
  if (spell_entry != 0) {
    const auto* entry = reinterpret_cast<const data::dbc::SpellEntry*>(spell_rec);
    if (entry != nullptr) spell_id = entry->id;
  } else if (spell_rec != 0) {
    const auto* entry = reinterpret_cast<const data::dbc::SpellEntry*>(spell_rec);
    spell_id = entry->id;
  }

  if (is_auto_repeat) {
    if (error_code == 52 || error_code == 53 || error_code == 54) {
      player->Animation().SetAutoRepeatActive(false);
    }
    return;
  }

  auto& runtime = SpellCastDiagnostics::Get();
  if (error_code == runtime.last_cast_failure_reason &&
      spell_id != 0 && spell_id == runtime.last_cast_spell_id &&
      now - runtime.previous_cast_time < 3000) {
    return;
  }
  runtime.last_cast_failure_reason = error_code;
  runtime.last_cast_spell_id = spell_id;
  runtime.previous_cast_time = now;

  if (error_code != 0) {
    const std::uint32_t fizzle_sound_id = 5400u;
    const Position pos = player->GetPosition();
    const float sound_pos[3] = {pos.x, pos.y, pos.z};
    auto& sound = session.sound_runtime();
    sound.PlaySoundKit(fizzle_sound_id, sound_pos);
  }

  std::string substitution;
  switch (error_code) {
    case 100:
    case 131: {
      if (extra1 > 0) {
        const auto item_id = static_cast<std::uint32_t>(extra1);
        auto item_name = session.item_definitions().GetItemNameSnapshot(item_id);
        if (!item_name.has_value()) {
          (void)session.query_cache().GetOrRequestItemTemplate(item_id);
        } else if (!item_name->empty()) {
          substitution = *item_name;
        }
      }
      break;
    }
    case 135: {
      if (dbc != nullptr) {
        const auto* ct_entry = dbc->creature_type().LookupEntry(
            static_cast<std::uint32_t>(extra1));
        if (ct_entry != nullptr) {
          substitution = ct_entry->name[0];
        }
      }
      break;
    }
    case 39: {
      if (dbc != nullptr) {
        const auto* area = dbc->area_table().LookupEntry(
            static_cast<std::uint32_t>(extra1));
        if (area != nullptr && !area->name.empty()) {
          substitution = area->name;
        }
      }
      break;
    }
    default:
      break;
  }

  SpellAction_DisplaySpellFailure(session, spell_id, player->GetGuid(),
                                  error_code, substitution);

  ui::game::ScriptEventDispatch::Get().FireUnitSpellcastFailed(
      player->GetGuid().GetRawValue());
}

int PetSpellCast(std::uint32_t action_slot,
                  std::uintptr_t action_data) {
  if (!ui::game::CVarSystem::Instance().GetCVarBool("secureAbilityToggle")) {
    return 0;
  }

  auto& tracker = (action_data == 0)
      ? RecentCastTracker::GetPlayerTracker()
      : RecentCastTracker::GetPetTracker();

  return tracker.Contains(action_slot, 0, core::GameClock::GetTickCount32())
             ? 1
             : 0;
}

int IsItemUseSpellRecentlyCast(const ItemDefinitions& item_definitions,
                               std::uint32_t item_entry_id) {
  const auto* item_template = item_definitions.GetItem(item_entry_id);
  if (item_template == nullptr) {
    return 0;
  }

  const auto slot_idx = FindFirstOnUseSpellIndex(*item_template);
  if (slot_idx < 0) {
    return 0;
  }

  const auto spell_id =
      item_template->spells[static_cast<std::size_t>(slot_idx)].spell_id;

  return RecentCastTracker::GetPlayerTracker().Contains(
             spell_id, item_entry_id,
             core::GameClock::GetTickCount32())
             ? 1
             : 0;
}

int HasActiveSpellCooldown(std::uintptr_t spell_rec) {
  if (!spell_rec) return 0;

  const auto* spell =
      reinterpret_cast<const data::dbc::SpellEntry*>(spell_rec);
  const auto now = core::GameClock::GetTickCount32();
  const auto& cooldowns = CooldownTracker::Get();

  if (cooldowns.GetSpellCooldownRemaining(spell->id, now) > 0.0f ||
      cooldowns.GetCategoryCooldownRemaining(spell->category, now) > 0.0f) {
    return 1;
  }

  return 0;
}

constexpr std::uint32_t kSpellAttr0ReqAmmo      = 0x2;
constexpr std::uint32_t kSpellAttr3MainHand      = 0x400;
constexpr std::uint32_t kSpellAttr3ReqOffhand    = 0x1000000;
constexpr std::uint32_t kTargetFlagSkipItemCheck = 0x10;

constexpr std::uint32_t kSlotBitMainHand = 1u << InventorySlots::kMainHand;
constexpr std::uint32_t kSlotBitOffHand  = 1u << InventorySlots::kOffHand;

constexpr std::uint32_t kSlotBitRanged   = 1u << InventorySlots::kRanged;

constexpr std::uint32_t kAllEquipSlots   = 0xFFFFFFFFu;

constexpr std::uint32_t kItemSubClassFlagSkipAmmoCheck = 0x10;

bool CanSatisfyEquippedItemRequirement(const CGItem_C& item) {
  const std::uint32_t item_flags = item.GetItemFlags();
  if ((item_flags & kItemFieldFlagBroken) != 0) {
    return false;
  }

  return (item_flags & kItemFieldFlagWrapped) != 0 ||
         item.GetUInt32(ITEM_FIELD_MAXDURABILITY) == 0 ||
         item.GetUInt32(ITEM_FIELD_DURABILITY) != 0;
}

const CGItem_C* FindEquippedItemForSpell(
    const WorldSession& session, const CGPlayer_C& player,
    const data::dbc::SpellEntry& spell,
    std::uint32_t slot_mask) {
  if (player.IsVisibleWeaponDisplaySuppressed(0)) {
    slot_mask &= ~kSlotBitMainHand;
  }
  if (player.IsVisibleWeaponDisplaySuppressed(1)) {
    slot_mask &= ~kSlotBitOffHand;
  }

  const auto& inventory = session.inventory_replica();
  const auto& obj_mgr = session.objects();

  constexpr std::uint8_t kMaxScanSlot = InventorySlots::kBagSlotsEnd;

  for (std::uint8_t slot = 0; slot < kMaxScanSlot; ++slot) {
    if (((1u << slot) & slot_mask) == 0) {
      continue;
    }

    const auto* slot_data = inventory.GetItemInSlot(slot);
    if (slot_data == nullptr || slot_data->guid == 0) {
      continue;
    }

    const auto* item = obj_mgr.GetItem(ObjectGuid(slot_data->guid));
    if (item == nullptr) {
      continue;
    }
    if (!CanSatisfyEquippedItemRequirement(*item)) {
      continue;
    }

    const auto item_class = item->GetItemClassFromClientDbc();
    const auto item_subclass = item->GetItemSubClassFromClientDbc();
    if (static_cast<std::int32_t>(item_class) != spell.equipped_item_class) {
      continue;
    }
    if (item_subclass >= 32 ||
        ((1u << item_subclass) &
         static_cast<std::uint32_t>(spell.equipped_item_sub_class_mask)) == 0) {
      continue;
    }

    return item;
  }

  return nullptr;
}

bool CheckEquippedItemAndAmmo(const WorldSession& session,
                              const CGPlayer_C& player,
                               const data::dbc::SpellEntry& spell,
                               bool check_ammo) {
  if (!player.IsActivePlayer()) {
    return true;
  }

  if ((spell.targets & kTargetFlagSkipItemCheck) != 0 ||
      spell.equipped_item_class < 0 ||
      spell.equipped_item_sub_class_mask == 0) {
    return true;
  }

  const std::uint32_t attr_ex3 = spell.attributes_ex3;
  std::uint32_t slot_mask;
  if ((attr_ex3 & kSpellAttr3MainHand) != 0) {
    slot_mask = kSlotBitMainHand;
  } else if ((attr_ex3 & kSpellAttr3ReqOffhand) != 0) {
    slot_mask = kSlotBitOffHand;
  } else if ((spell.attributes & kSpellAttr0ReqAmmo) != 0) {
    slot_mask = kSlotBitRanged;
  } else {
    slot_mask = kAllEquipSlots;
  }

  const auto* equipped_item =
      FindEquippedItemForSpell(session, player, spell, slot_mask);

  if (equipped_item != nullptr) {
    if ((attr_ex3 & (kSpellAttr3MainHand | kSpellAttr3ReqOffhand)) ==
            (kSpellAttr3MainHand | kSpellAttr3ReqOffhand) &&
        FindEquippedItemForSpell(
            session, player, spell, kSlotBitOffHand) == nullptr) {
      return false;
    }

    if (check_ammo) {
      const auto item_class = equipped_item->GetItemClassFromClientDbc();
      const auto item_subclass = equipped_item->GetItemSubClassFromClientDbc();
      const int num_entries = data::DBClient_GetItemSubClassCount();

      bool found_subclass_entry = false;
      for (int i = 0; i < num_entries; ++i) {
        const auto* entry = data::DBClient_GetItemSubClassEntryByIndex(i);
        if (entry == nullptr) {
          continue;
        }
        if (entry->class_id == item_class &&
            entry->subclass_id == item_subclass) {
          if ((entry->flags & kItemSubClassFlagSkipAmmoCheck) != 0) {
            return true;
          }
          found_subclass_entry = true;
          break;
        }
      }
      (void)found_subclass_entry;

      const auto* item_template =
          session.item_definitions().GetItem(equipped_item->GetEntry());
      if (item_template != nullptr && item_template->ammo_type != 0) {
        const std::uint32_t ammo_type = item_template->ammo_type;

        const std::uint32_t equipped_ammo_entry =
            player.GetUInt32(PLAYER_AMMO_ID);

        if (equipped_ammo_entry == 0) {
          return false;
        }

        const auto* found_projectile =
            FindFirstDefaultCarriedInventoryItem(
                session.inventory_replica(),
                [&](const ItemInstance& item) {
                  const auto* projectile =
                      session.item_definitions().GetItem(item.entry);
                  return projectile != nullptr &&
                         projectile->item_class == ItemClass::Projectile &&
                         projectile->subclass == ammo_type &&
                         (equipped_ammo_entry == 0 ||
                          item.entry == equipped_ammo_entry);
                });

        if (found_projectile == nullptr) {
          return false;
        }
      }
    }

    return true;
  }

  return false;
}

void SpellAuraList_AdjustDuration(std::uintptr_t ,
                                   std::uint32_t spell_id,
                                   std::int32_t delta_ms) {
  CooldownTracker::Get().AdjustSpellCooldown(spell_id, delta_ms);
}

void SpellAuraList_RecycleAll(std::uintptr_t aura_list) {
  if (aura_list == 0) return;

  SpellHistory_FreeOwnedNodes(aura_list);
}

bool GetTargetRangeWindow(const WorldSession& session,
                          const std::uint32_t spell_id,
                          const CGUnit_C& caster,
                          const CGObject_C* pending_target,
                          float* out_min,
                          float* out_max) {
  if (out_min == nullptr || out_max == nullptr) {
    return false;
  }
  return WriteTargetRangeWindow(
      session, spell_id, caster, pending_target, out_min, out_max);
}

}

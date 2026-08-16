#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"

#include "openwow/game/spell_cast_execution.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/spell_cost_and_range.h"
#include "openwow/game/spell_effective_variant.h"
#include "openwow/game/spell_runtime_detail.h"

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/core/storm_intrusive_list.h"
#include "openwow/data/formats/dbc/dbc_entries_extended.h"
#include "openwow/data/formats/dbc/dbc_entries_gameplay.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/data/formats/dbc/dbc_table_registry.h"
#include "openwow/game/active_player_environment.h"
#include "openwow/game/cooldown_tracker.h"
#include "openwow/game/flyable_area.h"
#include "openwow/game/inventory/search/action_item_inventory_search.h"
#include "openwow/game/group_system.h"
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
#include "openwow/game/reputation_info.h"
#include "openwow/game/spell_action.h"
#include "openwow/game/spell_book.h"
#include "openwow/game/spell_c_internals.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/game/spell_query_bridge.h"
#include "openwow/game/spell_usability.h"
#include "openwow/game/spellbook_system.h"
#include "openwow/game/spell_target_validation.h"
#include "openwow/game/spell_validation.h"
#include "openwow/game/time_of_day_windows.h"
#include "openwow/game/update_fields.h"
#include "openwow/game/world_environment_state.h"
#include "openwow/game/world_session.h"
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

int SendCastSpell(WorldSession& session, std::uintptr_t spell_entry,
                  bool skip_visual) {
  if (spell_entry == 0) return 0;

  const auto* spell =
      reinterpret_cast<const data::dbc::SpellEntry*>(spell_entry);
  if (spell == nullptr) return 0;

  auto* player = session.objects().GetActivePlayer();
  if (player == nullptr) return 0;
  const auto* dbc = session.GetDbcLoader();
  if (dbc == nullptr) return 0;

  if (!ValidateSpellRequirements(
          session,
          reinterpret_cast<std::uintptr_t>(player),
          spell_entry,
          0,
          0,
          false) ||
          !HasEnoughSpellPower(*spell, *player, session)) {
    return 0;
  }

  auto& client = session.spells();
  std::uint64_t target_guid = 0;

  const ObjectGuid target_guid_obj = session.objects().GetTargetGuid();
  if (!target_guid_obj.IsEmpty()) {
    target_guid = target_guid_obj.GetRawValue();
  }

  const auto result = client.CastSpell(session, spell->id, target_guid, 0);
  if (result != SpellCastResult::kSuccess) {
    return 0;
  }

  (void)skip_visual;

  return 1;
}

constexpr std::uint32_t kAuraTypeAllowOnlyAbility = 262u;
constexpr std::uint32_t kAuraTypeIgnoreShapeshift = 275u;

bool HasMatchingAuraEffectForSpell(
    const CGUnit_C& aura_source, const data::dbc::DbcLoader& dbc,
    const data::dbc::SpellEntry& spell, const std::uint32_t aura_type) {
  for (const auto& aura : aura_source.Auras().All()) {
    if (aura.spell_id == 0u) {
      continue;
    }

    const auto* const aura_spell = dbc.spell().LookupEntry(aura.spell_id);
    if (aura_spell == nullptr) {
      continue;
    }

    const std::uint8_t active_effect_mask = aura.ActiveEffectMask();
    for (std::uint32_t effect_index = 0u;
         effect_index < data::dbc::kMaxSpellEffects; ++effect_index) {
      if ((active_effect_mask & (1u << effect_index)) == 0u ||
          aura_spell->effect_apply_aura[effect_index] != aura_type) {
        continue;
      }
      if (Spell_MatchesEffectSpellClassMask(
              &spell, aura_spell, effect_index)) {
        return true;
      }
    }
  }

  return false;
}

bool HasMatchingAllowOnlyAbilityAuraForSpell(
    const CGUnit_C& unit, const data::dbc::SpellEntry& spell) {
  const auto* const objects = unit.object_manager();
  const auto* const dbc = unit.dbc_loader();
  if (objects == nullptr || dbc == nullptr) {
    return false;
  }

  const CGUnit_C* aura_source = &unit;
  for (ObjectGuid creator_guid = aura_source->State().GetCreatedBy();
       !creator_guid.IsEmpty();
       creator_guid = aura_source->State().GetCreatedBy()) {
    const auto* const creator = objects->GetUnit(creator_guid);
    if (creator == nullptr) {
      break;
    }
    aura_source = creator;
  }

  return HasMatchingAuraEffectForSpell(
      *aura_source, *dbc, spell, kAuraTypeAllowOnlyAbility);
}

bool HasMatchingIgnoreShapeshiftAuraForSpell(
    const CGUnit_C& unit, const data::dbc::SpellEntry& spell) {
  const auto* const dbc = unit.dbc_loader();
  return dbc != nullptr && HasMatchingAuraEffectForSpell(
                            unit, *dbc, spell, kAuraTypeIgnoreShapeshift);
}

bool HasComboPointsForSpellTarget(const WorldSession& session,
                                  const CGPlayer_C& player,
                                  const std::uintptr_t target_guid_ptr) {
  ObjectGuid target_guid{};
  if (target_guid_ptr != 0u) {
    const auto raw_guid = *reinterpret_cast<const std::uint64_t*>(
        target_guid_ptr);
    target_guid = ObjectGuid(raw_guid);
  }
  if (target_guid.IsEmpty()) {
    target_guid = session.objects().GetTargetGuid();
  }

  return player.GetComboPoints() != 0u &&
         player.Casts().GetComboTarget() == target_guid;
}

const CGUnit_C* ResolveSpellRequirementTarget(
    const WorldSession& session, const std::uintptr_t target_guid_ptr) {
  ObjectGuid target_guid{};
  if (target_guid_ptr != 0u) {
    const auto raw_guid = *reinterpret_cast<const std::uint64_t*>(
        target_guid_ptr);
    target_guid = ObjectGuid(raw_guid);
  }
  if (target_guid.IsEmpty()) {
    target_guid = session.objects().GetTargetGuid();
  }
  return target_guid.IsEmpty() ? nullptr : session.objects().GetUnit(target_guid);
}

constexpr std::uint32_t kMapTypeNormal       = 0;
constexpr std::uint32_t kMapTypeRaid         = 2;
constexpr std::uint32_t kMapTypeBattleground = 3;
constexpr std::uint32_t kMapTypeArena        = 4;

constexpr std::uint32_t kAttr0NextSwing               = 0x00000002u;
constexpr std::uint32_t kAttr0OnlyDaytime             = 0x00001000u;
constexpr std::uint32_t kAttr0OnlyNighttime           = 0x00002000u;
constexpr std::uint32_t kAttr0CastableWhileMounted     = 0x01000000u;
constexpr std::uint32_t kAttr0Tradespell               = 0x02000000u;
constexpr std::uint32_t kAttr0IndoorOnly               = 0x00004000u;
constexpr std::uint32_t kAttr0OutdoorOnly              = 0x00008000u;
constexpr std::uint32_t kAttr0IndoorOutdoorMask        = 0x0000C000u;

constexpr std::uint32_t kAttrEx1FinishingMove          = 0x00500000u;

constexpr std::uint32_t kAttrEx3BattlegroundOnly       = 0x00000800u;

constexpr std::uint32_t kAttrEx4NotInArenaOrRatedBg    = 0x00020000u;
constexpr std::uint32_t kAttrEx4UsableInArena          = 0x00010000u;
constexpr std::uint32_t kAttrEx4CastOnlyInFlyableArea = 0x04000000u;

constexpr std::uint32_t kAttrEx6InstanceOnly           = 0x00000002u;
constexpr std::uint32_t kAttrEx6NotInRaid              = 0x00000800u;

constexpr std::uint32_t kFormFlagBlockNextSwing   = 0x01000000u;
constexpr std::uint32_t kFormFlagBlockNonMelee    = 0x00800000u;

constexpr std::uint32_t kAuraIntFlagRequireUnderwater = 0x00000100u;
constexpr std::uint32_t kAuraIntFlagRequireAboveWater = 0x00000080u;
constexpr std::uint32_t kAuraIntFlagRequireMounted = 0x00000040u;
constexpr std::uint32_t kAuraIntFlagRequireUnsheathed = 0x00000200u;

constexpr std::int32_t kArenaMaxCooldownMs = 900000;

bool HasNoReagentCost(const CGPlayer_C& player,
                      const data::dbc::DbcLoader* dbc,
                      const data::dbc::SpellEntry& spell) {
  if (dbc == nullptr) {
    return false;
  }

  const auto* const player_class =
      dbc->chr_classes().LookupEntry(player.State().GetClass());
  if (player_class == nullptr ||
      spell.spell_family_name != player_class->spell_family) {
    return false;
  }

  for (std::size_t word_index = 0;
       word_index < spell.spell_family_flags.size(); ++word_index) {
    const auto no_reagent_cost_mask = player.GetUInt32(
        static_cast<std::uint16_t>(PLAYER_NO_REAGENT_COST_1 + word_index));
    if ((spell.spell_family_flags[word_index] & no_reagent_cost_mask) != 0u) {
      return true;
    }
  }
  return false;
}

bool HasRequiredTotemItem(const PlayerInventoryReplica& inventory,
                          const std::uint32_t required_item_id) {

  return !inventory.VisitDefaultPlayerItems(
      [required_item_id](const ItemInstance& item) {
        return item.entry != required_item_id;
      });
}

bool HasRequiredTotemCategory(const WorldSession& session,
                              const data::dbc::DbcLoader* dbc,
                              const std::uint32_t required_category_id) {
  if (dbc == nullptr || required_category_id == 0u) {
    return false;
  }

  const auto* const required_category =
      dbc->totem_category().LookupEntry(required_category_id);
  if (required_category == nullptr) {
    return false;
  }

  std::uint32_t remaining_mask = required_category->totem_category_mask;

  return !session.inventory_replica().VisitDefaultPlayerItems(
      [&](const ItemInstance& item) {
        const auto* const item_template =
            session.query_cache().GetItemTemplate(item.entry);
        if (item_template == nullptr || item_template->totem_category == 0u) {
          return true;
        }

        const auto* const item_category =
            dbc->totem_category().LookupEntry(item_template->totem_category);
        if (item_category == nullptr ||
            item_category->totem_category_type !=
                required_category->totem_category_type) {
          return true;
        }

        remaining_mask &= ~item_category->totem_category_mask;
        return remaining_mask != 0u;
      });
}

bool IsCurrentAreaInGroup(
    const data::dbc::DbcLoader& dbc, std::int32_t area_group_id,
    const std::int32_t zone_id, const std::int32_t area_id) {
  while (area_group_id > 0) {
    const auto* const group =
        dbc.area_group().LookupEntry(
            static_cast<std::uint32_t>(area_group_id));
    if (group == nullptr) {
      return false;
    }
    for (const auto allowed_area_id : group->area_id) {
      if (allowed_area_id != 0u &&
          (allowed_area_id == static_cast<std::uint32_t>(zone_id) ||
           allowed_area_id == static_cast<std::uint32_t>(area_id))) {
        return true;
      }
    }
    area_group_id = static_cast<std::int32_t>(group->next_group);
  }
  return false;
}

SpellRequirementValidation ValidateSpellRequirementsDetailed(
    const WorldSession& session, const std::uintptr_t caster_ptr,
    const std::uintptr_t spell_rec_ptr,
    const std::uintptr_t target_guid_ptr,
    const std::uintptr_t item_template_ptr, const bool force) {
  const auto failure = [](const SpellCastResult result,
                          const std::int32_t extra1 = -1,
                          const std::int32_t extra2 = -1) {
    return SpellRequirementValidation{result, extra1, extra2};
  };
  if (caster_ptr == 0) {
    return failure(SpellCastResult::kError);
  }
  if (spell_rec_ptr == 0) {
    return failure(SpellCastResult::kError);
  }

  auto* caster = reinterpret_cast<CGUnit_C*>(caster_ptr);
  const auto* spell =
      reinterpret_cast<const data::dbc::SpellEntry*>(spell_rec_ptr);
  const auto* item_template =
      reinterpret_cast<const ItemTemplate*>(item_template_ptr);

  if (spell->effect[0] == kSpellEffectTradeSkill) {
    return {};
  }

  const auto* dbc = session.GetDbcLoader();

  if (caster->IsActivePlayer()) {
    const bool is_next_swing_or_open_lock =
        (spell->effect[0] == kSpellEffectAttack) ||
        (spell->attributes & kAttr0NextSwing) != 0;

    const auto form_id = caster->Animation().GetShapeshiftForm();
    if (form_id != 0 && dbc != nullptr) {
      const auto* form_entry =
          dbc->spell_shapeshift_form().LookupEntry(form_id);
      if (form_entry != nullptr) {
        const std::uint32_t form_flags = form_entry->flags;
        if ((form_flags & kFormFlagBlockNextSwing) != 0) {
          if (is_next_swing_or_open_lock ||
              SpellRec_IsNextSwingOrInitiatesCombat(spell)) {
            return failure(SpellCastResult::kNoItemsWhileShapeshifted);
          }
        } else if (!is_next_swing_or_open_lock &&
                   (form_flags & kFormFlagBlockNonMelee) != 0) {
          return failure(SpellCastResult::kNotShapeshift);
        }
      }
    }
  }

  if (caster->Mount().IsMountedStateActive(*caster) &&
      (spell->attributes & kAttr0CastableWhileMounted) == 0) {
    return failure(SpellCastResult::kNotMounted);
  }

  const data::dbc::MapEntry* map_entry = nullptr;
  std::uint32_t map_type = kMapTypeNormal;

  if (dbc != nullptr) {
    const auto current_map_id = session.world_states().map_id();
    if (current_map_id >= 0) {
      map_entry = dbc->map().LookupEntry(
          static_cast<std::uint32_t>(current_map_id));
    }
    if (map_entry != nullptr) {
      map_type = map_entry->map_type;
    }
  }

  if (map_entry != nullptr) {
    if (map_type == kMapTypeArena) {
      const std::uint32_t attr4 = spell->attributes_ex4;
      if (!((attr4 & kAttrEx4NotInArenaOrRatedBg) != 0 ||
            (static_cast<std::int32_t>(spell->recovery_time) <= kArenaMaxCooldownMs &&
             static_cast<std::int32_t>(spell->category_recovery_time) <= kArenaMaxCooldownMs &&
             (attr4 & kAttrEx4UsableInArena) == 0))) {
        return failure(SpellCastResult::kNotInArena);
      }
    } else if ((spell->attributes_ex6 & kAttrEx6InstanceOnly) != 0) {
      return failure(SpellCastResult::kOnlyInArena);
    }
  }

  if (!force && dbc != nullptr) {
    const auto* const active_player = session.objects().GetActivePlayer();
    const ObjectGuid active_player_guid =
        active_player != nullptr ? active_player->GetGuid() : ObjectGuid{};
    std::uint32_t blocking_mechanic = 0;
    const auto control_result = ValidateSpellCasterControlState(
        *caster, *dbc, *spell, active_player_guid, &blocking_mechanic);
    if (control_result != SpellCastResult::kSuccess) {
      return failure(control_result,
                     blocking_mechanic != 0
                         ? static_cast<std::int32_t>(blocking_mechanic)
                         : -1);
    }
  }

  {
    const bool skip_totem_and_reagent_check =
        item_template != nullptr &&
        (item_template->flags & 0x10000000u) != 0;
    if (!skip_totem_and_reagent_check) {
      if (caster->IsActivePlayer()) {
        const auto& inventory = session.inventory_replica();
        for (std::uint32_t i = 0; i < data::dbc::kMaxSpellTotems; ++i) {
          if (spell->totem[i] != 0) {
            if (!HasRequiredTotemItem(inventory, spell->totem[i])) {
              return failure(SpellCastResult::kTotems,
                             static_cast<std::int32_t>(spell->totem[i]));
            }
          }
        }
        for (std::uint32_t i = 0; i < data::dbc::kMaxSpellTotems; ++i) {
          if (spell->totem_category[i] != 0u &&
              !HasRequiredTotemCategory(session, dbc, spell->totem_category[i])) {
            return failure(SpellCastResult::kTotemCategory,
                           static_cast<std::int32_t>(spell->totem_category[i]));
          }
        }
        const auto& player = static_cast<const CGPlayer_C&>(*caster);
        if (!HasNoReagentCost(player, dbc, *spell)) {
          for (std::uint32_t i = 0; i < data::dbc::kMaxSpellReagents; ++i) {
            if (spell->reagent[i] > 0) {
              if (inventory.GetItemCount(
                      static_cast<std::uint32_t>(spell->reagent[i])) <
                  spell->reagent_count[i]) {
                return failure(
                    SpellCastResult::kReagents,
                    spell->reagent[i], spell->reagent_count[i]);
              }
            }
          }
        }
      }
    }
  }

  if (caster->IsActivePlayer()) {
    const auto* player = reinterpret_cast<const CGPlayer_C*>(caster);
    if (!CheckEquippedItemAndAmmo(
            session, *player, *spell, true)) {
      return failure(SpellCastResult::kEquippedItem,
                     spell->equipped_item_class,
                     spell->equipped_item_sub_class_mask);
    }
  }

  if ((spell->attributes_ex & kAttrEx1FinishingMove) != 0) {
    if (caster->IsActivePlayer()) {
      const auto* player = reinterpret_cast<const CGPlayer_C*>(caster);
      if (!HasComboPointsForSpellTarget(session, *player, target_guid_ptr) &&
          !HasMatchingAllowOnlyAbilityAuraForSpell(*caster, *spell)) {
        return failure(SpellCastResult::kNoComboPoints);
      }
    }
  }

  {
    SpellUsabilityInfo usability_info{};
    usability_info.stances = spell->stances;
    usability_info.stances_high = spell->stances_high;
    usability_info.stances_not = spell->stances_not;
    usability_info.stances_not_high = spell->stances_not_high;
    usability_info.attributes = spell->attributes;
    usability_info.attributes_ex2 = spell->attributes_ex2;

    PlayerStateSnapshot snapshot{};

    snapshot.shapeshift_form =
        caster->State().SuppressesCurrentFormSpellQueries()
            ? 0u
            : caster->Animation().GetShapeshiftForm();
    if (snapshot.shapeshift_form != 0 && dbc != nullptr) {
      const auto* form_entry =
          dbc->spell_shapeshift_form().LookupEntry(snapshot.shapeshift_form);
      if (form_entry != nullptr) {
        snapshot.shapeshift_form_flags = form_entry->flags;
        snapshot.shapeshift_form_is_turn_sensitive =
            (form_entry->flags & 0x1) == 0;
      }
    }
    snapshot.has_aura_ignore_shapeshift =
        HasMatchingIgnoreShapeshiftAuraForSpell(*caster, *spell);

    SpellUsabilityChecker::SpellCastError stance_error{};
    if (!SpellUsabilityChecker::CheckStanceRequirement(
            usability_info, snapshot, &stance_error)) {

      if (stance_error ==
              SpellUsabilityChecker::SpellCastError::kOnlyShapeshift ||
              !caster->Interaction().CanAutoCancelShapeshiftFormForAction()) {
        return failure(static_cast<SpellCastResult>(stance_error));
      }
    }
  }

  {
    SpellUsabilityInfo usability_info{};
    usability_info.attributes = spell->attributes;
    usability_info.attributes_ex = spell->attributes_ex;

    PlayerStateSnapshot snapshot{};
    snapshot.is_stealthed = caster->State().IsStealth();
    snapshot.has_aura_ignore_shapeshift =
        HasMatchingIgnoreShapeshiftAuraForSpell(*caster, *spell);

    if (!SpellUsabilityChecker::CheckStealthedRequirement(
            usability_info, snapshot, nullptr)) {
      return failure(SpellCastResult::kOnlyStealthed);
    }

    if ((spell->attributes & 0x10000000u) != 0 &&
        (caster->State().GetUnitFlags2() & 0x80000u) != 0 &&
        !HasMatchingAllowOnlyAbilityAuraForSpell(*caster, *spell)) {
      return failure(SpellCastResult::kAffectingCombat);
    }
  }

  if (spell->caster_aura_spell != 0 &&
      !caster->Auras().HasSpellId(spell->caster_aura_spell) &&
      !HasMatchingAllowOnlyAbilityAuraForSpell(*caster, *spell)) {
    return failure(SpellCastResult::kCasterAuraState,
                   static_cast<std::int32_t>(spell->caster_aura_spell));
  }
  if (spell->exclude_caster_aura_spell != 0 &&
      caster->Auras().HasSpellId(spell->exclude_caster_aura_spell) &&
      !HasMatchingAllowOnlyAbilityAuraForSpell(*caster, *spell)) {
    return failure(SpellCastResult::kCasterAuraState,
                   static_cast<std::int32_t>(spell->exclude_caster_aura_spell));
  }
  if (spell->caster_aura_state != 0) {
    if (!caster->State().HasAuraState(
            1u << ((spell->caster_aura_state - 1u) & 31u)) &&
        !HasMatchingAllowOnlyAbilityAuraForSpell(*caster, *spell)) {
      return failure(SpellCastResult::kCasterAuraState);
    }
  }
  if (spell->caster_aura_state_not != 0) {
    if (caster->State().HasAuraState(
            1u << ((spell->caster_aura_state_not - 1u) & 31u)) &&
        !HasMatchingAllowOnlyAbilityAuraForSpell(*caster, *spell)) {
      return failure(SpellCastResult::kCasterAuraState);
    }
  }

  if (spell->target_aura_state != 0u ||
      spell->target_aura_state_not != 0u ||
      spell->target_aura_spell != 0u ||
      spell->exclude_target_aura_spell != 0u) {
    const auto target_aura_spell =
        ResolveEffectiveSpellId(session, spell->target_aura_spell);
    const auto exclude_target_aura_spell =
        ResolveEffectiveSpellId(session, spell->exclude_target_aura_spell);
    const auto* const target =
        ResolveSpellRequirementTarget(session, target_guid_ptr);
    if (target == nullptr) {

      if (spell->target_aura_state != 0u ||
          spell->target_aura_spell != 0u ||
          spell->exclude_target_aura_spell != 0u) {
        return failure(SpellCastResult::kBadTargets);
      }
    } else {
      const bool allow_only_ability =
          HasMatchingAllowOnlyAbilityAuraForSpell(*caster, *spell);
      if (spell->target_aura_state != 0u &&
          !target->State().HasAuraState(
              1u << ((spell->target_aura_state - 1u) & 31u)) &&
          !allow_only_ability) {
        return failure(
            SpellCastResult::kTargetAuraState,
            static_cast<std::int32_t>(spell->target_aura_state));
      }
      if (spell->target_aura_state_not != 0u &&
          target->State().HasAuraState(
              1u << ((spell->target_aura_state_not - 1u) & 31u)) &&
          !allow_only_ability) {
        return failure(
            SpellCastResult::kTargetAuraState,
            static_cast<std::int32_t>(spell->target_aura_state_not));
      }
      if (spell->target_aura_spell != 0u &&
          !target->Auras().HasSpellId(target_aura_spell) &&
          !allow_only_ability) {
        return failure(
            SpellCastResult::kTargetAuraState,
            static_cast<std::int32_t>(target_aura_spell));
      }
      if (spell->exclude_target_aura_spell != 0u &&
          target->Auras().HasSpellId(exclude_target_aura_spell) &&
          !allow_only_ability) {
        return failure(
            SpellCastResult::kTargetAuraState,
            static_cast<std::int32_t>(exclude_target_aura_spell));
      }
    }
  }

  if (spell->minimum_faction_id != 0u &&
      ReputationInfo::Get().GetStandingLevel(
          static_cast<std::int32_t>(spell->minimum_faction_id)) <
          spell->minimum_reputation) {
    return failure(SpellCastResult::kReputation,
                   static_cast<std::int32_t>(spell->minimum_faction_id));
  }

  if (spell->area_group_id > 0 &&
      (dbc == nullptr ||
       !IsCurrentAreaInGroup(*dbc, spell->area_group_id,
                             session.world_states().zone_id(),
                             session.world_states().area_id()))) {
    return failure(SpellCastResult::kIncorrectArea);
  }

  const float game_hour =
      static_cast<float>(session.scene_state().GetGameHour()) +
      static_cast<float>(session.scene_state().GetGameMinute()) / 60.0f;
  if ((spell->attributes & kAttr0OnlyDaytime) != 0u &&
      !IsObservedDaytime(game_hour)) {
    return failure(SpellCastResult::kOnlyDaytime);
  }
  if ((spell->attributes & kAttr0OnlyNighttime) != 0u &&
      IsObservedDaytime(game_hour)) {
    return failure(SpellCastResult::kOnlyNighttime);
  }

  if ((spell->attributes & kAttr0Tradespell) != 0 &&
      HasActiveSpellCooldown(spell_rec_ptr)) {
    return failure(SpellCastResult::kNotReady);
  }

  {
    const std::uint32_t aura_int = spell->aura_interrupt_flags;
    const std::uint32_t chan_int = spell->channel_interrupt_flags;
    const std::uint32_t attr_ex = spell->attributes_ex;
    const bool is_in_water = caster->Movement().IsInWater();
    const bool is_mounted = caster->Mount().IsMountedStateActive(*caster);

    if (((aura_int & kAuraIntFlagRequireUnsheathed) != 0u ||
         ((attr_ex & 0x44u) != 0u &&
          (chan_int & kAuraIntFlagRequireUnsheathed) != 0u)) &&
        caster->State().GetSheathState() == 0u) {
      return failure(SpellCastResult::kNotUnsheathed);
    }

    if (((aura_int & kAuraIntFlagRequireMounted) != 0u ||
         ((attr_ex & 0x44u) != 0u &&
          (chan_int & kAuraIntFlagRequireMounted) != 0u)) &&
        !is_mounted) {
      return failure(SpellCastResult::kOnlyMounted);
    }

    if (((aura_int & kAuraIntFlagRequireUnderwater) != 0 ||
         ((attr_ex & 0x44u) != 0 &&
          (chan_int & kAuraIntFlagRequireUnderwater) != 0)) &&
        !is_in_water) {
      return failure(SpellCastResult::kOnlyUnderwater);
    }
    if (((aura_int & kAuraIntFlagRequireAboveWater) != 0 ||
         ((attr_ex & 0x44u) != 0 &&
          (chan_int & kAuraIntFlagRequireAboveWater) != 0)) &&
        is_in_water) {
      return failure(SpellCastResult::kOnlyAbovewater);
    }
  }

  if ((spell->attributes & kAttr0IndoorOutdoorMask) != 0) {
    const CGPlayer_C* player = nullptr;
    if (caster->IsActivePlayer()) {
      player = static_cast<const CGPlayer_C*>(caster);
    } else if (const auto* const objects = caster->object_manager();
               objects != nullptr) {
      player = objects->GetActivePlayer();
    }
    const bool is_outdoors = player != nullptr && IsPlayerOutdoors(*player);
    if (is_outdoors && (spell->attributes & kAttr0IndoorOnly) != 0) {
      return failure(SpellCastResult::kOnlyIndoors);
    }
    if (!is_outdoors && (spell->attributes & kAttr0OutdoorOnly) != 0) {
      return failure(SpellCastResult::kOnlyOutdoors);
    }
  }

  if ((spell->attributes_ex4 & kAttrEx4CastOnlyInFlyableArea) != 0u &&
      !IsFlyableAreaForActivePlayer(session)) {
    return failure(SpellCastResult::kNotHere);
  }

  if ((spell->attributes_ex3 & kAttrEx3BattlegroundOnly) != 0 &&
      map_entry != nullptr && map_type != kMapTypeBattleground) {
    return failure(SpellCastResult::kOnlyBattlegrounds);
  }
  if ((spell->attributes_ex6 & kAttrEx6NotInRaid) != 0 &&
      map_entry != nullptr && map_type == kMapTypeRaid) {
    return failure(SpellCastResult::kNotInRaidInstance);
  }

  return {};
}

bool ValidateSpellRequirements(const WorldSession& session,
                               const std::uintptr_t caster,
                               const std::uintptr_t spell_rec,
                               const std::uintptr_t target_guid_ptr,
                               const std::uintptr_t item_template,
                               const bool force) {
  return ValidateSpellRequirementsDetailed(
             session, caster, spell_rec, target_guid_ptr, item_template, force)
      .IsSuccess();
}

bool TryAssignTarget(WorldSession& session,
                      std::uintptr_t target_unit,
                      std::uintptr_t spell_entry) {
  const auto kItemMask = SpellTargetFlag::kItem | SpellTargetFlag::kGoItem;
  const auto kGameObjectMask =
      SpellTargetFlag::kGameobject | SpellTargetFlag::kGoItem;

  if (target_unit == 0 || spell_entry == 0) {
    return false;
  }

  const auto* const dbc = session.GetDbcLoader();
  const auto* const spell =
      reinterpret_cast<const data::dbc::SpellEntry*>(spell_entry);
  auto& spell_client = session.spells();
  auto& targeting = spell_client.GetTargeting();
  if (dbc == nullptr || spell == nullptr || spell->id == 0 ||
      targeting.GetSpellId() != spell->id) {
    return false;
  }
  const SpellCastOrigin cast_origin =
      spell_client.GetPreparedOrigin(spell->id);
  const ObjectGuid prepared_caster =
      spell_client.GetPreparedCaster(spell->id);
  const auto* const caster = cast_origin == SpellCastOrigin::kPet
      ? session.objects().GetUnit(prepared_caster)
      : session.objects().GetActivePlayer();
  if (caster == nullptr) {
    return false;
  }

  const auto* const guid = reinterpret_cast<const ObjectGuid*>(target_unit);
  if (!*guid) {
    return false;
  }

  const auto* target = session.objects().GetObjectByGUID(*guid);
  if (target == nullptr) {
    target = CGObject_HasFlags(session.objects(), guid->GetRawValue(),
                               kTypeMaskObject);
  }
  if (target == nullptr) {
    return false;
  }

  const std::uint32_t original_mask = targeting.GetTargetMask();
  std::uint32_t consumed_mask = 0;
  std::uint32_t packet_target_mask = 0;
  ObjectGuid resolved_target = *guid;
  bool item_target = false;

  if (target->IsUnit()) {
    const auto unit_resolution = SpellTargetValidator::ResolveUnitTarget(
        session, *dbc, spell->id, *caster,
        static_cast<const CGUnit_C&>(*target), original_mask, true);
    if (!unit_resolution.IsValid()) {
      if (unit_resolution.result == SpellTargetResult::kOutOfRange ||
          unit_resolution.result == SpellTargetResult::kTooClose) {
        HandleCastFailure(session, spell_entry, spell_entry,
                          static_cast<std::uint8_t>(
                              SpellTargetResultToCastResult(
                                  unit_resolution.result)),
                          -1, -1, false);
      }
      return false;
    }
    resolved_target = unit_resolution.resolved_target;
    consumed_mask = unit_resolution.consumed_target_mask;
    packet_target_mask = unit_resolution.packet_target_mask;
  } else if (target->IsItem()) {
    if (!HasFlag(static_cast<SpellTargetFlag>(original_mask), kItemMask)) {
      return false;
    }
    consumed_mask = static_cast<std::uint32_t>(kItemMask);
    packet_target_mask = static_cast<std::uint32_t>(SpellTargetFlag::kItem);
    item_target = true;
  } else if (target->IsGameObject()) {
    if (!HasFlag(static_cast<SpellTargetFlag>(original_mask),
                 kGameObjectMask)) {
      return false;
    }
    consumed_mask = static_cast<std::uint32_t>(kGameObjectMask);
    packet_target_mask =
        static_cast<std::uint32_t>(SpellTargetFlag::kGameobject);
  } else if (target->IsCorpse()) {
    const bool friendly = caster->Interaction().IsFriendlyCorpseTarget(
        static_cast<const CGCorpse_C&>(*target));
    const auto target_mask = static_cast<SpellTargetFlag>(original_mask);
    if (friendly && HasFlag(target_mask, SpellTargetFlag::kCorpseAlly)) {
      consumed_mask = packet_target_mask =
          static_cast<std::uint32_t>(SpellTargetFlag::kCorpseAlly);
    } else if (!friendly &&
               HasFlag(target_mask, SpellTargetFlag::kCorpseEnemy)) {
      consumed_mask = packet_target_mask =
          static_cast<std::uint32_t>(SpellTargetFlag::kCorpseEnemy);
    } else {
      return false;
    }
  } else {
    return false;
  }

  if (consumed_mask == 0u || packet_target_mask == 0u) {
    return false;
  }

  if (!spell_client.PrepareTargetedCast(spell->id, cast_origin,
                                        caster->GetGuid())) {
    return false;
  }
  if (item_target) {
    spell_client.SetItemTarget(SpellSlotType::kCurrent, resolved_target);
  } else {
    spell_client.SetObjectTarget(SpellSlotType::kCurrent, resolved_target,
                                 packet_target_mask);
  }

  const std::uint32_t remaining_mask = original_mask & ~consumed_mask;
  targeting.SetTargetMask(remaining_mask);

  if (remaining_mask != 0u) {
    return true;
  }

  const SpellCastResult result = spell_client.CastPreparedSpell(session);
  spell_client.DiscardPreparedTargetedCast(spell->id);
  targeting.CancelTargeting();
  if (result != SpellCastResult::kSuccess) {
    HandleCastFailure(session, spell_entry, spell_entry,
                      static_cast<std::uint8_t>(result), -1, -1, false);
    return false;
  }
  return true;
}

}

#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"

#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/spell_runtime_values.h"
#include "openwow/game/spell_target_resolver.h"
#include "openwow/game/spell_validation.h"
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
#include "openwow/game/spell_book.h"
#include "openwow/game/spell_c_internals.h"
#include "openwow/game/spell_cost_and_range.h"
#include "openwow/foundation/diagnostics/logging.h"
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

void UseSpellAction(const WorldSession& session, std::uintptr_t action_data,
                    std::uint32_t spell_id) {
  SpellCastDiagnostics::Get().last_action_invocation =
      SpellActionInvocation{.action_data = action_data, .spell_id = spell_id};

  const std::uint64_t target_guid =
      session.objects().GetTargetGuid().GetRawValue();
  (void)SpellAction_ValidateAndInitiateCast(
      session, spell_id, target_guid, -1, 0);
}

namespace detail {

const std::optional<SpellActionInvocation> &LastUseSpellActionInvocation() {
  return SpellCastDiagnostics::Get().last_action_invocation;
}

void ClearLastUseSpellActionInvocation() {
  SpellCastDiagnostics::Get().last_action_invocation.reset();
}

std::uint32_t GetLastCastFailureReasonForTests() {
  return SpellCastDiagnostics::Get().last_cast_failure_reason;
}

void SetLastCastFailureReasonForTests(const std::uint32_t reason) {
  SpellCastDiagnostics::Get().last_cast_failure_reason = reason;
}

}

bool ValidateAllTargets(const WorldSession& session,
                         std::uintptr_t caster,
                         std::uintptr_t spell_rec,
                         std::uint32_t target_count,
                         std::uintptr_t target_list,
                         std::uint32_t* out_error,
                         bool has_item_target) {
  if (out_error) *out_error = 0;
  if (target_count == 0) return true;
  if (spell_rec == 0) return false;

  const auto* spell =
      reinterpret_cast<const data::dbc::SpellEntry*>(spell_rec);
  const auto* dbc = session.GetDbcLoader();
  if (dbc == nullptr) return false;

  const auto* range_entry =
      spell->range_index != 0 ? dbc->spell_range().LookupEntry(spell->range_index)
                               : nullptr;
  float min_range = 0.0f, max_range = 0.0f;
  if (range_entry != nullptr) {
    min_range = range_entry->range_min;
    max_range = range_entry->range_max;
  }

  const bool is_harmful = GetHelpfulHarmfulDisposition(*spell) ==
                          SpellHelpfulHarmfulDisposition::kHarmful;
  const auto requirements = SpellTargetValidator::BuildRequirements(
      *spell, !is_harmful, min_range, max_range);

  constexpr std::size_t kTargetEntrySize = 40;
  const std::uint8_t* base =
      reinterpret_cast<const std::uint8_t*>(target_list);
  const auto* objects = &session.objects();
  const auto* caster_unit =
      caster != 0 ? reinterpret_cast<const CGUnit_C*>(caster) : nullptr;

  for (std::uint32_t i = 0; i < target_count; ++i) {
    const auto* entry_base = base + i * kTargetEntrySize;
    const auto* guid = reinterpret_cast<const ObjectGuid*>(entry_base + 8);
    if (guid == nullptr || guid->IsEmpty()) {
      continue;
    }

    const auto* target_unit = objects->GetUnit(*guid);
    if (target_unit == nullptr) {
      if (out_error != nullptr && *out_error == 0) {
        *out_error = static_cast<std::uint32_t>(
            SpellCastResult::kBadImplicitTargets);
      }
      return false;
    }

    UnitTargetInfo info;
    info.guid = *guid;
    info.is_dead = target_unit->State().IsDead();
    info.is_player = target_unit->IsPlayer();
    info.is_self = caster_unit != nullptr &&
                   caster_unit->GetGuid() == *guid;
    info.is_in_party =
        GroupSystem::Get().IsPartyUnitGuid(session.objects(), guid->GetRawValue());
    info.is_in_raid =
        GroupSystem::Get().IsRaidUnitGuid(session.objects(), guid->GetRawValue());
    info.relation = caster_unit != nullptr
        ? (caster_unit->GetGuid() == *guid
             ? UnitRelation::kFriendly
             : caster_unit->Interaction().CanAttackSpellTarget(*target_unit)
                 ? UnitRelation::kHostile
                  : caster_unit->Interaction().CanAssistSpellTarget(*target_unit, false)
                     ? UnitRelation::kFriendly
                     : UnitRelation::kNeutral)
        : UnitRelation::kNeutral;
    info.distance = caster_unit != nullptr
        ? static_cast<float>(std::sqrt(caster_unit->GetSquaredDistanceToPosition(
              target_unit->GetPosition())))
        : 0.0f;
    info.is_immune = false;
    info.creature_type = SpellTargetValidator::GetSpellTargetCreatureTypeId(
        session, *target_unit);

    const auto result = SpellTargetValidator::Validate(requirements, info);
    if (result != SpellTargetResult::kValid) {
      if (out_error != nullptr && *out_error == 0) {
        *out_error = static_cast<std::uint32_t>(
            SpellTargetResultToCastResult(result));
      }
      return false;
    }
  }

  (void)has_item_target;
  return true;
}

bool IsTargetInRange(const WorldSession& session,
                     const CGObject_C& target,
                     const std::uint32_t spell_id,
                     bool* out_of_range,
                     const CGUnit_C& caster) {
  if (out_of_range != nullptr) {
    *out_of_range = false;
  }

  float min_range = 0.0f;
  float max_range = 0.0f;
  WriteTargetRangeWindow(
      session, spell_id, caster, &target, &min_range, &max_range);

  const auto distance_sq =
      caster.GetSquaredDistanceToPosition(target.GetPosition());
  const auto min_range_sq =
      static_cast<double>(min_range) * static_cast<double>(min_range);
  if (min_range_sq > distance_sq) {
    return false;
  }

  const auto max_range_sq =
      static_cast<double>(max_range) * static_cast<double>(max_range);
  if (max_range_sq >= distance_sq) {
    return true;
  }

  if (out_of_range != nullptr) {
    *out_of_range = true;
  }
  return false;
}

static constexpr std::uint32_t kSpellInterruptFlagInterrupt = 0x00000008u;

static constexpr std::uint32_t kChannelInterruptFlagSuppressAttack = 0x00001000u;

bool HasActiveInterruptibleCast(const WorldSession& session,
                                const ObjectGuid& caster_guid) {

  const auto* dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return false;
  }

  const auto& spell_client = session.spells();
  const auto raw_guid = caster_guid.GetRawValue();

  static constexpr std::array<SpellSlotType, 3> kSlots = {
      SpellSlotType::kCurrent,
      SpellSlotType::kAutoRepeat,
      SpellSlotType::kChannel,
  };

  for (const auto slot_type : kSlots) {
    const auto& slot = spell_client.GetSlot(slot_type);
    if (slot.state == SpellClientState::kIdle) {
      continue;
    }
    if (slot.caster_guid.GetRawValue() != raw_guid) {
      continue;
    }

    const auto* spell = dbc->spell().LookupEntry(slot.spell_id);
    if (spell == nullptr) {
      continue;
    }

    if ((spell->interrupt_flags & kSpellInterruptFlagInterrupt) != 0) {
      return true;
    }
  }

  constexpr std::uint32_t kTypeMaskUnit = 8u;
  const auto* obj = CGObject_HasFlags(session.objects(), raw_guid,
                                      kTypeMaskUnit);
  if (obj == nullptr) {
    return false;
  }

  auto* unit = static_cast<const CGUnit_C*>(obj);
  const auto channel_spell_id = unit->Casts().GetChannelSpellId(*unit);
  if (channel_spell_id == 0) {
    return false;
  }

  const auto* channel_spell = dbc->spell().LookupEntry(channel_spell_id);
  if (channel_spell == nullptr) {
    return false;
  }

  if ((channel_spell->channel_interrupt_flags &
       kChannelInterruptFlagSuppressAttack) != 0) {
    return true;
  }

  return false;
}

bool IsValidSpellTarget(const WorldSession& session,
                         std::uintptr_t spell_rec,
                         std::uintptr_t target_unit,
                         std::uint32_t target_mask,
                         std::uintptr_t caster) {
  if (spell_rec == 0 || target_unit == 0) return false;

  const auto* spell =
      reinterpret_cast<const data::dbc::SpellEntry*>(spell_rec);
  const auto* target = reinterpret_cast<const CGObject_C*>(target_unit);
  const auto* caster_unit =
      caster != 0 ? reinterpret_cast<const CGUnit_C*>(caster) : nullptr;

  const auto* dbc = session.GetDbcLoader();
  if (dbc == nullptr) return false;

  const bool expects_unit = (target_mask & 0x08u) != 0;
  const bool expects_game_object = (target_mask & 0x01u) != 0;
  const bool expects_item = (target_mask & 0x02u) != 0;
  const bool expects_corpse = (target_mask & 0x80u) != 0;

  bool is_target_unit = target->IsUnit();
  bool is_target_game_object = target->IsGameObject();
  bool is_target_corpse = target->IsCorpse();

  if (expects_unit && !is_target_unit) return false;
  if (expects_game_object && !is_target_game_object) return false;
  if (expects_item && is_target_unit) {
  }
  if (expects_corpse && !is_target_corpse) return false;

  if (is_target_unit) {
    const auto* unit = static_cast<const CGUnit_C*>(target);

    if ((spell->attributes_ex2 & 0x00000001u) == 0) {
      if (unit->State().IsDead()) return false;
    } else {
      if (!unit->State().IsDead()) return false;
    }

    if (caster_unit != nullptr) {
      const bool is_self = (unit->GetGuid() == caster_unit->GetGuid());
      if ((spell->attributes_ex & 0x00080000u) != 0 && is_self) {
        return false;
      }
    }

    const bool is_harmful = GetHelpfulHarmfulDisposition(*spell) ==
                            SpellHelpfulHarmfulDisposition::kHarmful;
    if (is_harmful && caster_unit != nullptr) {
      if (!caster_unit->Interaction().CanAttackSpellTarget(*unit)) {
        return false;
      }
    } else if (!is_harmful && caster_unit != nullptr) {
      if (caster_unit->Interaction().CanAttackSpellTarget(*unit)) {
        if (!caster_unit->Interaction().CanAssistSpellTarget(*unit, false)) {
          return false;
        }
      }
    }

    float min_range = 0.0f, max_range = 0.0f;
    if (GetTargetRangeWindow(session, spell->id,
                             caster_unit != nullptr ? *caster_unit : *unit,
                             target, &min_range, &max_range)) {
      const float dist_sq = caster_unit != nullptr
          ? caster_unit->GetSquaredDistanceToPosition(target->GetPosition())
          : 0.0f;
      const float max_sq = max_range * max_range;
      if (max_range > 0.0f && dist_sq > max_sq) return false;
      const float min_sq = min_range * min_range;
      if (min_range > 0.0f && dist_sq < min_sq) return false;
    }

    if (spell->target_creature_type != 0) {
      const auto target_creature =
          static_cast<std::uint32_t>(unit->State().GetCreatureType());
      if (target_creature != 0 &&
          (spell->target_creature_type & (1u << (target_creature - 1))) == 0) {
        return false;
      }
    }

  }

  return true;
}

SpellGroundClickValidation ValidateSpellGroundClickPosition(
    const WorldSession& session,
    const SpellGroundClickData& click,
    const bool allow_range_validation) {
  const auto target_mask = static_cast<SpellTargetFlag>(
      session.spells().GetTargeting().GetTargetMask());
  if (!HasFlag(target_mask, SpellTargetFlag::kSourceLocation) &&
      !HasFlag(target_mask, SpellTargetFlag::kDestLocation)) {
    return SpellGroundClickValidation::kOutOfRange;
  }

  const auto* caster = session.objects().GetActivePlayer();
  if (caster == nullptr) {
    return SpellGroundClickValidation::kOutOfRange;
  }

  if (!allow_range_validation) {
    return SpellGroundClickValidation::kInRange;
  }

  const auto range_window =
      QueryActiveGroundClickRangeWindow(session, session.spells());
  if (!range_window.has_value()) {
    return SpellGroundClickValidation::kOutOfRange;
  }

  const Position world_position = ResolveGroundClickWorldPosition(session, click);
  const double distance_sq = caster->GetSquaredDistanceToPosition(world_position);
  const double min_range_sq =
      static_cast<double>(range_window->min_range) * range_window->min_range;
  const double max_range_sq =
      static_cast<double>(range_window->max_range) * range_window->max_range;

  if (range_window->min_range > 0.0f && distance_sq < min_range_sq) {
    return distance_sq > (max_range_sq * 4.0)
               ? SpellGroundClickValidation::kTooFar
               : SpellGroundClickValidation::kOutOfRange;
  }

  if (distance_sq <= max_range_sq) {
    return SpellGroundClickValidation::kInRange;
  }

  return distance_sq > (max_range_sq * 4.0)
             ? SpellGroundClickValidation::kTooFar
             : SpellGroundClickValidation::kOutOfRange;
}

constexpr std::uint32_t kAuraTypeModPossess       = 177u;

bool CGUnit_C__HasCompatibleStunAura(
    const CGUnit_C& unit,
    const data::dbc::DbcLoader& dbc,
    const data::dbc::SpellEntry* spell,
    std::uint32_t* blocking_mechanic_out) {
  if (blocking_mechanic_out != nullptr) {
    *blocking_mechanic_out = 0;
  }
  return CGUnit_C__HasCompatibleAuraType(unit, dbc, spell,
                                         kAuraTypeModStun,
                                         blocking_mechanic_out);
}

bool CGUnit_C__HasCompatiblePacifyAura(
    const CGUnit_C& unit,
    const data::dbc::DbcLoader& dbc,
    const data::dbc::SpellEntry* spell,
    std::uint32_t* blocking_mechanic_out) {
  if (blocking_mechanic_out != nullptr) {
    *blocking_mechanic_out = 0;
  }
  return CGUnit_C__HasCompatibleAuraType(unit, dbc, spell,
                                         kAuraTypeModPacify,
                                         blocking_mechanic_out) ||
         CGUnit_C__HasCompatibleAuraType(unit, dbc, spell,
                                         kAuraTypeModPacifySilence,
                                         blocking_mechanic_out);
}

bool CGUnit_C__HasCompatibleSilenceAura(
    const CGUnit_C& unit,
    const data::dbc::DbcLoader& dbc,
    const data::dbc::SpellEntry* spell,
    std::uint32_t* blocking_mechanic_out) {
  if (blocking_mechanic_out != nullptr) {
    *blocking_mechanic_out = 0;
  }
  return CGUnit_C__HasCompatibleAuraType(unit, dbc, spell,
                                         kAuraTypeModSilence,
                                         blocking_mechanic_out) ||
         CGUnit_C__HasCompatibleAuraType(unit, dbc, spell,
                                         kAuraTypeModPacifySilence,
                                         blocking_mechanic_out);
}

bool CGUnit_C__HasCompatibleFearAura(
    const CGUnit_C& unit,
    const data::dbc::DbcLoader& dbc,
    const data::dbc::SpellEntry* spell,
    std::uint32_t* blocking_mechanic_out) {
  if (blocking_mechanic_out != nullptr) {
    *blocking_mechanic_out = 0;
  }
  return CGUnit_C__HasCompatibleAuraType(unit, dbc, spell,
                                         kAuraTypeModFear,
                                         blocking_mechanic_out);
}

bool CGUnit_C__HasCompatibleConfuseAura(
    const CGUnit_C& unit,
    const data::dbc::DbcLoader& dbc,
    const data::dbc::SpellEntry* spell,
    std::uint32_t* blocking_mechanic_out) {
  if (blocking_mechanic_out != nullptr) {
    *blocking_mechanic_out = 0;
  }
  return CGUnit_C__HasCompatibleAuraType(unit, dbc, spell,
                                         kAuraTypeModConfuse,
                                         blocking_mechanic_out);
}

bool CGUnit_C__HasCompatibleCharmAura(
    const CGUnit_C& unit,
    const data::dbc::DbcLoader& dbc,
    const data::dbc::SpellEntry* spell,
    std::uint32_t* blocking_mechanic_out) {
  if (blocking_mechanic_out != nullptr) {
    *blocking_mechanic_out = 0;
  }
  return CGUnit_C__HasCompatibleAuraType(unit, dbc, spell,
                                         kAuraTypeModCharm,
                                         blocking_mechanic_out) ||
         CGUnit_C__HasCompatibleAuraType(unit, dbc, spell,
                                         kAuraTypeModPossess,
                                         blocking_mechanic_out) ||
         CGUnit_C__HasCompatibleAuraType(unit, dbc, spell,
                                         kAuraTypeModPossessPet,
                                         blocking_mechanic_out);
}

SpellCastResult ValidateSpellCasterControlState(
    const CGUnit_C& caster,
    const data::dbc::DbcLoader& dbc,
    const data::dbc::SpellEntry& spell,
    const ObjectGuid& active_player_guid,
    std::uint32_t* const blocking_mechanic_out) {
  constexpr std::uint32_t kAttrEx5UsableWhileStunned = 0x00000008u;
  constexpr std::uint32_t kAttrEx5UsableWhileFeared = 0x00020000u;
  constexpr std::uint32_t kAttrEx5UsableWhileConfused = 0x00040000u;
  constexpr std::uint32_t kAttrEx7UsableWhileControlled = 0x00100000u;
  constexpr std::uint32_t kPreventionTypeSilence = 1u;
  constexpr std::uint32_t kPreventionTypePacify = 2u;

  if (blocking_mechanic_out != nullptr) {
    *blocking_mechanic_out = 0u;
  }

  const auto blocked_result = [&](const SpellCastResult base_result,
                                  const auto compatibility_check) {
    std::uint32_t blocking_mechanic = 0u;
    if (compatibility_check(&blocking_mechanic)) {
      return SpellCastResult::kSuccess;
    }
    if (blocking_mechanic_out != nullptr) {
      *blocking_mechanic_out = blocking_mechanic;
    }
    return blocking_mechanic == 0u
               ? base_result
               : SpellCastResult::kPreventedByMechanic;
  };

  const ObjectGuid charmer = caster.State().GetCharmedBy();
  if (charmer && charmer != active_player_guid) {
    const auto result = blocked_result(
        SpellCastResult::kCharmed,
        [&](std::uint32_t* mechanic) {
          return CGUnit_C__HasCompatibleCharmAura(
              caster, dbc, &spell, mechanic);
        });
    if (result != SpellCastResult::kSuccess) {
      return result;
    }
  }

  if (caster.State().IsStunned() &&
      (spell.attributes_ex5 & kAttrEx5UsableWhileStunned) == 0u &&
      (spell.attributes_ex7 & kAttrEx7UsableWhileControlled) == 0u) {
    const auto result = blocked_result(
        SpellCastResult::kStunned,
        [&](std::uint32_t* mechanic) {
          return CGUnit_C__HasCompatibleStunAura(
              caster, dbc, &spell, mechanic);
        });
    if (result != SpellCastResult::kSuccess) {
      return result;
    }
  }

  if (spell.prevention_type == kPreventionTypeSilence &&
      caster.State().IsSilenced()) {
    const auto result = blocked_result(
        SpellCastResult::kSilenced,
        [&](std::uint32_t* mechanic) {
          return CGUnit_C__HasCompatibleSilenceAura(
              caster, dbc, &spell, mechanic);
        });
    if (result != SpellCastResult::kSuccess) {
      return result;
    }
  }

  if (spell.prevention_type == kPreventionTypePacify &&
      caster.State().IsPacified()) {
    const auto result = blocked_result(
        SpellCastResult::kPacified,
        [&](std::uint32_t* mechanic) {
          return CGUnit_C__HasCompatiblePacifyAura(
              caster, dbc, &spell, mechanic);
        });
    if (result != SpellCastResult::kSuccess) {
      return result;
    }
  }

  if (caster.State().IsFleeing() &&
      (spell.attributes_ex5 & kAttrEx5UsableWhileFeared) == 0u &&
      (spell.attributes_ex7 & kAttrEx7UsableWhileControlled) == 0u) {
    const auto result = blocked_result(
        SpellCastResult::kFleeing,
        [&](std::uint32_t* mechanic) {
          return CGUnit_C__HasCompatibleFearAura(
              caster, dbc, &spell, mechanic);
        });
    if (result != SpellCastResult::kSuccess) {
      return result;
    }
  }

  if (caster.State().IsConfused() &&
      (spell.attributes_ex5 & kAttrEx5UsableWhileConfused) == 0u &&
      (spell.attributes_ex7 & kAttrEx7UsableWhileControlled) == 0u) {
    return blocked_result(
        SpellCastResult::kConfused,
        [&](std::uint32_t* mechanic) {
          return CGUnit_C__HasCompatibleConfuseAura(
              caster, dbc, &spell, mechanic);
        });
  }

  return SpellCastResult::kSuccess;
}

}

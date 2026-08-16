#include "openwow/game/spell_aura_candidate_validation.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/game/aura_tracker.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/spell_cast_execution.h"
#include "openwow/game/spell_effective_variant.h"
#include "openwow/game/spell_public_values.h"
#include "openwow/game/world_session.h"

#include <cstddef>

namespace openwow::game {
namespace {

constexpr std::uint32_t kSpellAttrPassive = 0x00000040u;
constexpr std::uint32_t kSpellAttrExChanneledMask = 0x00000044u;
constexpr std::uint32_t kSpellAttrEx4NotStealable = 0x00000040u;
constexpr std::uint8_t kAuraNegative = 0x80u;
constexpr std::uint32_t kSpellEffectDispel = 38u;
constexpr std::uint32_t kSpellEffectDispelMechanic = 108u;
constexpr std::uint32_t kSpellEffectStealBeneficialBuff = 126u;
constexpr std::uint32_t kSpellAttrExIgnoreTargetRelation = 0x00010000u;
constexpr std::uint32_t kSpellAttrExTargetNotInCombat = 0x00000100u;
constexpr std::uint32_t kSpellAttrEx4NoAuraCandidateFailureEffect0 =
    0x20000000u;

bool IsAppliedEffect(const std::uint32_t effect_id) {
  return effect_id == 2u || effect_id == 6u || effect_id == 177u;
}

bool IsHelpfulAura(const AuraData& aura) {
  return aura.has_raw_flags ? (aura.raw_flags & kAuraNegative) == 0u
                            : aura.IsBuff();
}

bool IsAuraCandidateEffect(const std::uint32_t effect_id) {
  return effect_id == kSpellEffectDispel ||
         effect_id == kSpellEffectDispelMechanic ||
         effect_id == kSpellEffectStealBeneficialBuff;
}

std::uint32_t ResolveDispelTypeMask(
    const WorldSession& session, const std::uint32_t dispel_type) {
  const auto* const dbc = session.GetDbcLoader();
  const auto* const dispel =
      dbc != nullptr ? dbc->spell_dispel_type().LookupEntry(dispel_type)
                     : nullptr;
  return dispel != nullptr && dispel->mask != 0u
             ? dispel->mask
             : 1u << (dispel_type & 31u);
}

bool TargetHasAuraCandidate(
    const WorldSession& session, const CGUnit_C& caster,
    const CGUnit_C& target, const std::int32_t requested_value,
    const SpellAuraCandidateMatch match, const bool reject_nonstealable,
    const bool ignore_relation) {
  bool helpful_only = false;
  bool require_applied_effect = false;
  if (!ignore_relation) {
    if (caster.Interaction().CanAttackSpellTarget(target)) {
      helpful_only = true;
    } else if (!caster.Interaction().CanAssistSpellTarget(target, false)) {
      return false;
    }
  }

  if (!ignore_relation && target.State().HasCharmedByGUID() &&
      !target.GetGuid().IsVehicle()) {
    if (!helpful_only) {
      return false;
    }
    require_applied_effect = true;
  }

  const auto requested = static_cast<std::uint32_t>(requested_value);
  const SpellAuraCandidateCriteria criteria{
      .match = match,
      .requested_mask_or_mechanic =
          match == SpellAuraCandidateMatch::kDispelType
              ? ResolveDispelTypeMask(session, requested)
              : requested,
      .helpful_only = helpful_only,
      .require_applied_effect = require_applied_effect,
      .reject_nonstealable = reject_nonstealable,
  };

  bool found = false;
  const auto* const dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return false;
  }
  AuraTracker::Get().ForEachAuraAll(
      target.GetGuid(),
      [&](const std::uint8_t , const AuraData& aura) {
        if (found) {
          return;
        }
        const auto* const aura_spell = dbc->spell().LookupEntry(aura.spell_id);
        if (aura_spell != nullptr &&
            IsSpellAuraCandidate(BuildSpellAuraCandidateData(*aura_spell),
                                 aura, criteria)) {
          found = true;
        }
      });
  return found;
}

}

SpellAuraCandidateData BuildSpellAuraCandidateData(
    const data::dbc::SpellEntry& spell) {
  return SpellAuraCandidateData{
      .mechanic = spell.mechanic,
      .attributes = spell.attributes,
      .attributes_ex = spell.attributes_ex,
      .attributes_ex4 = spell.attributes_ex4,
      .dispel_type = spell.dispel,
      .effect_ids = spell.effect,
      .effect_mechanics = spell.effect_mechanic,
  };
}

bool IsAuraVisibilityOnlyEffect(const std::uint32_t effect_id) {
  switch (effect_id) {
    case 27u:
    case 35u:
    case 65u:
    case 119u:
    case 128u:
    case 129u:
    case 143u:
      return true;
    default:
      return false;
  }
}

bool IsSpellAuraCandidate(
    const SpellAuraCandidateData& aura_spell,
    const AuraData& aura,
    const SpellAuraCandidateCriteria& criteria) {
  if ((aura_spell.attributes & kSpellAttrPassive) != 0u ||
      (criteria.helpful_only && !IsHelpfulAura(aura))) {
    return false;
  }

  const std::uint8_t active_effects = aura.ActiveEffectMask();
  bool has_applied_effect = false;
  for (std::size_t effect_index = 0;
       effect_index < aura_spell.effect_ids.size(); ++effect_index) {
    const std::uint8_t effect_bit =
        static_cast<std::uint8_t>(1u << effect_index);
    if ((active_effects & effect_bit) == 0u) {
      continue;
    }
    const auto effect_id = aura_spell.effect_ids[effect_index];
    if (IsAuraVisibilityOnlyEffect(effect_id)) {
      return false;
    }
    has_applied_effect = has_applied_effect || IsAppliedEffect(effect_id);
  }
  if (criteria.require_applied_effect && !has_applied_effect) {
    return false;
  }

  if (criteria.reject_nonstealable &&
      ((aura_spell.attributes_ex & kSpellAttrExChanneledMask) != 0u ||
       (aura_spell.attributes_ex4 & kSpellAttrEx4NotStealable) != 0u)) {
    return false;
  }

  if (criteria.match == SpellAuraCandidateMatch::kDispelType) {
    return (criteria.requested_mask_or_mechanic &
            (1u << (aura_spell.dispel_type & 31u))) != 0u;
  }

  if (aura_spell.mechanic == criteria.requested_mask_or_mechanic) {
    return true;
  }
  for (std::size_t effect_index = 0;
       effect_index < aura_spell.effect_ids.size(); ++effect_index) {
    if (aura_spell.effect_ids[effect_index] != 0u &&
        aura_spell.effect_mechanics[effect_index] ==
            criteria.requested_mask_or_mechanic) {
      return true;
    }
  }
  return false;
}

SpellCastResult ValidateSpellTargetCandidates(
    const WorldSession& session,
    const data::dbc::SpellEntry& spell,
    const CGUnit_C& caster,
    const std::span<const SpellTargetCandidateUnit> candidate_units) {
  if (candidate_units.empty()) {
    return SpellCastResult::kSuccess;
  }
  if ((spell.attributes_ex & kSpellAttrExTargetNotInCombat) != 0u) {
    bool found_noncombat_target = false;
    for (const auto& candidate : candidate_units) {
      found_noncombat_target =
          found_noncombat_target ||
          (candidate.unit != nullptr && !candidate.unit->State().IsInCombat());
    }
    if (!found_noncombat_target) {
      return SpellCastResult::kTargetAffectingCombat;
    }
  }

  const bool has_target_aura_requirements =
      spell.target_aura_state != 0u || spell.target_aura_state_not != 0u ||
      spell.target_aura_spell != 0u || spell.exclude_target_aura_spell != 0u;
  if (has_target_aura_requirements) {
    const auto target_aura_spell =
        ResolveEffectiveSpellId(session, spell.target_aura_spell);
    const auto exclude_target_aura_spell =
        ResolveEffectiveSpellId(session, spell.exclude_target_aura_spell);
    const bool ignore_aura_requirements =
        HasMatchingAllowOnlyAbilityAuraForSpell(caster, spell);
    bool found_valid_target = false;
    for (const auto& candidate : candidate_units) {
      if (candidate.unit == nullptr || candidate.is_implicit_caster) {
        continue;
      }
      const auto& target = *candidate.unit;
      const bool has_required_state =
          spell.target_aura_state == 0u ||
          target.State().HasAuraState(
              1u << ((spell.target_aura_state - 1u) & 31u));
      const bool lacks_excluded_state =
          spell.target_aura_state_not == 0u ||
          !target.State().HasAuraState(
              1u << ((spell.target_aura_state_not - 1u) & 31u));
      const bool has_required_spell =
          spell.target_aura_spell == 0u ||
          target.Auras().HasSpellId(target_aura_spell);
      const bool lacks_excluded_spell =
          spell.exclude_target_aura_spell == 0u ||
          !target.Auras().HasSpellId(exclude_target_aura_spell);
      if ((has_required_state && lacks_excluded_state &&
           has_required_spell && lacks_excluded_spell) ||
          ignore_aura_requirements) {
        found_valid_target = true;
        break;
      }
    }
    if (!found_valid_target) {
      return SpellCastResult::kTargetAuraState;
    }
  }

  bool has_dispel_effect = false;
  bool has_steal_effect = false;
  for (const auto effect_id : spell.effect) {
    has_dispel_effect = has_dispel_effect ||
                        effect_id == kSpellEffectDispel ||
                        effect_id == kSpellEffectDispelMechanic;
    has_steal_effect = has_steal_effect ||
                       effect_id == kSpellEffectStealBeneficialBuff;
  }
  if (!has_dispel_effect && !has_steal_effect) {
    return SpellCastResult::kSuccess;
  }

  const bool ignore_relation =
      (spell.attributes_ex & kSpellAttrExIgnoreTargetRelation) != 0u;
  bool bypass_empty_candidate_failure = false;
  bool has_candidate_unit = false;
  for (const auto& candidate : candidate_units) {
    const auto* const target = candidate.unit;
    if (target == nullptr) {
      continue;
    }
    has_candidate_unit = true;
    for (std::size_t effect_index = 0; effect_index < spell.effect.size();
         ++effect_index) {
      const auto effect_id = spell.effect[effect_index];
      if (!IsAuraCandidateEffect(effect_id)) {
        continue;
      }
      const auto match = effect_id == kSpellEffectDispelMechanic
                             ? SpellAuraCandidateMatch::kMechanic
                             : SpellAuraCandidateMatch::kDispelType;
      if (TargetHasAuraCandidate(
              session, caster, *target, spell.effect_misc_value[effect_index],
              match, effect_id == kSpellEffectStealBeneficialBuff,
              ignore_relation)) {
        return SpellCastResult::kSuccess;
      }
      bypass_empty_candidate_failure =
          bypass_empty_candidate_failure ||
          (spell.attributes_ex4 &
           (kSpellAttrEx4NoAuraCandidateFailureEffect0 << effect_index)) != 0u;
    }
  }

  if (!has_candidate_unit) {
    return SpellCastResult::kSuccess;
  }
  if (bypass_empty_candidate_failure) {
    return SpellCastResult::kSuccess;
  }
  for (const auto effect_id : spell.effect) {
    if (effect_id != 0u && !IsAuraCandidateEffect(effect_id)) {
      return SpellCastResult::kSuccess;
    }
  }
  return has_dispel_effect ? SpellCastResult::kNothingToDispel
                           : SpellCastResult::kNothingToSteal;
}

}

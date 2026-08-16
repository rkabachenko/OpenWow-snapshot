
#include "openwow/game/aura_application.h"

#include "openwow/data/formats/dbc/dbc_entries_gameplay.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/game/proc_manager.h"

namespace openwow::game {

namespace {

constexpr std::uint32_t kSpellAttrEx3CanProcWithProcs = 0x00000020u;

}

AuraApplication& AuraApplication::Get() {
  static AuraApplication instance;
  return instance;
}

AuraStackingRule AuraApplication::ResolveStackingRule(
    const data::dbc::SpellEntry& spell,
    const std::size_t effect_index) {

  if (effect_index < spell.effect_apply_aura.size()) {
    if (spell.effect_apply_aura[effect_index] == kAuraTypeModImmunity) {
      return AuraStackingRule::kExclusive;
    }
  }

  if ((spell.attributes_ex3 & kSpellAttrEx3CanProcWithProcs) != 0) {
    return AuraStackingRule::kExclusiveSameEffect;
  }

  if (effect_index < spell.effect_apply_aura.size()) {
    const auto aura_type = spell.effect_apply_aura[effect_index];
    if (aura_type == kAuraTypeModStun ||
        aura_type == kAuraTypeModFear ||
        aura_type == kAuraTypeModCharm ||
        aura_type == kAuraTypeModConfuse ||
        aura_type == kAuraTypeModPossessPet ||
        aura_type == kAuraTypeModSilence ||
        aura_type == kAuraTypeModPacify ||
        aura_type == kAuraTypeModPacifySilence) {
      return AuraStackingRule::kCrowdControl;
    }
  }

  if (effect_index < spell.effect_mechanic.size()) {
    const auto mechanic = spell.effect_mechanic[effect_index];

    if (mechanic == 1 || mechanic == 2 || mechanic == 3 ||
        mechanic == 7 || mechanic == 11 || mechanic == 12 ||
        mechanic == 14 || mechanic == 16 || mechanic == 17 ||
        mechanic == 18 || mechanic == 21) {
      return AuraStackingRule::kExclusive;
    }
  }

  return AuraStackingRule::kDefault;
}

bool AuraApplication::HasStackingConflict(
    const CGUnit_C& target,
    const data::dbc::SpellEntry& spell,
    const AuraApplicationRequest& request,
    const data::dbc::DbcLoader& dbc) const {

  for (std::size_t effect_index = 0;
       effect_index < spell.effect_apply_aura.size();
       ++effect_index) {
    if (spell.effect_apply_aura[effect_index] == 0) {
      continue;
    }

    const auto rule = ResolveStackingRule(spell, effect_index);
    if (rule == AuraStackingRule::kDefault) {
      continue;
    }

    const auto new_aura_type = spell.effect_apply_aura[effect_index];

    for (const auto& existing : target.Auras().All()) {
      if (existing.spell_id == 0 || existing.spell_id == request.spell_id) {
        continue;
      }

      const auto* existing_spell = dbc.spell().LookupEntry(existing.spell_id);
      if (existing_spell == nullptr) {
        continue;
      }

      for (std::size_t ei = 0;
           ei < existing_spell->effect_apply_aura.size();
           ++ei) {
        if (existing_spell->effect_apply_aura[ei] != new_aura_type) {
          continue;
        }

        switch (rule) {
          case AuraStackingRule::kExclusive:

            return true;

          case AuraStackingRule::kExclusiveSameCaster:

            if (existing.caster_guid == request.caster_guid) {
              return true;
            }
            break;

          case AuraStackingRule::kExclusiveSameEffect:

            if (existing_spell->effect[ei] == spell.effect[effect_index]) {
              return true;
            }
            break;

          case AuraStackingRule::kCrowdControl:

            return true;

          default:
            break;
        }
      }
    }
  }

  return false;
}

AuraApplyResult AuraApplication::CanApplyAura(
    const CGUnit_C& target,
    const data::dbc::SpellEntry& spell,
    const AuraApplicationRequest& request,
    const data::dbc::DbcLoader& dbc,
    std::uint32_t* out_reason) const {

  if (target.Auras().HasSpellId(request.spell_id)) {

    if (spell.stack_amount > 0) {

      const auto* existing = target.Auras().FindBySpellId(request.spell_id);
      if (existing != nullptr &&
          existing->stack_count >= static_cast<std::uint8_t>(spell.stack_amount)) {
        if (out_reason != nullptr) {
          *out_reason = 0;
        }
        return AuraApplyResult::kStackFull;
      }
      return AuraApplyResult::kSuccess;
    }

    if (out_reason != nullptr) {
      *out_reason = 0;
    }
    return AuraApplyResult::kAlreadyApplied;
  }

  for (std::size_t i = 0; i < spell.effect_apply_aura.size(); ++i) {
    if (spell.effect_apply_aura[i] == 0) {
      continue;
    }

    if (IsUnitImmuneToAuraType(target, spell.effect_apply_aura[i])) {
      if (out_reason != nullptr) {
        *out_reason = 1;
      }
      return AuraApplyResult::kImmunity;
    }
  }

  if (HasStackingConflict(target, spell, request, dbc)) {
    if (out_reason != nullptr) {
      *out_reason = 2;
    }
    return AuraApplyResult::kExclusiveConflict;
  }

  if (IsHarmfulAura(spell)) {

    const auto caster_level = request.caster_guid.IsEmpty()
                                  ? 1
                                  : 80;
    (void)caster_level;

  }

  return AuraApplyResult::kSuccess;
}

AuraApplyResult AuraApplication::ApplyAura(
    WorldSession& session, CGUnit_C& unit,
    const data::dbc::SpellEntry& spell,
    const AuraApplicationRequest& request,
    const data::dbc::DbcLoader& dbc) {

  std::uint32_t fail_reason = 0;
  const auto can_apply = CanApplyAura(
      unit, spell, request, dbc, &fail_reason);
  if (can_apply != AuraApplyResult::kSuccess) {
    return can_apply;
  }

  if (unit.Auras().HasSpellId(request.spell_id)) {

    if (spell.stack_amount == 0) {
      return AuraApplyResult::kAlreadyApplied;
    }
    return AuraApplyResult::kSuccess;
  }

  for (std::size_t i = 0; i < spell.effect_apply_aura.size(); ++i) {
    if (spell.effect_apply_aura[i] == 0) {
      continue;
    }

    const auto base_points = (static_cast<std::size_t>(i) < request.effect_amounts.size())
                                 ? request.effect_amounts[i]
                                 : spell.effect_base_points[i];
    ApplyEffectAura(session, unit, spell, i, base_points);
  }

  PendingAuraChange change;
  change.spell_id = request.spell_id;
  change.target_guid = unit.GetGuid();
  change.applied = true;
  pending_changes_.push_back(change);

  return AuraApplyResult::kSuccess;
}

bool AuraApplication::RemoveAura(CGUnit_C& unit,
                                  const std::uint32_t spell_id) {
  const auto* aura = unit.Auras().FindBySpellId(spell_id);
  if (aura == nullptr) {
    return false;
  }

  const auto* const dbc = unit.dbc_loader();
  const auto* const spell =
      dbc != nullptr ? dbc->spell().LookupEntry(spell_id) : nullptr;

  if (spell != nullptr) {
    for (std::size_t i = 0; i < spell->effect_apply_aura.size(); ++i) {
      if (spell->effect_apply_aura[i] == 0) {
        continue;
      }
      RemoveEffectAura(unit, *spell, i);
    }
  }

  PendingAuraChange change;
  change.spell_id = spell_id;
  change.target_guid = unit.GetGuid();
  change.applied = false;
  pending_changes_.push_back(change);

  return true;
}

bool AuraApplication::IsUnitImmuneToAuraType(
    const CGUnit_C& unit,
    const std::uint32_t aura_type) {

  for (const auto& a : unit.Auras().All()) {
    if (a.spell_id == 0) {
      continue;
    }

    (void)aura_type;
  }

  return false;
}

bool AuraApplication::IsHarmfulAura(const data::dbc::SpellEntry& spell) {

  if ((spell.targets & 0x80u) != 0) {
    return true;
  }
  if ((spell.targets & 0x100u) != 0) {
    return false;
  }

  for (std::size_t i = 0; i < spell.effect_implicit_target_a.size(); ++i) {
    const auto target_a = spell.effect_implicit_target_a[i];
    const auto target_b = spell.effect_implicit_target_b[i];

    if (target_a == 2 || target_a == 6 || target_a == 15 ||
        target_a == 16 || target_a == 24 || target_a == 28 ||
        target_a == 53 || target_a == 54 || target_a == 93) {
      return true;
    }
    if (target_b == 2 || target_b == 6 || target_b == 15 ||
        target_b == 16 || target_b == 24 || target_b == 28 ||
        target_b == 53 || target_b == 54 || target_b == 93) {
      return true;
    }
  }

  return false;
}

std::int32_t AuraApplication::GetSpellDuration(
    const data::dbc::SpellEntry& spell,
    const data::dbc::DbcLoader& dbc,
    const std::uint32_t caster_level) {
  if (spell.duration_index == 0) {
    return -1;
  }

  const auto* dur_entry = dbc.spell_duration().LookupEntry(spell.duration_index);
  if (dur_entry == nullptr) {
    return -1;
  }

  if (dur_entry->duration < 0) {
    return -1;
  }

  auto duration = dur_entry->duration;
  if (dur_entry->duration_per_level > 0 && caster_level > 0) {
    duration += static_cast<std::int32_t>(caster_level) *
                dur_entry->duration_per_level;
  }

  if (dur_entry->max_duration > 0 && duration > dur_entry->max_duration) {
    duration = dur_entry->max_duration;
  }

  return (duration < 0) ? 0 : duration;
}

void AuraApplication::ApplyEffectAura(
    WorldSession& session, CGUnit_C& unit,
    const data::dbc::SpellEntry& spell,
    const std::size_t effect_index,
    const std::int32_t base_points) {

  if (effect_index >= spell.effect_apply_aura.size()) {
    return;
  }

  const auto aura_type = spell.effect_apply_aura[effect_index];
  const auto misc_value = (effect_index < spell.effect_misc_value.size())
                              ? spell.effect_misc_value[effect_index]
                              : 0;

  switch (aura_type) {
    case kAuraTypeModShapeshift: {

      unit.Auras().SetShapeShiftForm(static_cast<std::uint32_t>(misc_value));
      break;
    }
    case kAuraTypeModStealth: {
      unit.Auras().SetStealthDetect(static_cast<std::uint8_t>(base_points));
      break;
    }
    case kAuraTypeModInvisibility: {
      unit.Auras().SetInvisibilityDetect(static_cast<std::uint8_t>(base_points));
      break;
    }
    case kAuraTypePeriodicTriggerSpell:
    case data::dbc::kSpellAuraPeriodicTriggerSpellFromClient: {

      unit.Auras().ReschedulePeriodicClientAuras(unit, session);
      break;
    }
    case kAuraTypeModProcTrigger: {

      const auto spell_id = spell.id;
      if (effect_index < spell.effect_trigger_spell.size() &&
          spell.effect_trigger_spell[effect_index] != 0) {
        ProcTriggerDescriptor desc;
        desc.source_spell_id = spell_id;
        desc.trigger_flags = static_cast<ProcTriggerFlag>(spell.proc_flags);
        desc.proc_chance = spell.proc_chance;
        desc.proc_charges = spell.proc_charges;
        desc.triggered_spell_id = spell.effect_trigger_spell[effect_index];
        desc.source = ProcSource::kSpell;
        if (spell.attributes_ex4 & 0x1000000u) {
          desc.icd_duration_ms = 1;
        }
        ProcManager::Get().RegisterSpellProc(
            unit.GetGuid().GetRawValue(), desc);
      }
      break;
    }
    default:
      break;
  }

  (void)base_points;
}

void AuraApplication::RemoveEffectAura(
    CGUnit_C& unit,
    const data::dbc::SpellEntry& spell,
    const std::size_t effect_index) {
  if (effect_index >= spell.effect_apply_aura.size()) {
    return;
  }

  const auto aura_type = spell.effect_apply_aura[effect_index];
  const auto misc_value = (effect_index < spell.effect_misc_value.size())
                              ? spell.effect_misc_value[effect_index]
                              : 0;

  switch (aura_type) {
    case kAuraTypeModShapeshift: {
      unit.Auras().SetShapeShiftForm(0);
      break;
    }
    case kAuraTypeModStealth: {
      unit.Auras().SetStealthDetect(0);
      break;
    }
    case kAuraTypeModInvisibility: {
      unit.Auras().SetInvisibilityDetect(0);
      break;
    }
    case kAuraTypeModProcTrigger: {
      ProcManager::Get().UnregisterSpellProcs(
          unit.GetGuid().GetRawValue(), spell.id);
      break;
    }
    default:
      break;
  }

  (void)misc_value;
}

void AuraApplication::Clear() {
  pending_changes_.clear();
}

}

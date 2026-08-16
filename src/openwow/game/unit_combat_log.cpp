
#include "openwow/game/unit_combat_log.h"

#include "openwow/game/combat_log_internal.h"
#include "openwow/game/combat_tracker.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/spell_query_bridge.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace openwow::game {

UnitCombatLog& UnitCombatLog::Get() {
  static UnitCombatLog instance;
  return instance;
}

SimThreatEntry* UnitCombatLog::FindEntry(const ObjectGuid& unit_guid,
                                          const ObjectGuid& target_guid) {
  auto it = threat_tables_.find(target_guid.GetRawValue());
  if (it == threat_tables_.end()) return nullptr;

  for (auto& entry : it->second) {
    if (entry.unit_guid == unit_guid) {
      return &entry;
    }
  }
  return nullptr;
}

const SimThreatEntry* UnitCombatLog::FindEntry(
    const ObjectGuid& unit_guid,
    const ObjectGuid& target_guid) const {
  auto it = threat_tables_.find(target_guid.GetRawValue());
  if (it == threat_tables_.end()) return nullptr;

  for (const auto& entry : it->second) {
    if (entry.unit_guid == unit_guid) {
      return &entry;
    }
  }
  return nullptr;
}

SimThreatEntry& UnitCombatLog::FindOrCreateEntry(
    const ObjectGuid& unit_guid,
    const ObjectGuid& target_guid) {
  auto* existing = FindEntry(unit_guid, target_guid);
  if (existing) return *existing;

  auto& vec = threat_tables_[target_guid.GetRawValue()];
  vec.emplace_back();
  auto& entry = vec.back();
  entry.unit_guid = unit_guid;
  entry.target_guid = target_guid;
  entry.last_update_time = game_time_;
  return entry;
}

float UnitCombatLog::ApplyModifiers(float base_threat,
                                     std::uint32_t school_mask) const {
  float modified = base_threat;

  for (const auto& [id, mod] : threat_modifiers_) {
    if (!mod.is_active) continue;

    if (!mod.applies_to_all && mod.school_mask != 0 &&
        (school_mask & mod.school_mask) == 0) {
      continue;
    }
    modified *= mod.multiplier;
    if (mod.additive != 0.0f) {
      modified += mod.additive * (base_threat > 0.0f ? base_threat : 1.0f);
    }
  }

  return modified;
}

void UnitCombatLog::PopulateSourceDest(CombatLogEntry& entry,
                                        const ObjectGuid& source,
                                        const ObjectGuid& dest) const {
  entry.source_guid = source.GetRawValue();
  entry.dest_guid = dest.GetRawValue();

  if (!source.IsEmpty()) {
    entry.source_name = CombatLog_BuildNameForGUID(entry.source_guid);
    entry.source_flags = CombatLog_BuildUnitFlags(entry.source_guid);
  }
  if (!dest.IsEmpty()) {
    entry.dest_name = CombatLog_BuildNameForGUID(entry.dest_guid);
    entry.dest_flags = CombatLog_BuildUnitFlags(entry.dest_guid);
  }
}

void UnitCombatLog::AddThreat(const ObjectGuid& unit_guid,
                               const ObjectGuid& target_guid,
                               float base_threat,
                               std::uint32_t school_mask) {
  if (unit_guid.IsEmpty() || target_guid.IsEmpty()) return;
  if (base_threat <= 0.0f) return;

  std::lock_guard lock(mutex_);

  auto& entry = FindOrCreateEntry(unit_guid, target_guid);
  entry.base_threat += base_threat;
  entry.last_update_time = game_time_;

  float stance_mult = 1.0f;
  auto stance_it = stances_.find(unit_guid.GetRawValue());
  if (stance_it != stances_.end()) {
    stance_mult = stance_it->second;
  }

  float total_mult = stance_mult;
  for (const auto& [id, mod] : threat_modifiers_) {
    if (!mod.is_active) continue;
    if (!mod.applies_to_all && mod.school_mask != 0 &&
        (school_mask & mod.school_mask) == 0) {
      continue;
    }
    total_mult *= mod.multiplier;
  }

  float modified = base_threat * total_mult;
  entry.modified_threat += modified;
  entry.threat_value += modified;
}

void UnitCombatLog::RemoveThreat(const ObjectGuid& unit_guid,
                                  const ObjectGuid& target_guid) {
  if (unit_guid.IsEmpty() || target_guid.IsEmpty()) return;

  std::lock_guard lock(mutex_);

  auto it = threat_tables_.find(target_guid.GetRawValue());
  if (it == threat_tables_.end()) return;

  auto& vec = it->second;
  vec.erase(
      std::remove_if(vec.begin(), vec.end(),
                      [&](const SimThreatEntry& e) {
                        return e.unit_guid == unit_guid;
                      }),
      vec.end());

  if (vec.empty()) {
    threat_tables_.erase(it);
  }
}

void UnitCombatLog::RemoveUnitFromAllThreats(const ObjectGuid& unit_guid) {
  if (unit_guid.IsEmpty()) return;

  std::lock_guard lock(mutex_);

  for (auto it = threat_tables_.begin(); it != threat_tables_.end(); ) {
    auto& vec = it->second;
    vec.erase(
        std::remove_if(vec.begin(), vec.end(),
                        [&](const SimThreatEntry& e) {
                          return e.unit_guid == unit_guid;
                        }),
        vec.end());

    if (vec.empty()) {
      it = threat_tables_.erase(it);
    } else {
      ++it;
    }
  }
}

void UnitCombatLog::ModifyThreat(const ObjectGuid& unit_guid,
                                  const ObjectGuid& target_guid,
                                  float delta) {
  if (unit_guid.IsEmpty() || target_guid.IsEmpty()) return;

  std::lock_guard lock(mutex_);

  auto* entry = FindEntry(unit_guid, target_guid);
  if (!entry) return;

  entry->threat_value += delta;
  entry->base_threat += delta;
  entry->modified_threat += delta;
  if (entry->threat_value < 0.0f) {
    entry->threat_value = 0.0f;
    entry->base_threat = 0.0f;
    entry->modified_threat = 0.0f;
  }
  entry->last_update_time = game_time_;
}

float UnitCombatLog::GetThreat(const ObjectGuid& unit_guid,
                                const ObjectGuid& target_guid) const {
  if (unit_guid.IsEmpty() || target_guid.IsEmpty()) return 0.0f;

  std::lock_guard lock(mutex_);

  const auto* entry = FindEntry(unit_guid, target_guid);
  return entry ? entry->threat_value : 0.0f;
}

float UnitCombatLog::GetThreatPercent(const ObjectGuid& unit_guid,
                                       const ObjectGuid& target_guid) const {
  if (unit_guid.IsEmpty() || target_guid.IsEmpty()) return 0.0f;

  std::lock_guard lock(mutex_);

  const auto* my_entry = FindEntry(unit_guid, target_guid);
  if (!my_entry) return 0.0f;

  float highest = 0.0f;
  auto it = threat_tables_.find(target_guid.GetRawValue());
  if (it != threat_tables_.end()) {
    for (const auto& e : it->second) {
      if (e.threat_value > highest) highest = e.threat_value;
    }
  }

  if (highest <= 0.0f) return 0.0f;
  return (my_entry->threat_value / highest) * 100.0f;
}

ObjectGuid UnitCombatLog::GetHighestThreatUnit(
    const ObjectGuid& target_guid) const {
  std::lock_guard lock(mutex_);

  auto it = threat_tables_.find(target_guid.GetRawValue());
  if (it == threat_tables_.end() || it->second.empty()) return ObjectGuid{};

  const SimThreatEntry* best = nullptr;
  for (const auto& e : it->second) {
    if (!best || e.threat_value > best->threat_value) {
      best = &e;
    }
  }
  return best ? best->unit_guid : ObjectGuid{};
}

float UnitCombatLog::GetHighestThreatValue(
    const ObjectGuid& target_guid) const {
  std::lock_guard lock(mutex_);

  auto it = threat_tables_.find(target_guid.GetRawValue());
  if (it == threat_tables_.end() || it->second.empty()) return 0.0f;

  float highest = 0.0f;
  for (const auto& e : it->second) {
    if (e.threat_value > highest) highest = e.threat_value;
  }
  return highest;
}

bool UnitCombatLog::IsHighestThreat(const ObjectGuid& unit_guid,
                                     const ObjectGuid& target_guid) const {
  ObjectGuid highest = GetHighestThreatUnit(target_guid);
  return !highest.IsEmpty() && highest == unit_guid;
}

std::vector<SimThreatEntry> UnitCombatLog::GetThreatList(
    const ObjectGuid& target_guid) const {
  std::lock_guard lock(mutex_);

  auto it = threat_tables_.find(target_guid.GetRawValue());
  if (it == threat_tables_.end()) return {};

  return it->second;
}

void UnitCombatLog::ClearThreatTable(const ObjectGuid& target_guid) {
  std::lock_guard lock(mutex_);
  threat_tables_.erase(target_guid.GetRawValue());
}

void UnitCombatLog::ClearAllThreatTables() {
  std::lock_guard lock(mutex_);
  threat_tables_.clear();
}

std::uint32_t UnitCombatLog::AddThreatModifier(ThreatModifier modifier) {
  std::lock_guard lock(mutex_);
  std::uint32_t id = next_modifier_id_++;
  threat_modifiers_[id] = std::move(modifier);
  return id;
}

void UnitCombatLog::RemoveThreatModifier(std::uint32_t modifier_id) {
  std::lock_guard lock(mutex_);
  threat_modifiers_.erase(modifier_id);
}

void UnitCombatLog::RemoveThreatModifiersBySpell(std::uint32_t spell_id) {
  std::lock_guard lock(mutex_);
  for (auto it = threat_modifiers_.begin(); it != threat_modifiers_.end(); ) {
    if (it->second.spell_id == spell_id) {
      it = threat_modifiers_.erase(it);
    } else {
      ++it;
    }
  }
}

void UnitCombatLog::ClearThreatModifiers() {
  std::lock_guard lock(mutex_);
  threat_modifiers_.clear();
}

void UnitCombatLog::SetStance(const ObjectGuid& unit_guid,
                               float stance_multiplier) {
  if (unit_guid.IsEmpty()) return;

  std::lock_guard lock(mutex_);
  stances_[unit_guid.GetRawValue()] = stance_multiplier;
}

void UnitCombatLog::ApplyTalentBonus(const ObjectGuid& unit_guid,
                                      float bonus_multiplier) {
  if (unit_guid.IsEmpty()) return;

  ThreatModifier mod;
  mod.source = ThreatModifierSource::Talent;
  mod.multiplier = 1.0f + bonus_multiplier;
  mod.is_active = true;

  std::lock_guard lock(mutex_);
  std::uint32_t id = next_modifier_id_++;
  threat_modifiers_[id] = std::move(mod);
}

float UnitCombatLog::GetEffectiveThreatMultiplier(
    const ObjectGuid& unit_guid, std::uint32_t school_mask) const {
  std::lock_guard lock(mutex_);

  float total = 1.0f;

  auto stance_it = stances_.find(unit_guid.GetRawValue());
  if (stance_it != stances_.end()) {
    total *= stance_it->second;
  }

  for (const auto& [id, mod] : threat_modifiers_) {
    if (!mod.is_active) continue;
    if (!mod.applies_to_all && mod.school_mask != 0 &&
        (school_mask & mod.school_mask) == 0) {
      continue;
    }
    total *= mod.multiplier;
  }

  return total;
}

void UnitCombatLog::UpdateThreatDecay(float delta_time, float game_time) {
  if (delta_time <= 0.0f) return;

  std::lock_guard lock(mutex_);

  for (auto& [target_key, entries] : threat_tables_) {
    for (auto& entry : entries) {
      if (entry.threat_value <= 0.0f) continue;

      if (entry.is_taunted) continue;

      float decay_per_second = 0.0f;
      if (!entry.in_melee_range) {

        decay_per_second = ThreatDecayRate::kRangedDecay * decay_rate_;
      }

      if (decay_per_second > 0.0f) {
        float decay = entry.threat_value * decay_per_second * delta_time;
        entry.threat_value -= decay;
        entry.base_threat -= decay * (entry.base_threat /
            (entry.threat_value + decay > 0.0f ? entry.threat_value + decay
                                                : 1.0f));
        entry.modified_threat -= decay * (entry.modified_threat /
            (entry.threat_value + decay > 0.0f ? entry.threat_value + decay
                                                : 1.0f));

        if (entry.threat_value < 0.0f) {
          entry.threat_value = 0.0f;
          entry.base_threat = 0.0f;
          entry.modified_threat = 0.0f;
        }
      }

      entry.last_update_time = game_time;
    }
  }
}

void UnitCombatLog::SetInMeleeRange(const ObjectGuid& unit_guid,
                                     const ObjectGuid& target_guid,
                                     bool in_melee) {
  std::lock_guard lock(mutex_);

  auto* entry = FindEntry(unit_guid, target_guid);
  if (entry) {
    entry->in_melee_range = in_melee;
  }
}

void UnitCombatLog::ApplyTaunt(const ObjectGuid& unit_guid,
                                const ObjectGuid& target_guid,
                                float duration_seconds,
                                float bonus) {
  if (unit_guid.IsEmpty() || target_guid.IsEmpty()) return;

  std::lock_guard lock(mutex_);

  float highest_threat = 0.0f;
  ObjectGuid highest_unit;
  auto it = threat_tables_.find(target_guid.GetRawValue());
  if (it != threat_tables_.end()) {
    for (const auto& e : it->second) {
      if (e.threat_value > highest_threat) {
        highest_threat = e.threat_value;
        highest_unit = e.unit_guid;
      }
    }
  }

  auto& entry = FindOrCreateEntry(unit_guid, target_guid);
  float taunt_threat = highest_threat + bonus;
  entry.threat_value = taunt_threat;
  entry.base_threat = taunt_threat;
  entry.modified_threat = taunt_threat;
  entry.is_taunted = true;
  entry.last_update_time = game_time_;

  std::uint64_t taunt_key =
      (static_cast<std::uint64_t>(unit_guid.GetRawValue()) << 32) |
      target_guid.GetRawValue();
  taunt_expirations_[taunt_key] = game_time_ + duration_seconds;
}

void UnitCombatLog::RemoveTaunt(const ObjectGuid& unit_guid,
                                 const ObjectGuid& target_guid) {
  std::uint64_t taunt_key =
      (static_cast<std::uint64_t>(unit_guid.GetRawValue()) << 32) |
      target_guid.GetRawValue();
  taunt_expirations_.erase(taunt_key);

  std::lock_guard lock(mutex_);
  auto* entry = FindEntry(unit_guid, target_guid);
  if (entry) {
    entry->is_taunted = false;
  }
}

bool UnitCombatLog::IsTaunting(const ObjectGuid& unit_guid,
                                const ObjectGuid& target_guid) const {
  std::uint64_t taunt_key =
      (static_cast<std::uint64_t>(unit_guid.GetRawValue()) << 32) |
      target_guid.GetRawValue();
  auto it = taunt_expirations_.find(taunt_key);
  return it != taunt_expirations_.end() && it->second > game_time_;
}

CombatLogEntry UnitCombatLog::BuildSpellDamageEntry(
    const RawDamageEvent& event) const {
  CombatLogEntry entry;
  entry.type = CombatLogEventType::SPELL_DAMAGE;
  PopulateSourceDest(entry, event.source, event.target);

  entry.spell_id = event.spell_id;
  entry.spell_name = event.spell_name;
  entry.spell_school = event.school_mask;

  entry.amount = event.amount;
  entry.overkill = event.overkill;
  entry.school = event.school_mask;
  entry.resisted = event.resisted;
  entry.blocked = event.blocked;
  entry.absorbed = event.absorbed;
  entry.critical = event.critical;
  entry.glancing = event.glancing;
  entry.crushing = event.crushing;

  return entry;
}

CombatLogEntry UnitCombatLog::BuildSpellMissEntry(
    const RawMissEvent& event) const {
  CombatLogEntry entry;
  entry.type = CombatLogEventType::SPELL_MISSED;
  PopulateSourceDest(entry, event.source, event.target);

  entry.spell_id = event.spell_id;
  entry.spell_name = event.spell_name;
  entry.miss_type = event.miss_type;

  return entry;
}

CombatLogEntry UnitCombatLog::BuildSpellHealEntry(
    const RawHealEvent& event) const {
  CombatLogEntry entry;
  entry.type = CombatLogEventType::SPELL_HEAL;
  PopulateSourceDest(entry, event.source, event.target);

  entry.spell_id = event.spell_id;
  entry.spell_name = event.spell_name;
  entry.amount = event.amount;
  entry.overheal = event.overheal;
  entry.absorbed = event.absorbed;
  entry.critical = event.critical;

  return entry;
}

CombatLogEntry UnitCombatLog::BuildSpellEnergizeEntry(
    const RawEnergizeEvent& event) const {
  CombatLogEntry entry;
  entry.type = CombatLogEventType::SPELL_ENERGIZE;
  PopulateSourceDest(entry, event.source, event.target);

  entry.spell_id = event.spell_id;
  entry.spell_name = event.spell_name;
  entry.power_amount = event.amount;
  entry.power_type = static_cast<std::int32_t>(event.power_type);

  return entry;
}

CombatLogEntry UnitCombatLog::BuildSwingDamageEntry(
    const RawDamageEvent& event) const {
  CombatLogEntry entry;
  entry.type = CombatLogEventType::SWING_DAMAGE;
  PopulateSourceDest(entry, event.source, event.target);

  entry.amount = event.amount;
  entry.overkill = event.overkill;
  entry.school = event.school_mask;
  entry.resisted = event.resisted;
  entry.blocked = event.blocked;
  entry.absorbed = event.absorbed;
  entry.critical = event.critical;
  entry.glancing = event.glancing;
  entry.crushing = event.crushing;

  return entry;
}

CombatLogEntry UnitCombatLog::BuildSwingMissEntry(
    const RawMissEvent& event) const {
  CombatLogEntry entry;
  entry.type = CombatLogEventType::SWING_MISSED;
  PopulateSourceDest(entry, event.source, event.target);

  entry.miss_type = event.miss_type;

  return entry;
}

CombatLogEntry UnitCombatLog::BuildRangeDamageEntry(
    const RawDamageEvent& event) const {
  CombatLogEntry entry;
  entry.type = CombatLogEventType::RANGE_DAMAGE;
  PopulateSourceDest(entry, event.source, event.target);

  entry.spell_id = event.spell_id;
  entry.spell_name = event.spell_name;
  entry.spell_school = event.school_mask;

  entry.amount = event.amount;
  entry.overkill = event.overkill;
  entry.school = event.school_mask;
  entry.resisted = event.resisted;
  entry.blocked = event.blocked;
  entry.absorbed = event.absorbed;
  entry.critical = event.critical;
  entry.glancing = event.glancing;
  entry.crushing = event.crushing;

  return entry;
}

CombatLogEntry UnitCombatLog::BuildRangeMissEntry(
    const RawMissEvent& event) const {
  CombatLogEntry entry;
  entry.type = CombatLogEventType::RANGE_MISSED;
  PopulateSourceDest(entry, event.source, event.target);

  entry.spell_id = event.spell_id;
  entry.spell_name = event.spell_name;
  entry.miss_type = event.miss_type;

  return entry;
}

CombatLogEntry UnitCombatLog::BuildPeriodicDamageEntry(
    const RawDamageEvent& event) const {
  CombatLogEntry entry;
  entry.type = CombatLogEventType::SPELL_PERIODIC_DAMAGE;
  PopulateSourceDest(entry, event.source, event.target);

  entry.spell_id = event.spell_id;
  entry.spell_name = event.spell_name;
  entry.spell_school = event.school_mask;

  entry.amount = event.amount;
  entry.overkill = event.overkill;
  entry.school = event.school_mask;
  entry.resisted = event.resisted;
  entry.blocked = event.blocked;
  entry.absorbed = event.absorbed;
  entry.critical = event.critical;
  entry.glancing = event.glancing;
  entry.crushing = event.crushing;

  return entry;
}

CombatLogEntry UnitCombatLog::BuildPeriodicHealEntry(
    const RawHealEvent& event) const {
  CombatLogEntry entry;
  entry.type = CombatLogEventType::SPELL_PERIODIC_HEAL;
  PopulateSourceDest(entry, event.source, event.target);

  entry.spell_id = event.spell_id;
  entry.spell_name = event.spell_name;
  entry.amount = event.amount;
  entry.overheal = event.overheal;
  entry.absorbed = event.absorbed;
  entry.critical = event.critical;

  return entry;
}

CombatLogEntry UnitCombatLog::BuildPeriodicEnergizeEntry(
    const RawEnergizeEvent& event) const {
  CombatLogEntry entry;
  entry.type = CombatLogEventType::SPELL_PERIODIC_ENERGIZE;
  PopulateSourceDest(entry, event.source, event.target);

  entry.spell_id = event.spell_id;
  entry.spell_name = event.spell_name;
  entry.power_amount = event.amount;
  entry.power_type = static_cast<std::int32_t>(event.power_type);

  return entry;
}

CombatLogEntry UnitCombatLog::BuildPeriodicMissEntry(
    const RawMissEvent& event) const {
  CombatLogEntry entry;
  entry.type = CombatLogEventType::SPELL_PERIODIC_MISSED;
  PopulateSourceDest(entry, event.source, event.target);

  entry.spell_id = event.spell_id;
  entry.spell_name = event.spell_name;
  entry.miss_type = event.miss_type;

  return entry;
}

CombatLogEntry UnitCombatLog::BuildDamageShieldEntry(
    const RawDamageShieldEvent& event) const {
  CombatLogEntry entry;
  entry.type = CombatLogEventType::DAMAGE_SHIELD;

  PopulateSourceDest(entry, event.caster, event.attacker);

  entry.spell_id = event.spell_id;
  entry.spell_name = event.spell_name;

  entry.amount = event.damage;
  entry.overkill = 0;
  entry.school = 0;
  entry.resisted = event.resisted;
  entry.blocked = 0;
  entry.absorbed = event.absorbed;
  entry.critical = false;
  entry.glancing = false;
  entry.crushing = false;

  return entry;
}

CombatLogEntry UnitCombatLog::BuildDamageSplitEntry(
    const RawDamageSplitEvent& event) const {
  CombatLogEntry entry;
  entry.type = CombatLogEventType::DAMAGE_SPLIT;
  PopulateSourceDest(entry, event.source, event.target);

  entry.spell_id = event.spell_id;
  entry.spell_name = event.spell_name;
  entry.spell_school = event.school_mask;

  entry.amount = event.damage;
  entry.overkill = event.overkill;
  entry.school = event.school_mask;
  entry.resisted = event.resisted;
  entry.blocked = event.blocked;
  entry.absorbed = event.absorbed;
  entry.critical = event.critical;
  entry.glancing = false;
  entry.crushing = false;

  return entry;
}

CombatLogEntry UnitCombatLog::BuildEnvironmentalDamageEntry(
    const RawEnvironmentalEvent& event) const {
  CombatLogEntry entry;
  entry.type = CombatLogEventType::ENVIRONMENTAL_DAMAGE;

  PopulateSourceDest(entry, ObjectGuid{}, event.target);

  const char* env_name = CombatLog_GetEnvironmentalDamageTypeName(event.env_type);
  entry.env_type = env_name ? env_name : "";

  const std::uint32_t school_mask =
      CombatLog_GetEnvironmentalDamageSchoolMask(event.env_type);

  entry.amount = event.amount;
  entry.overkill = 0;
  entry.school = school_mask;
  entry.resisted = event.resisted;
  entry.blocked = 0;
  entry.absorbed = event.absorbed;
  entry.critical = false;

  return entry;
}

void UnitCombatLog::AppendSpellDamage(CombatLog& log,
                                       const RawDamageEvent& event) {
  auto entry = BuildSpellDamageEntry(event);
  CombatLog_FinalizeEntry(log, entry);
}

void UnitCombatLog::AppendSpellMiss(CombatLog& log,
                                     const RawMissEvent& event) {
  auto entry = BuildSpellMissEntry(event);
  CombatLog_FinalizeEntry(log, entry);
}

void UnitCombatLog::AppendSpellHeal(CombatLog& log,
                                     const RawHealEvent& event) {
  auto entry = BuildSpellHealEntry(event);
  CombatLog_FinalizeEntry(log, entry);
}

void UnitCombatLog::AppendSpellEnergize(CombatLog& log,
                                         const RawEnergizeEvent& event) {
  auto entry = BuildSpellEnergizeEntry(event);
  CombatLog_FinalizeEntry(log, entry);
}

void UnitCombatLog::AppendSwingDamage(CombatLog& log,
                                       const RawDamageEvent& event) {
  auto entry = BuildSwingDamageEntry(event);
  CombatLog_FinalizeEntry(log, entry);
}

void UnitCombatLog::AppendSwingMiss(CombatLog& log,
                                     const RawMissEvent& event) {
  auto entry = BuildSwingMissEntry(event);
  CombatLog_FinalizeEntry(log, entry);
}

void UnitCombatLog::AppendRangeDamage(CombatLog& log,
                                       const RawDamageEvent& event) {
  auto entry = BuildRangeDamageEntry(event);
  CombatLog_FinalizeEntry(log, entry);
}

void UnitCombatLog::AppendRangeMiss(CombatLog& log,
                                     const RawMissEvent& event) {
  auto entry = BuildRangeMissEntry(event);
  CombatLog_FinalizeEntry(log, entry);
}

void UnitCombatLog::AppendPeriodicDamage(CombatLog& log,
                                          const RawDamageEvent& event) {
  auto entry = BuildPeriodicDamageEntry(event);
  CombatLog_FinalizeEntry(log, entry);
}

void UnitCombatLog::AppendPeriodicHeal(CombatLog& log,
                                        const RawHealEvent& event) {
  auto entry = BuildPeriodicHealEntry(event);
  CombatLog_FinalizeEntry(log, entry);
}

void UnitCombatLog::AppendPeriodicEnergize(CombatLog& log,
                                            const RawEnergizeEvent& event) {
  auto entry = BuildPeriodicEnergizeEntry(event);
  CombatLog_FinalizeEntry(log, entry);
}

void UnitCombatLog::AppendPeriodicMiss(CombatLog& log,
                                        const RawMissEvent& event) {
  auto entry = BuildPeriodicMissEntry(event);
  CombatLog_FinalizeEntry(log, entry);
}

void UnitCombatLog::AppendDamageShield(CombatLog& log,
                                        const RawDamageShieldEvent& event) {
  auto entry = BuildDamageShieldEntry(event);
  CombatLog_FinalizeEntry(log, entry);
}

void UnitCombatLog::AppendDamageSplit(CombatLog& log,
                                       const RawDamageSplitEvent& event) {
  auto entry = BuildDamageSplitEntry(event);
  CombatLog_FinalizeEntry(log, entry);
}

void UnitCombatLog::AppendEnvironmentalDamage(
    CombatLog& log, const RawEnvironmentalEvent& event) {
  auto entry = BuildEnvironmentalDamageEntry(event);
  CombatLog_FinalizeEntry(log, entry);
}

void UnitCombatLog::EnterCombat(const ObjectGuid& unit_guid) {
  if (unit_guid.IsEmpty()) return;

  CombatTracker::Get().SetInCombat(true);

  std::lock_guard lock(mutex_);

  for (auto& [target_key, entries] : threat_tables_) {
    for (auto& entry : entries) {
      if (entry.unit_guid == unit_guid) {
        entry.last_update_time = game_time_;
      }
    }
  }
}

void UnitCombatLog::LeaveCombat(const ObjectGuid& unit_guid) {
  if (unit_guid.IsEmpty()) return;

  CombatTracker::Get().SetInCombat(false);

  std::lock_guard lock(mutex_);

  for (auto it = threat_tables_.begin(); it != threat_tables_.end(); ) {
    auto& entries = it->second;
    entries.erase(
        std::remove_if(entries.begin(), entries.end(),
                        [&](const SimThreatEntry& e) {
                          return e.unit_guid == unit_guid;
                        }),
        entries.end());
    if (entries.empty()) {
      it = threat_tables_.erase(it);
    } else {
      ++it;
    }
  }
}

bool UnitCombatLog::IsInCombat(const ObjectGuid& unit_guid) const {
  (void)unit_guid;
  return CombatTracker::Get().IsInCombat();
}

void UnitCombatLog::Update(float delta_time, float game_time) {
  if (delta_time <= 0.0f) return;

  game_time_ = game_time;

  UpdateThreatDecay(delta_time, game_time);

  std::lock_guard lock(mutex_);
  for (auto it = taunt_expirations_.begin();
       it != taunt_expirations_.end();) {
    if (it->second <= game_time) {

      std::uint32_t unit_lo = static_cast<std::uint32_t>(it->first);
      std::uint32_t unit_hi = static_cast<std::uint32_t>(it->first >> 32);
      ObjectGuid unit_guid(
          (static_cast<std::uint64_t>(unit_hi) << 32) | unit_lo);
      ObjectGuid target_guid(
          static_cast<std::uint64_t>(it->first & 0xFFFFFFFF));

      auto* entry = FindEntry(unit_guid, target_guid);
      if (entry) {
        entry->is_taunted = false;
      }
      it = taunt_expirations_.erase(it);
    } else {
      ++it;
    }
  }
}

void UnitCombatLog::Reset() {
  std::lock_guard lock(mutex_);
  threat_tables_.clear();
  threat_modifiers_.clear();
  stances_.clear();
  taunt_expirations_.clear();
  next_modifier_id_ = 1;
  game_time_ = 0.0f;
  decay_rate_ = 1.0f;
}

std::size_t UnitCombatLog::GetEntryCount() const {
  std::lock_guard lock(mutex_);
  std::size_t count = 0;
  for (const auto& [key, entries] : threat_tables_) {
    count += entries.size();
  }
  return count;
}

}

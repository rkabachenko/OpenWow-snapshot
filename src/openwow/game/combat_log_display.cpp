
#include "openwow/game/combat_log_display.h"

#include "openwow/game/nameplate_damage_flash.h"
#include "openwow/ui/game/script_event_dispatch.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <sstream>

namespace openwow::game {

namespace {

bool IsDamageType(CombatLogDisplayEventType t) {
  return t == CombatLogDisplayEventType::SpellDamage ||
         t == CombatLogDisplayEventType::MeleeDamage ||
         t == CombatLogDisplayEventType::EnvironmentalDamage;
}

bool IsHealType(CombatLogDisplayEventType t) {
  return t == CombatLogDisplayEventType::SpellHeal;
}

constexpr std::array<const char*, 9> kIndexedUnitCombatSchools{
    "MISS",
    "WOUND",
    "DODGE",
    "PARRY",
    "INTERRUPT",
    "BLOCK",
    "EVADE",
    "IMMUNE",
    "DEFLECT",
};

constexpr std::array<const char*, 12> kSpellMissUnitCombatSchools{
    "NONE",
    "MISS",
    "RESIST",
    "DODGE",
    "PARRY",
    "BLOCK",
    "EVADE",
    "IMMUNE",
    "IMMUNE",
    "DEFLECT",
    "ABSORB",
    "REFLECT",
};

const char* SelectHitDescriptor(int amount, int hit_flags) {
  if (amount > 0) {
    if ((hit_flags & HitFlags::kCritical) != 0) return "CRITICAL";
    if ((hit_flags & HitFlags::kGlancing) != 0) return "GLANCING";
    if ((hit_flags & HitFlags::kCrushing) != 0) return "CRUSHING";
  } else {
    if ((hit_flags & HitFlags::kAbsorb) != 0) return "ABSORB";
    if ((hit_flags & HitFlags::kBlock) != 0) return "BLOCK";
    if ((hit_flags & HitFlags::kResist) != 0) return "RESIST";
  }

  return "";
}

const char* LookupIndexedUnitCombatSchool(const IndexedUnitCombatSchool school) {
  const auto index = static_cast<std::size_t>(school);
  assert(index < kIndexedUnitCombatSchools.size());
  if (index >= kIndexedUnitCombatSchools.size()) {
    return "";
  }
  return kIndexedUnitCombatSchools[index];
}

const char* LookupSpellMissUnitCombatSchool(const std::uint8_t miss_info) {
  const auto index = static_cast<std::size_t>(miss_info);
  if (index >= kSpellMissUnitCombatSchools.size()) {
    return nullptr;
  }
  return kSpellMissUnitCombatSchools[index];
}

}

void CombatLogDisplay::AddEntry(CombatLogDisplayEntry entry) {
  entries_.push_back(std::move(entry));
  while (entries_.size() > maxEntries_)
    entries_.pop_front();
}

const std::deque<CombatLogDisplayEntry>& CombatLogDisplay::GetEntries() const {
  return entries_;
}

std::vector<const CombatLogDisplayEntry*> CombatLogDisplay::GetFilteredEntries(
    uint32_t filterFlags) const {
  std::vector<const CombatLogDisplayEntry*> result;
  for (const auto& e : entries_)
    if (PassesFilter(e, filterFlags))
      result.push_back(&e);
  return result;
}

void CombatLogDisplay::SetMaxEntries(size_t max) {
  maxEntries_ = (max == 0) ? 1 : max;
  while (entries_.size() > maxEntries_)
    entries_.pop_front();
}

size_t CombatLogDisplay::GetMaxEntries() const { return maxEntries_; }
size_t CombatLogDisplay::GetEntryCount() const { return entries_.size(); }

std::string CombatLogDisplay::FormatEntry(const CombatLogDisplayEntry& e) {
  std::ostringstream ss;
  switch (e.type) {
    case CombatLogDisplayEventType::SpellDamage:
      ss << e.sourceName << "'s " << e.spellName << " hit " << e.destName
         << " for " << e.amount << " " << e.school << " damage";
      if (e.isCritical) ss << " (Critical)";
      if (e.overkill > 0) ss << " (" << e.overkill << " overkill)";
      break;
    case CombatLogDisplayEventType::SpellHeal:
      ss << e.sourceName << "'s " << e.spellName << " healed " << e.destName
         << " for " << e.amount;
      if (e.isCritical) ss << " (Critical)";
      break;
    case CombatLogDisplayEventType::MeleeDamage:
      ss << e.sourceName << " hit " << e.destName << " for " << e.amount
         << " " << e.school << " damage";
      if (e.isCritical) ss << " (Critical)";
      break;
    case CombatLogDisplayEventType::SpellMiss:
      ss << e.sourceName << "'s " << e.spellName << " missed " << e.destName;
      break;
    case CombatLogDisplayEventType::SpellAuraApplied:
      ss << e.destName << " gains " << e.spellName;
      break;
    case CombatLogDisplayEventType::SpellAuraRemoved:
      ss << e.spellName << " fades from " << e.destName;
      break;
    case CombatLogDisplayEventType::SpellCast:
      ss << e.sourceName << " casts " << e.spellName;
      break;
    case CombatLogDisplayEventType::UnitDied:
      ss << e.destName << " dies";
      break;
    case CombatLogDisplayEventType::SpellEnergize:
      ss << e.sourceName << "'s " << e.spellName << " energizes "
         << e.destName << " for " << e.amount;
      break;
    case CombatLogDisplayEventType::SpellDrain:
      ss << e.sourceName << "'s " << e.spellName << " drains " << e.amount
         << " from " << e.destName;
      break;
    case CombatLogDisplayEventType::EnvironmentalDamage:
      ss << e.destName << " takes " << e.amount << " " << e.school
         << " damage from the environment";
      break;
    case CombatLogDisplayEventType::SpellInterrupt:
      ss << e.sourceName << " interrupts " << e.destName << "'s "
         << e.spellName;
      break;
    case CombatLogDisplayEventType::SpellDispel:
      ss << e.sourceName << " dispels " << e.spellName << " from "
         << e.destName;
      break;
  }
  return ss.str();
}

void CombatLogDisplay::SetActiveFilters(uint32_t flags) {
  activeFilters_ = flags;
}

uint32_t CombatLogDisplay::GetActiveFilters() const {
  return activeFilters_;
}

bool CombatLogDisplay::PassesFilter(const CombatLogDisplayEntry& e,
                                    uint32_t flags) const {
  using namespace CombatLogFilterFlag;
  switch (e.type) {
    case CombatLogDisplayEventType::SpellDamage:
      return (flags & ShowSpellDamage) != 0;
    case CombatLogDisplayEventType::MeleeDamage:
      return (flags & ShowMelee) != 0;
    case CombatLogDisplayEventType::SpellHeal:
      return (flags & ShowHealing) != 0;
    case CombatLogDisplayEventType::SpellAuraApplied:
    case CombatLogDisplayEventType::SpellAuraRemoved:
      return (flags & ShowAuras) != 0;
    case CombatLogDisplayEventType::SpellCast:
      return (flags & ShowCast) != 0;
    case CombatLogDisplayEventType::SpellMiss:
      return (flags & ShowSpellDamage) != 0;
    case CombatLogDisplayEventType::SpellEnergize:
    case CombatLogDisplayEventType::SpellDrain:
    case CombatLogDisplayEventType::UnitDied:
    case CombatLogDisplayEventType::EnvironmentalDamage:
    case CombatLogDisplayEventType::SpellInterrupt:
    case CombatLogDisplayEventType::SpellDispel:
      return (flags & ShowMisc) != 0;
  }
  return true;
}

int64_t CombatLogDisplay::GetDamageDealt() const {
  int64_t total = 0;
  for (const auto& e : entries_)
    if (IsDamageType(e.type)) total += e.amount;
  return total;
}

int64_t CombatLogDisplay::GetHealingDone() const {
  int64_t total = 0;
  for (const auto& e : entries_)
    if (IsHealType(e.type)) total += e.amount;
  return total;
}

float CombatLogDisplay::GetDPS(double windowSeconds) const {
  if (windowSeconds <= 0.0 || entries_.empty()) return 0.0f;
  double latest = entries_.back().timestamp;
  double cutoff = latest - windowSeconds;
  int64_t dmg   = 0;
  for (const auto& e : entries_) {
    if (e.timestamp >= cutoff && IsDamageType(e.type))
      dmg += e.amount;
  }
  return static_cast<float>(static_cast<double>(dmg) / windowSeconds);
}

float CombatLogDisplay::GetHPS(double windowSeconds) const {
  if (windowSeconds <= 0.0 || entries_.empty()) return 0.0f;
  double latest = entries_.back().timestamp;
  double cutoff = latest - windowSeconds;
  int64_t heal  = 0;
  for (const auto& e : entries_) {
    if (e.timestamp >= cutoff && IsHealType(e.type))
      heal += e.amount;
  }
  return static_cast<float>(static_cast<double>(heal) / windowSeconds);
}

void CombatLogDisplay::Clear() {
  entries_.clear();
}

int BuildUnitCombatHitFlags(const bool is_critical,
                            const bool has_absorb,
                            const bool has_resist,
                            const bool has_block) {
  int hit_flags = 0;
  if (is_critical) {
    hit_flags |= HitFlags::kCritical;
  }
  if (has_absorb) {
    hit_flags |= HitFlags::kAbsorb;
  }
  if (has_resist) {
    hit_flags |= HitFlags::kResist;
  }
  if (has_block) {
    hit_flags |= HitFlags::kBlock;
  }
  return hit_flags;
}

void DispatchHitIndicator(const ObjectGuid& unit_guid,
                          const char* damage_school,
                          int amount, int extra_amount, int hit_flags) {
  openwow::ui::game::ScriptEventDispatch::Get().FireUnitCombat(
      unit_guid.GetRawValue(),
      damage_school ? damage_school : "",
      SelectHitDescriptor(amount, hit_flags),
      amount,
      extra_amount);
}

void DispatchIndexedUnitCombatEvent(const ObjectGuid& unit_guid,
                                    const IndexedUnitCombatSchool school,
                                    const int amount,
                                    const int extra_amount,
                                    const int hit_flags) {
  DispatchHitIndicator(
      unit_guid,
      LookupIndexedUnitCombatSchool(school),
      amount,
      extra_amount,
      hit_flags);
}

void DispatchSpellMissUnitCombatEvent(const ObjectGuid& unit_guid,
                                      const std::uint8_t miss_info,
                                      const std::uint32_t armor_resistance_mask) {
  const char* const school = LookupSpellMissUnitCombatSchool(miss_info);
  if (school == nullptr) {
    return;
  }

  openwow::ui::game::ScriptEventDispatch::Get().FireUnitCombat(
      unit_guid.GetRawValue(), school, "", 0, static_cast<int>(armor_resistance_mask));
}

void DispatchWoundEvent(const ObjectGuid& unit_guid,
                        const int amount,
                        const int extra_amount,
                        const int hit_flags) {
  if (amount > 0) {
    NameplateDamageFlashState::Get().Trigger(unit_guid);
  }
  DispatchHitIndicator(unit_guid, "WOUND", amount, extra_amount, hit_flags);
}

void DispatchHealEvent(const ObjectGuid& unit_guid,
                       int heal_amount,
                       int absorb_amount,
                       bool is_crit) {
  if (heal_amount == 0) return;
  const int flags =
      BuildUnitCombatHitFlags(is_crit, false, false, false);
  DispatchHitIndicator(unit_guid, "HEAL", heal_amount, absorb_amount, flags);
}

}

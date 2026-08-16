
#include "openwow/game/aura_tracker.h"

#include <algorithm>

namespace openwow::game {

float AuraData::GetRemainingTime(std::uint32_t current_time) const {
  if (duration == 0) return 0.0f;
  if (current_time >= expiration) return 0.0f;
  return static_cast<float>(expiration - current_time);
}

bool AuraData::IsExpired(std::uint32_t current_time) const {
  if (duration == 0) return false;
  return current_time >= expiration;
}

bool AuraData::IsBuff() const {
  return AuraTracker::IsBuffSlot(slot);
}

bool AuraData::IsDebuff() const {
  return AuraTracker::IsDebuffSlot(slot);
}

bool AuraData::IsPassive() const {
  return AuraTracker::IsPassiveSlot(slot);
}

bool AuraData::IsHelpfulByOriginalFlags() const {
  if (has_raw_flags) {
    return (raw_flags & 0x80u) == 0;
  }

  return IsBuff();
}

bool AuraData::IsCancelableByOriginalFlags() const {
  if (has_raw_flags) {
    return (raw_flags & 0x10u) != 0;
  }

  return is_cancellable;
}

AuraTracker& AuraTracker::Get() {
  static AuraTracker instance;
  return instance;
}

void AuraTracker::SetAura(const ObjectGuid& unit, std::uint8_t slot,
                          const AuraData& aura) {
  if (aura.spell_id == 0) return;
  std::lock_guard lock(mutex_);
  auto& ua = unit_auras_[unit.GetRawValue()];
  ua.slots[slot] = aura;
  ua.slots[slot]->slot = slot;
}

void AuraTracker::RemoveAura(const ObjectGuid& unit, std::uint8_t slot) {
  std::lock_guard lock(mutex_);
  auto it = unit_auras_.find(unit.GetRawValue());
  if (it != unit_auras_.end()) {
    it->second.slots[slot].reset();
  }
}

void AuraTracker::ClearAuras(const ObjectGuid& unit) {
  std::lock_guard lock(mutex_);
  unit_auras_.erase(unit.GetRawValue());
}

void AuraTracker::SetAllAuras(
    const ObjectGuid& unit,
    const std::vector<std::pair<std::uint8_t, AuraData>>& auras) {
  std::lock_guard lock(mutex_);
  auto& ua = unit_auras_[unit.GetRawValue()];

  for (auto& s : ua.slots) s.reset();

  for (const auto& [slot, data] : auras) {
    if (data.spell_id == 0) continue;
    ua.slots[slot] = data;
    ua.slots[slot]->slot = slot;
  }
}

const AuraData* AuraTracker::GetAura(const ObjectGuid& unit,
                                     std::uint8_t slot) const {
  std::lock_guard lock(mutex_);
  auto it = unit_auras_.find(unit.GetRawValue());
  if (it == unit_auras_.end()) return nullptr;
  const auto& opt = it->second.slots[slot];
  return opt.has_value() ? &opt.value() : nullptr;
}

std::vector<const AuraData*> AuraTracker::GetBuffs(
    const ObjectGuid& unit) const {
  std::vector<const AuraData*> result;
  std::lock_guard lock(mutex_);
  auto it = unit_auras_.find(unit.GetRawValue());
  if (it == unit_auras_.end()) return result;

  for (std::uint16_t i = 0; i < kFirstDebuffSlot; ++i) {
    const auto& opt = it->second.slots[i];
    if (opt.has_value()) {
      result.push_back(&opt.value());
    }
  }
  return result;
}

std::vector<const AuraData*> AuraTracker::GetDebuffs(
    const ObjectGuid& unit) const {
  std::vector<const AuraData*> result;
  std::lock_guard lock(mutex_);
  auto it = unit_auras_.find(unit.GetRawValue());
  if (it == unit_auras_.end()) return result;

  for (std::uint16_t i = kFirstDebuffSlot; i < kFirstPassiveSlot; ++i) {
    const auto& opt = it->second.slots[i];
    if (opt.has_value()) {
      result.push_back(&opt.value());
    }
  }
  return result;
}

std::uint32_t AuraTracker::GetBuffCount(const ObjectGuid& unit) const {
  std::uint32_t count = 0;
  std::lock_guard lock(mutex_);
  auto it = unit_auras_.find(unit.GetRawValue());
  if (it == unit_auras_.end()) return 0;
  for (std::uint16_t i = 0; i < kFirstDebuffSlot; ++i) {
    if (it->second.slots[i].has_value()) ++count;
  }
  return count;
}

std::uint32_t AuraTracker::GetDebuffCount(const ObjectGuid& unit) const {
  std::uint32_t count = 0;
  std::lock_guard lock(mutex_);
  auto it = unit_auras_.find(unit.GetRawValue());
  if (it == unit_auras_.end()) return 0;
  for (std::uint16_t i = kFirstDebuffSlot; i < kFirstPassiveSlot; ++i) {
    if (it->second.slots[i].has_value()) ++count;
  }
  return count;
}

const AuraData* AuraTracker::FindAuraBySpell(const ObjectGuid& unit,
                                             std::uint32_t spell_id) const {
  std::lock_guard lock(mutex_);
  auto it = unit_auras_.find(unit.GetRawValue());
  if (it == unit_auras_.end()) return nullptr;
  for (const auto& opt : it->second.slots) {
    if (opt.has_value() && opt->spell_id == spell_id) {
      return &opt.value();
    }
  }
  return nullptr;
}

bool AuraTracker::HasAura(const ObjectGuid& unit,
                          std::uint32_t spell_id) const {
  return FindAuraBySpell(unit, spell_id) != nullptr;
}

void AuraTracker::ForEachAura(
    const ObjectGuid& unit,
    const std::function<void(std::uint8_t slot, const AuraData&)>& fn) const {
  std::lock_guard lock(mutex_);
  auto it = unit_auras_.find(unit.GetRawValue());
  if (it == unit_auras_.end()) return;

  for (std::uint16_t i = 0; i < kFirstPassiveSlot; ++i) {
    if (it->second.slots[i].has_value()) {
      fn(static_cast<std::uint8_t>(i), it->second.slots[i].value());
    }
  }
}

void AuraTracker::ForEachAuraAll(
    const ObjectGuid& unit,
    const std::function<void(std::uint8_t slot, const AuraData&)>& fn) const {
  std::lock_guard lock(mutex_);
  auto it = unit_auras_.find(unit.GetRawValue());
  if (it == unit_auras_.end()) return;
  for (std::uint16_t i = 0; i < kMaxTotalSlots; ++i) {
    if (it->second.slots[i].has_value()) {
      fn(static_cast<std::uint8_t>(i), it->second.slots[i].value());
    }
  }
}

void AuraTracker::Reset() {
  std::lock_guard lock(mutex_);
  unit_auras_.clear();
}

}

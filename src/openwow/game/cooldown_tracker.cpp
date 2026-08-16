
#include "openwow/game/cooldown_tracker.h"

#include <algorithm>

namespace openwow::game {

float CooldownInfo::GetRemainingTime(std::uint32_t current_time) const {
  if (duration == 0) return 0.0f;
  std::uint32_t end_time = start_time + duration;
  if (current_time >= end_time) return 0.0f;
  return static_cast<float>(end_time - current_time);
}

float CooldownInfo::GetProgress(std::uint32_t current_time) const {
  if (duration == 0) return 1.0f;
  float remaining = GetRemainingTime(current_time);
  return 1.0f - (remaining / static_cast<float>(duration));
}

bool CooldownInfo::IsReady(std::uint32_t current_time) const {
  if (duration == 0) return true;
  return current_time >= (start_time + duration);
}

CooldownTracker& CooldownTracker::Get() {
  static CooldownTracker instance;
  return instance;
}

void CooldownTracker::SetSpellCooldown(std::uint32_t spell_id,
                                       std::uint32_t duration,
                                       std::uint32_t start_time,
                                       std::uint32_t category_id) {
  std::lock_guard lock(mutex_);
  CooldownInfo& cd = spell_cooldowns_[spell_id];
  cd.id = spell_id;
  cd.duration = duration;
  cd.start_time = start_time;
  cd.category_id = category_id;
}

void CooldownTracker::SetCategoryCooldown(std::uint32_t category_id,
                                          std::uint32_t duration,
                                          std::uint32_t start_time) {
  std::lock_guard lock(mutex_);
  CooldownInfo& cd = category_cooldowns_[category_id];
  cd.id = category_id;
  cd.duration = duration;
  cd.start_time = start_time;
  cd.category_id = category_id;
}

void CooldownTracker::ClearSpellCooldown(std::uint32_t spell_id) {
  std::lock_guard lock(mutex_);
  spell_cooldowns_.erase(spell_id);
}

void CooldownTracker::ClearAllCooldowns() {
  std::lock_guard lock(mutex_);
  spell_cooldowns_.clear();
  item_cooldowns_.clear();
  category_cooldowns_.clear();
}

void CooldownTracker::AdjustSpellCooldown(std::uint32_t spell_id,
                                           std::int32_t delta_ms) {
  std::lock_guard lock(mutex_);
  auto it = spell_cooldowns_.find(spell_id);
  if (it == spell_cooldowns_.end()) return;

  CooldownInfo& cd = it->second;

  cd.start_time = static_cast<std::uint32_t>(
      static_cast<std::int64_t>(cd.start_time) + delta_ms);

  if (cd.category_id != 0) {
    auto cat_it = category_cooldowns_.find(cd.category_id);
    if (cat_it != category_cooldowns_.end()) {
      cat_it->second.start_time = static_cast<std::uint32_t>(
          static_cast<std::int64_t>(cat_it->second.start_time) + delta_ms);
    }
  }
}

const CooldownInfo* CooldownTracker::GetSpellCooldown(
    std::uint32_t spell_id) const {
  std::lock_guard lock(mutex_);
  auto it = spell_cooldowns_.find(spell_id);
  return it != spell_cooldowns_.end() ? &it->second : nullptr;
}

float CooldownTracker::GetSpellCooldownRemaining(
    std::uint32_t spell_id, std::uint32_t current_time) const {
  std::lock_guard lock(mutex_);
  auto it = spell_cooldowns_.find(spell_id);
  if (it == spell_cooldowns_.end()) return 0.0f;
  return it->second.GetRemainingTime(current_time);
}

float CooldownTracker::GetCategoryCooldownRemaining(
    const std::uint32_t category_id, const std::uint32_t current_time) const {
  if (category_id == 0u) {
    return 0.0f;
  }

  std::lock_guard lock(mutex_);
  const auto it = category_cooldowns_.find(category_id);
  return it != category_cooldowns_.end()
             ? it->second.GetRemainingTime(current_time)
             : 0.0f;
}

bool CooldownTracker::IsSpellReady(std::uint32_t spell_id,
                                   std::uint32_t current_time) const {
  std::lock_guard lock(mutex_);

  auto it = spell_cooldowns_.find(spell_id);
  if (it != spell_cooldowns_.end() && !it->second.IsReady(current_time))
    return false;

  if (it != spell_cooldowns_.end() && it->second.category_id != 0) {
    auto cat_it = category_cooldowns_.find(it->second.category_id);
    if (cat_it != category_cooldowns_.end() &&
        !cat_it->second.IsReady(current_time))
      return false;
  }

  return true;
}

void CooldownTracker::SetItemCooldown(std::uint32_t item_id,
                                      std::uint32_t duration,
                                      std::uint32_t start_time) {
  std::lock_guard lock(mutex_);
  CooldownInfo& cd = item_cooldowns_[item_id];
  cd.id = item_id;
  cd.duration = duration;
  cd.start_time = start_time;
}

const CooldownInfo* CooldownTracker::GetItemCooldown(
    std::uint32_t item_id) const {
  std::lock_guard lock(mutex_);
  const auto it = item_cooldowns_.find(item_id);
  return it != item_cooldowns_.end() ? &it->second : nullptr;
}

float CooldownTracker::GetItemCooldownRemaining(
    std::uint32_t item_id, std::uint32_t current_time) const {
  std::lock_guard lock(mutex_);
  auto it = item_cooldowns_.find(item_id);
  if (it == item_cooldowns_.end()) return 0.0f;
  return it->second.GetRemainingTime(current_time);
}

bool CooldownTracker::IsItemReady(std::uint32_t item_id,
                                  std::uint32_t current_time) const {
  std::lock_guard lock(mutex_);
  auto it = item_cooldowns_.find(item_id);
  if (it == item_cooldowns_.end()) return true;
  return it->second.IsReady(current_time);
}

bool CooldownTracker::IsItemUseReady(const std::uint32_t spell_id,
                                     const std::uint32_t item_id,
                                     const std::uint32_t category_id,
                                     const std::uint32_t current_time) const {
  std::lock_guard lock(mutex_);

  if (item_id != 0) {
    const auto item = item_cooldowns_.find(item_id);
    if (item != item_cooldowns_.end() &&
        !item->second.IsReady(current_time)) {
      return false;
    }
  }

  if (spell_id != 0) {
    const auto spell = spell_cooldowns_.find(spell_id);
    if (spell != spell_cooldowns_.end() &&
        !spell->second.IsReady(current_time)) {
      return false;
    }
  }

  if (category_id != 0) {
    const auto category = category_cooldowns_.find(category_id);
    if (category != category_cooldowns_.end() &&
        !category->second.IsReady(current_time)) {
      return false;
    }
  }

  return true;
}

void CooldownTracker::ForEachCooldown(
    const std::function<void(std::uint32_t id, const CooldownInfo&)>& fn)
    const {
  std::lock_guard lock(mutex_);
  for (const auto& [id, cd] : spell_cooldowns_) fn(id, cd);
  for (const auto& [id, cd] : item_cooldowns_) fn(id, cd);
}

void CooldownTracker::PruneExpiredCooldowns(std::uint32_t current_time) {
  std::lock_guard lock(mutex_);

  for (auto it = spell_cooldowns_.begin(); it != spell_cooldowns_.end(); ) {
    if (it->second.IsReady(current_time)) {
      it = spell_cooldowns_.erase(it);
    } else {
      ++it;
    }
  }

  for (auto it = item_cooldowns_.begin(); it != item_cooldowns_.end(); ) {
    if (it->second.IsReady(current_time)) {
      it = item_cooldowns_.erase(it);
    } else {
      ++it;
    }
  }

  for (auto it = category_cooldowns_.begin(); it != category_cooldowns_.end(); ) {
    if (it->second.IsReady(current_time)) {
      it = category_cooldowns_.erase(it);
    } else {
      ++it;
    }
  }

}

void CooldownTracker::Reset() {
  std::lock_guard lock(mutex_);
  spell_cooldowns_.clear();
  item_cooldowns_.clear();
  category_cooldowns_.clear();
}

}

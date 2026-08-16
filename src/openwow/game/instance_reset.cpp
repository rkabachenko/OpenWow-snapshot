
#include "openwow/game/instance_reset.h"

#include <algorithm>
#include <chrono>

namespace openwow::game {

InstanceResetSystem& InstanceResetSystem::Get() {
  static InstanceResetSystem instance;
  return instance;
}

const InstanceResetEntry* InstanceResetSystem::FindEntry(
    uint32_t mapId, uint32_t difficulty) const {
  for (const auto& e : resets_) {
    if (e.mapId == mapId && e.difficulty == difficulty) return &e;
  }
  return nullptr;
}

InstanceResetEntry* InstanceResetSystem::FindEntryMut(uint32_t mapId,
                                                       uint32_t difficulty) {
  for (auto& e : resets_) {
    if (e.mapId == mapId && e.difficulty == difficulty) return &e;
  }
  return nullptr;
}

void InstanceResetSystem::SetResets(
    const std::vector<InstanceResetEntry>& resets) {
  std::lock_guard lock(mutex_);
  resets_ = resets;
}

std::vector<InstanceResetEntry> InstanceResetSystem::GetResets() const {
  std::lock_guard lock(mutex_);
  return resets_;
}

uint32_t InstanceResetSystem::GetResetCount() const {
  std::lock_guard lock(mutex_);
  return static_cast<uint32_t>(resets_.size());
}

std::vector<InstanceResetEntry> InstanceResetSystem::GetResetsForMap(
    uint32_t mapId) const {
  std::lock_guard lock(mutex_);
  std::vector<InstanceResetEntry> result;
  for (const auto& e : resets_) {
    if (e.mapId == mapId) result.push_back(e);
  }
  return result;
}

std::optional<InstanceResetEntry> InstanceResetSystem::GetNextReset() const {
  std::lock_guard lock(mutex_);
  if (resets_.empty()) return std::nullopt;

  auto now = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());

  const InstanceResetEntry* best = nullptr;
  for (const auto& e : resets_) {
    if (e.resetTime > now) {
      if (!best || e.resetTime < best->resetTime) {
        best = &e;
      }
    }
  }
  if (best) return *best;
  return std::nullopt;
}

int64_t InstanceResetSystem::GetTimeUntilReset(uint32_t mapId,
                                                uint32_t difficulty) const {
  std::lock_guard lock(mutex_);
  const auto* entry = FindEntry(mapId, difficulty);
  if (!entry) return -1;

  auto now = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());

  if (entry->resetTime <= now) return 0;
  return static_cast<int64_t>(entry->resetTime - now);
}

bool InstanceResetSystem::IsExtended(uint32_t mapId,
                                      uint32_t difficulty) const {
  std::lock_guard lock(mutex_);
  const auto* entry = FindEntry(mapId, difficulty);
  return entry && entry->isExtended;
}

void InstanceResetSystem::SetExtended(uint32_t mapId, uint32_t difficulty,
                                       bool extended) {
  std::lock_guard lock(mutex_);
  auto* entry = FindEntryMut(mapId, difficulty);
  if (entry) entry->isExtended = extended;
}

std::pair<uint32_t, uint32_t> InstanceResetSystem::GetEncounterProgress(
    uint32_t mapId, uint32_t difficulty) const {
  std::lock_guard lock(mutex_);
  const auto* entry = FindEntry(mapId, difficulty);
  if (!entry) return {0, 0};
  return {entry->encountersCompleted, entry->encountersTotal};
}

void InstanceResetSystem::AddReset(const InstanceResetEntry& entry) {
  std::lock_guard lock(mutex_);

  auto* existing = FindEntryMut(entry.mapId, entry.difficulty);
  if (existing) {
    *existing = entry;
    return;
  }
  resets_.push_back(entry);
}

void InstanceResetSystem::RemoveReset(uint32_t mapId, uint32_t difficulty) {
  std::lock_guard lock(mutex_);
  resets_.erase(
      std::remove_if(resets_.begin(), resets_.end(),
                     [mapId, difficulty](const InstanceResetEntry& e) {
                       return e.mapId == mapId && e.difficulty == difficulty;
                     }),
      resets_.end());
}

bool InstanceResetSystem::CanManualReset(uint32_t mapId) const {
  std::lock_guard lock(mutex_);

  for (const auto& e : resets_) {
    if (e.mapId == mapId && e.difficulty == 0) return true;
  }
  return false;
}

void InstanceResetSystem::SetDailyResetTime(uint64_t time) {
  std::lock_guard lock(mutex_);
  dailyResetTime_ = time;
}

uint64_t InstanceResetSystem::GetDailyResetTime() const {
  std::lock_guard lock(mutex_);
  return dailyResetTime_;
}

void InstanceResetSystem::SetWeeklyResetTime(uint64_t time) {
  std::lock_guard lock(mutex_);
  weeklyResetTime_ = time;
}

uint64_t InstanceResetSystem::GetWeeklyResetTime() const {
  std::lock_guard lock(mutex_);
  return weeklyResetTime_;
}

void InstanceResetSystem::Clear() {
  std::lock_guard lock(mutex_);
  resets_.clear();
}

void InstanceResetSystem::Reset() {
  std::lock_guard lock(mutex_);
  resets_.clear();
  dailyResetTime_ = 0;
  weeklyResetTime_ = 0;
}

}

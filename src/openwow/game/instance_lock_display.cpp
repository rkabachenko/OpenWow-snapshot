
#include "openwow/game/instance_lock_display.h"

#include <algorithm>

namespace openwow::game {

const InstanceLockDisplayEntry* InstanceLockDisplay::FindById(
    uint32_t instanceId) const {
  for (const auto& e : locks_) {
    if (e.instanceId == instanceId) return &e;
  }
  return nullptr;
}

InstanceLockDisplayEntry* InstanceLockDisplay::FindByIdMut(
    uint32_t instanceId) {
  for (auto& e : locks_) {
    if (e.instanceId == instanceId) return &e;
  }
  return nullptr;
}

void InstanceLockDisplay::SetLocks(
    const std::vector<InstanceLockDisplayEntry>& locks) {
  std::lock_guard lock(mutex_);
  locks_ = locks;
}

std::vector<InstanceLockDisplayEntry> InstanceLockDisplay::GetLocks() const {
  std::lock_guard lock(mutex_);
  return locks_;
}

std::optional<InstanceLockDisplayEntry> InstanceLockDisplay::GetLock(
    uint32_t instanceId) const {
  std::lock_guard lock(mutex_);
  const auto* entry = FindById(instanceId);
  if (entry) return *entry;
  return std::nullopt;
}

std::vector<InstanceLockDisplayEntry> InstanceLockDisplay::GetRaidLocks()
    const {
  std::lock_guard lock(mutex_);
  std::vector<InstanceLockDisplayEntry> result;
  for (const auto& e : locks_) {
    if (e.isRaid) result.push_back(e);
  }
  return result;
}

std::vector<InstanceLockDisplayEntry> InstanceLockDisplay::GetDungeonLocks()
    const {
  std::lock_guard lock(mutex_);
  std::vector<InstanceLockDisplayEntry> result;
  for (const auto& e : locks_) {
    if (!e.isRaid) result.push_back(e);
  }
  return result;
}

size_t InstanceLockDisplay::GetLockCount() const {
  std::lock_guard lock(mutex_);
  return locks_.size();
}

bool InstanceLockDisplay::IsLocked(uint32_t instanceId) const {
  std::lock_guard lock(mutex_);
  return FindById(instanceId) != nullptr;
}

uint64_t InstanceLockDisplay::GetNextReset(uint32_t instanceId) const {
  std::lock_guard lock(mutex_);
  const auto* entry = FindById(instanceId);
  return entry ? entry->resetTime : 0;
}

bool InstanceLockDisplay::ToggleExtend(uint32_t instanceId) {
  std::lock_guard lock(mutex_);
  auto* entry = FindByIdMut(instanceId);
  if (!entry) return false;
  entry->isExtended = !entry->isExtended;
  return true;
}

std::string InstanceLockDisplay::GetBossProgress(uint32_t instanceId) const {
  std::lock_guard lock(mutex_);
  const auto* entry = FindById(instanceId);
  if (!entry) return "0/0";
  return std::to_string(entry->numBossesKilled) + "/" +
         std::to_string(entry->totalBosses);
}

void InstanceLockDisplay::Reset() {
  std::lock_guard lock(mutex_);
  locks_.clear();
}

}

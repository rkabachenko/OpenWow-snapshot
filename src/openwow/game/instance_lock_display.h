
#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

struct InstanceLockDisplayEntry {
  uint32_t instanceId = 0;
  std::string instanceName;
  uint32_t difficulty = 0;
  bool isRaid = false;
  uint32_t numBossesKilled = 0;
  uint32_t totalBosses = 0;
  uint64_t resetTime = 0;
  bool isExtended = false;
};

class InstanceLockDisplay {
 public:

  void SetLocks(const std::vector<InstanceLockDisplayEntry>& locks);
  [[nodiscard]] std::vector<InstanceLockDisplayEntry> GetLocks() const;

  [[nodiscard]] std::optional<InstanceLockDisplayEntry> GetLock(
      uint32_t instanceId) const;

  [[nodiscard]] std::vector<InstanceLockDisplayEntry> GetRaidLocks() const;
  [[nodiscard]] std::vector<InstanceLockDisplayEntry> GetDungeonLocks() const;

  [[nodiscard]] size_t GetLockCount() const;
  [[nodiscard]] bool IsLocked(uint32_t instanceId) const;
  [[nodiscard]] uint64_t GetNextReset(uint32_t instanceId) const;

  bool ToggleExtend(uint32_t instanceId);

  [[nodiscard]] std::string GetBossProgress(uint32_t instanceId) const;

  void Reset();

 private:
  [[nodiscard]] const InstanceLockDisplayEntry* FindById(
      uint32_t instanceId) const;
  [[nodiscard]] InstanceLockDisplayEntry* FindByIdMut(uint32_t instanceId);

  std::vector<InstanceLockDisplayEntry> locks_;
  mutable std::mutex mutex_;
};

}

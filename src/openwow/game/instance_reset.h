
#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace openwow::game {

enum class ResetType : uint8_t {
  Manual = 0,
  Scheduled = 1,
  Soft = 2,
};

struct InstanceResetEntry {
  uint32_t mapId = 0;
  std::string mapName;
  uint32_t difficulty = 0;
  uint64_t resetTime = 0;
  bool isExtended = false;
  uint32_t encountersCompleted = 0;
  uint32_t encountersTotal = 0;
};

class InstanceResetSystem {
 public:
  static InstanceResetSystem& Get();

  InstanceResetSystem(const InstanceResetSystem&) = delete;
  InstanceResetSystem& operator=(const InstanceResetSystem&) = delete;

  void SetResets(const std::vector<InstanceResetEntry>& resets);
  [[nodiscard]] std::vector<InstanceResetEntry> GetResets() const;
  [[nodiscard]] uint32_t GetResetCount() const;

  [[nodiscard]] std::vector<InstanceResetEntry> GetResetsForMap(
      uint32_t mapId) const;

  [[nodiscard]] std::optional<InstanceResetEntry> GetNextReset() const;

  [[nodiscard]] int64_t GetTimeUntilReset(uint32_t mapId,
                                          uint32_t difficulty) const;

  [[nodiscard]] bool IsExtended(uint32_t mapId, uint32_t difficulty) const;
  void SetExtended(uint32_t mapId, uint32_t difficulty, bool extended);

  [[nodiscard]] std::pair<uint32_t, uint32_t> GetEncounterProgress(
      uint32_t mapId, uint32_t difficulty) const;

  void AddReset(const InstanceResetEntry& entry);
  void RemoveReset(uint32_t mapId, uint32_t difficulty);

  [[nodiscard]] bool CanManualReset(uint32_t mapId) const;

  void SetDailyResetTime(uint64_t time);
  [[nodiscard]] uint64_t GetDailyResetTime() const;

  void SetWeeklyResetTime(uint64_t time);
  [[nodiscard]] uint64_t GetWeeklyResetTime() const;

  void Clear();
  void Reset();

 private:
  InstanceResetSystem() = default;

  [[nodiscard]] const InstanceResetEntry* FindEntry(uint32_t mapId,
                                                     uint32_t difficulty) const;
  [[nodiscard]] InstanceResetEntry* FindEntryMut(uint32_t mapId,
                                                  uint32_t difficulty);

  std::vector<InstanceResetEntry> resets_;
  uint64_t dailyResetTime_ = 0;
  uint64_t weeklyResetTime_ = 0;
  mutable std::mutex mutex_;
};

}

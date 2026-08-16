
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace openwow::game {

enum class DungeonDifficultyLevel : uint8_t {
  Normal = 0,
  Heroic = 1,
};

enum class RaidDifficultyLevel : uint8_t {
  Normal10 = 0,
  Normal25 = 1,
  Heroic10 = 2,
  Heroic25 = 3,
};

class DungeonDifficultyUI {
 public:

  void SetDungeonDifficulty(DungeonDifficultyLevel level);
  [[nodiscard]] DungeonDifficultyLevel GetDungeonDifficulty() const;
  [[nodiscard]] std::string GetDungeonDifficultyName() const;

  void SetRaidDifficulty(RaidDifficultyLevel level);
  [[nodiscard]] RaidDifficultyLevel GetRaidDifficulty() const;
  [[nodiscard]] std::string GetRaidDifficultyName() const;

  [[nodiscard]] bool CanChangeDifficulty() const;

  void SetInInstance(bool inInstance);
  [[nodiscard]] bool IsInInstance() const;

  void SetIsGroupLeader(bool isLeader);
  [[nodiscard]] bool IsGroupLeader() const;

  [[nodiscard]] std::vector<DungeonDifficultyLevel>
  GetAvailableDungeonDifficulties() const;

  [[nodiscard]] std::vector<RaidDifficultyLevel>
  GetAvailableRaidDifficulties() const;

  void Reset();

 private:
  DungeonDifficultyLevel dungeonDifficulty_ = DungeonDifficultyLevel::Normal;
  RaidDifficultyLevel raidDifficulty_ = RaidDifficultyLevel::Normal10;
  bool inInstance_ = false;
  bool isGroupLeader_ = true;
  mutable std::mutex mutex_;
};

}


#include "openwow/game/dungeon_difficulty_ui.h"

namespace openwow::game {

void DungeonDifficultyUI::SetDungeonDifficulty(DungeonDifficultyLevel level) {
  std::lock_guard lock(mutex_);
  dungeonDifficulty_ = level;
}

DungeonDifficultyLevel DungeonDifficultyUI::GetDungeonDifficulty() const {
  std::lock_guard lock(mutex_);
  return dungeonDifficulty_;
}

std::string DungeonDifficultyUI::GetDungeonDifficultyName() const {
  std::lock_guard lock(mutex_);
  switch (dungeonDifficulty_) {
    case DungeonDifficultyLevel::Normal:
      return "Normal";
    case DungeonDifficultyLevel::Heroic:
      return "Heroic";
  }
  return "Unknown";
}

void DungeonDifficultyUI::SetRaidDifficulty(RaidDifficultyLevel level) {
  std::lock_guard lock(mutex_);
  raidDifficulty_ = level;
}

RaidDifficultyLevel DungeonDifficultyUI::GetRaidDifficulty() const {
  std::lock_guard lock(mutex_);
  return raidDifficulty_;
}

std::string DungeonDifficultyUI::GetRaidDifficultyName() const {
  std::lock_guard lock(mutex_);
  switch (raidDifficulty_) {
    case RaidDifficultyLevel::Normal10:
      return "10 Player";
    case RaidDifficultyLevel::Normal25:
      return "25 Player";
    case RaidDifficultyLevel::Heroic10:
      return "10 Player (Heroic)";
    case RaidDifficultyLevel::Heroic25:
      return "25 Player (Heroic)";
  }
  return "Unknown";
}

bool DungeonDifficultyUI::CanChangeDifficulty() const {
  std::lock_guard lock(mutex_);
  return !inInstance_ && isGroupLeader_;
}

void DungeonDifficultyUI::SetInInstance(bool inInstance) {
  std::lock_guard lock(mutex_);
  inInstance_ = inInstance;
}

bool DungeonDifficultyUI::IsInInstance() const {
  std::lock_guard lock(mutex_);
  return inInstance_;
}

void DungeonDifficultyUI::SetIsGroupLeader(bool isLeader) {
  std::lock_guard lock(mutex_);
  isGroupLeader_ = isLeader;
}

bool DungeonDifficultyUI::IsGroupLeader() const {
  std::lock_guard lock(mutex_);
  return isGroupLeader_;
}

std::vector<DungeonDifficultyLevel>
DungeonDifficultyUI::GetAvailableDungeonDifficulties() const {
  return {DungeonDifficultyLevel::Normal, DungeonDifficultyLevel::Heroic};
}

std::vector<RaidDifficultyLevel>
DungeonDifficultyUI::GetAvailableRaidDifficulties() const {
  return {RaidDifficultyLevel::Normal10, RaidDifficultyLevel::Normal25,
          RaidDifficultyLevel::Heroic10, RaidDifficultyLevel::Heroic25};
}

void DungeonDifficultyUI::Reset() {
  std::lock_guard lock(mutex_);
  dungeonDifficulty_ = DungeonDifficultyLevel::Normal;
  raidDifficulty_ = RaidDifficultyLevel::Normal10;
  inInstance_ = false;
  isGroupLeader_ = true;
}

}

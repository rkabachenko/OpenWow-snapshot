#pragma once

#include "openwow/game/achievements/model/achievement_state_types.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace openwow::game {

class TrackedAchievementState {
 public:
  static TrackedAchievementState& Get();

  void Initialize();
  void Destroy();
  void Reset();

  void SaveTrackedAchievementsToCVar();
  void LoadTrackedAchievementsFromCVar();
  void LoadTrackedAchievementsFromString(const std::string& encoded);

  void AddTrackedAchievement(AchievementId achievement_id);
  void RemoveTrackedAchievement(AchievementId achievement_id);
  [[nodiscard]] bool IsTrackedAchievement(AchievementId achievement_id) const;
  [[nodiscard]] std::size_t GetNumTrackedAchievements() const;
  [[nodiscard]] std::vector<AchievementId> GetTrackedAchievements() const;

 private:
  TrackedAchievementState() = default;

  void EnsureLoaded() const;
  void FireTrackedAchievementUpdate(AchievementId achievement_id) const;
  void ResetRuntimeState(bool loaded);

  static constexpr std::size_t kMaxTrackedAchievements = 10;

  mutable std::mutex mutex_;
  std::vector<AchievementId> tracked_achievement_ids_;
  mutable bool loaded_{false};
  bool initialized_{false};
};

}

#pragma once

#include "openwow/game/achievements/adapters/lua/achievement_faction_filter.h"
#include "openwow/game/achievements/model/achievement_state_types.h"
#include "openwow/game/achievements/rules/achievement_category_resolver.h"

#include <cstdint>

namespace openwow::ui::game::detail {

struct AchievementCompletionCount {
  std::uint32_t total = 0;
  std::uint32_t completed = 0;
};

template <typename CompletedPredicate>
AchievementCompletionCount CountCompletedAchievements(
    const openwow::data::dbc::DbcLoader& dbc,
    const openwow::game::WorldObject* faction_unit,
    CompletedPredicate is_completed) {
  constexpr std::uint32_t kStatisticsCompletionCategoryId = 81;
  constexpr std::uint32_t kHiddenFlag = 0x2;
  constexpr std::uint32_t kIncompleteCountExclusionFlag = 0x800;

  AchievementCompletionCount count;
  const auto faction = ResolveAchievementFaction(faction_unit);

  for (const auto& achievement : dbc.achievement()) {
    const openwow::game::AchievementId achievement_id{achievement.id};
    if (!openwow::game::AchievementResolvesOutsideStatisticsTree(
            dbc, achievement_id) ||
        (achievement.flags & kHiddenFlag) != 0 ||
        !MatchesAchievementFaction(achievement, faction)) {
      continue;
    }

    const bool is_complete = is_completed(achievement_id);
    if (achievement.category == kStatisticsCompletionCategoryId) {
      continue;
    }
    if (is_complete) {
      ++count.completed;
      ++count.total;
    } else if ((achievement.flags & kIncompleteCountExclusionFlag) == 0) {
      ++count.total;
    }
  }

  return count;
}

}

#pragma once

#include "openwow/game/achievements/adapters/lua/achievement_category_selection.h"
#include "openwow/game/achievements/adapters/lua/achievement_faction_filter.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"

#include <cstdint>

namespace openwow::ui::game::detail {

struct AchievementCategoryCount final {
  std::uint32_t total = 0;
  std::uint32_t completed = 0;
};

inline std::uint32_t CountComparisonCategoryAchievements(
    const openwow::data::dbc::DbcLoader& dbc,
    const openwow::game::WorldObject* comparison_unit,
    const AchievementCategorySelector selector,
    const AchievementIdSet& comparison_completed_ids,
    const AchievementIdSet& local_completed_child_parent_ids) {
  constexpr std::uint32_t kHiddenFlag = 0x2;

  std::uint32_t count = 0;
  const auto comparison_faction =
      ResolveAchievementFaction(comparison_unit);

  for (const auto& achievement : dbc.achievement()) {
    const openwow::game::AchievementId achievement_id{achievement.id};
    if (!selector.Matches(achievement) ||
        (achievement.flags & kHiddenFlag) != 0 ||
        (comparison_unit != nullptr &&
         !MatchesAchievementFaction(achievement, comparison_faction)) ||
        !comparison_completed_ids.contains(achievement_id) ||
        (!selector.IsWildcard() &&
         local_completed_child_parent_ids.contains(achievement_id))) {
      continue;
    }
    ++count;
  }

  return count;
}

inline AchievementCategoryCount CountLocalCategoryAchievements(
    const openwow::data::dbc::DbcLoader& dbc,
    const openwow::game::WorldSession& session,
    const AchievementCategorySelector selector,
    const AchievementIdSet& local_completed_ids,
    const AchievementIdSet& comparison_completed_ids) {
  constexpr std::uint32_t kStatisticsCategoryId = 81;
  constexpr std::uint32_t kHiddenFlag = 0x2;
  constexpr std::uint32_t kIncompleteCountExclusionFlag = 0x800;

  AchievementCategoryCount result;
  const auto* player = session.objects().GetLocalPlayer();
  const auto player_faction = ResolveAchievementFaction(player);
  std::uint32_t comparison_only_count = 0;
  std::uint32_t visible_local_count = 0;
  openwow::game::AchievementId previous_chain_id;

  for (const auto& achievement : dbc.achievement()) {
    if (!selector.Matches(achievement) ||
        (achievement.flags & kHiddenFlag) != 0) {
      continue;
    }

    const openwow::game::AchievementId achievement_id{achievement.id};
    const bool local_completed =
        local_completed_ids.contains(achievement_id);
    const bool comparison_completed =
        comparison_completed_ids.contains(achievement_id);
    if (player != nullptr &&
        !MatchesAchievementFaction(achievement, player_faction) &&
        !comparison_completed) {
      continue;
    }
    if ((achievement.category == kStatisticsCategoryId ||
         (achievement.flags & kIncompleteCountExclusionFlag) != 0) &&
        !local_completed && !comparison_completed) {
      continue;
    }

    if (previous_chain_id.value != 0 &&
        previous_chain_id.value == achievement.parent_achievement) {
      previous_chain_id = achievement_id;
      if (!selector.IsWildcard()) {
        if (comparison_completed) {
          ++comparison_only_count;
        }
        continue;
      }
      ++visible_local_count;
      continue;
    }

    if (local_completed) {
      if (selector.IsWildcard() ||
          !HasCompletedChildAchievement(dbc, local_completed_ids,
                                        achievement_id)) {
        ++result.completed;
        ++visible_local_count;
      }
      continue;
    }

    if (!comparison_completed) {
      previous_chain_id = achievement_id;
      ++visible_local_count;
      continue;
    }

    ++comparison_only_count;
  }

  result.total = comparison_only_count +
                 (selector.IsStatisticsCategory() ? result.completed
                                                  : visible_local_count);
  return result;
}

}

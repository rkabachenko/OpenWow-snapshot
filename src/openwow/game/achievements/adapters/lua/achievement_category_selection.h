#pragma once

#include "openwow/data/formats/dbc/dbc_entries_extended.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/achievements/model/achievement_state_types.h"

#include <cstdint>
#include <unordered_set>

namespace openwow::ui::game::detail {

using AchievementIdSet =
    std::unordered_set<openwow::game::AchievementId,
                       openwow::game::AchievementIdHash>;

enum class AchievementCategorySelectionMode {
  kExactCategory,
  kPrimaryAchievements,
  kMetaAchievements,
  kAllAchievements,
};

struct AchievementCategorySelector final {
  AchievementCategorySelectionMode mode =
      AchievementCategorySelectionMode::kExactCategory;
  std::uint32_t category_id = 0;

  static AchievementCategorySelector FromLuaValue(
      const std::int32_t category_id, const bool include_all = false) {
    if (include_all) {
      return {AchievementCategorySelectionMode::kAllAchievements, 0};
    }
    if (category_id == -1) {
      return {AchievementCategorySelectionMode::kPrimaryAchievements, 0};
    }
    if (category_id == -2) {
      return {AchievementCategorySelectionMode::kMetaAchievements, 0};
    }
    return {AchievementCategorySelectionMode::kExactCategory,
            static_cast<std::uint32_t>(category_id)};
  }

  [[nodiscard]] bool IsWildcard() const {
    return mode != AchievementCategorySelectionMode::kExactCategory;
  }

  [[nodiscard]] bool IsStatisticsCategory() const {
    constexpr std::uint32_t kStatisticsCategoryId = 81;
    return mode == AchievementCategorySelectionMode::kExactCategory &&
           category_id == kStatisticsCategoryId;
  }

  [[nodiscard]] bool Matches(
      const openwow::data::dbc::AchievementEntry& achievement) const {
    constexpr std::uint32_t kCategorySelectorFlag = 0x1;
    switch (mode) {
      case AchievementCategorySelectionMode::kExactCategory:
        return achievement.category == category_id;
      case AchievementCategorySelectionMode::kPrimaryAchievements:
        return (achievement.flags & kCategorySelectorFlag) == 0;
      case AchievementCategorySelectionMode::kMetaAchievements:
        return (achievement.flags & kCategorySelectorFlag) != 0;
      case AchievementCategorySelectionMode::kAllAchievements:
        return true;
    }

    return false;
  }
};

template <typename Range, typename IdAccessor>
AchievementIdSet CollectAchievementIds(const Range& range,
                                       IdAccessor get_id) {
  AchievementIdSet ids;
  ids.reserve(range.size());
  for (const auto& entry : range) {
    ids.insert(get_id(entry));
  }
  return ids;
}

inline bool HasCompletedChildAchievement(
    const openwow::data::dbc::DbcLoader& dbc,
    const AchievementIdSet& completed_ids,
    const openwow::game::AchievementId achievement_id) {
  for (const auto& achievement : dbc.achievement()) {
    if (achievement.parent_achievement != achievement_id.value) {
      continue;
    }
    if (completed_ids.contains(
            openwow::game::AchievementId{achievement.id})) {
      return true;
    }
  }
  return false;
}

inline AchievementIdSet BuildCompletedChildParentAchievementSet(
    const openwow::data::dbc::DbcLoader& dbc,
    const AchievementIdSet& completed_ids) {
  AchievementIdSet parent_ids;
  for (const auto& achievement : dbc.achievement()) {
    if (achievement.parent_achievement == 0 ||
        !completed_ids.contains(
            openwow::game::AchievementId{achievement.id})) {
      continue;
    }
    parent_ids.insert(
        openwow::game::AchievementId{achievement.parent_achievement});
  }
  return parent_ids;
}

}

#pragma once

#include "openwow/game/achievements/model/achievement_state_types.h"

#include <cstdint>
#include <optional>

namespace openwow::data::dbc {

class DbcLoader;

}

namespace openwow::game {

inline constexpr std::uint32_t kAchievementStatisticsRootCategoryId = 1;

[[nodiscard]] bool AchievementCategoryResolvesToStatisticsTree(
    const data::dbc::DbcLoader& dbc, std::uint32_t category_id);

[[nodiscard]] bool AchievementResolvesToStatisticsTree(
    const data::dbc::DbcLoader& dbc, AchievementId achievement_id);

[[nodiscard]] bool AchievementResolvesOutsideStatisticsTree(
    const data::dbc::DbcLoader& dbc, AchievementId achievement_id);

[[nodiscard]] std::optional<AchievementId> ResolveAchievementForCriteria(
    const data::dbc::DbcLoader& dbc, AchievementCriteriaId criteria_id);

[[nodiscard]] std::optional<AchievementId>
ResolveStatisticsAchievementForCriteria(const data::dbc::DbcLoader& dbc,
                                        AchievementCriteriaId criteria_id);

}

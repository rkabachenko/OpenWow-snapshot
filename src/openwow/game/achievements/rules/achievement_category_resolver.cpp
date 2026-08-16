#include "openwow/game/achievements/rules/achievement_category_resolver.h"

#include "openwow/data/formats/dbc/dbc_entries_extended.h"
#include "openwow/data/formats/dbc/dbc_loader.h"

namespace openwow::game {

namespace {

const openwow::data::dbc::AchievementEntry* LookupAchievementEntry(
    const openwow::data::dbc::DbcLoader& dbc,
    const AchievementId achievement_id) {
  return dbc.achievement().LookupEntry(achievement_id.value);
}

const openwow::data::dbc::AchievementCategoryEntry* LookupAchievementCategoryEntry(
    const openwow::data::dbc::DbcLoader& dbc,
    const std::uint32_t category_id) {
  return dbc.achievement_category().LookupEntry(category_id);
}

const openwow::data::dbc::AchievementCriteriaEntry* LookupAchievementCriteriaEntry(
    const openwow::data::dbc::DbcLoader& dbc,
    const AchievementCriteriaId criteria_id) {
  return dbc.achievement_criteria().LookupEntry(criteria_id.value);
}

}

bool AchievementCategoryResolvesToStatisticsTree(
    const data::dbc::DbcLoader& dbc, const std::uint32_t category_id) {
  auto current_category_id = category_id;
  auto remaining_hops = dbc.achievement_category().entries().size() + 1;
  while (remaining_hops-- > 0) {
    const auto* category =
        LookupAchievementCategoryEntry(dbc, current_category_id);
    if (!category) {
      return false;
    }
    if (category->id == kAchievementStatisticsRootCategoryId) {
      return true;
    }
    current_category_id = category->parent_category;
  }
  return false;
}

bool AchievementResolvesToStatisticsTree(const data::dbc::DbcLoader& dbc,
                                         const AchievementId achievement_id) {
  const auto* achievement = LookupAchievementEntry(dbc, achievement_id);
  if (!achievement) {
    return false;
  }
  return AchievementCategoryResolvesToStatisticsTree(dbc,
                                                       achievement->category);
}

bool AchievementResolvesOutsideStatisticsTree(
    const data::dbc::DbcLoader& dbc, const AchievementId achievement_id) {
  const auto* achievement = LookupAchievementEntry(dbc, achievement_id);
  if (!achievement) {
    return false;
  }
  return !AchievementResolvesToStatisticsTree(dbc, achievement_id);
}

std::optional<AchievementId> ResolveAchievementForCriteria(
    const data::dbc::DbcLoader& dbc,
    const AchievementCriteriaId criteria_id) {
  const auto* criteria = LookupAchievementCriteriaEntry(dbc, criteria_id);
  if (!criteria) {
    return std::nullopt;
  }

  const auto* achievement =
      LookupAchievementEntry(dbc, AchievementId{criteria->achievement_id});
  if (!achievement) {
    return std::nullopt;
  }

  return AchievementId{achievement->id};
}

std::optional<AchievementId> ResolveStatisticsAchievementForCriteria(
    const data::dbc::DbcLoader& dbc,
    const AchievementCriteriaId criteria_id) {
  const auto achievement_id = ResolveAchievementForCriteria(dbc, criteria_id);
  if (!achievement_id) {
    return std::nullopt;
  }
  if (!AchievementResolvesToStatisticsTree(dbc, *achievement_id)) {
    return std::nullopt;
  }
  return achievement_id;
}

}

#pragma once

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/achievements/application/achievement_state.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct lua_State;

namespace openwow::ui::game::detail {

struct CriteriaProgressSnapshot final {
  openwow::game::AchievementCriteriaId criteria_id;
  openwow::game::AchievementProgressCounter counter;
  openwow::game::PackedAchievementTime date;
  openwow::game::ObjectGuid player_guid;
};

using CriteriaProgressMap =
    std::unordered_map<openwow::game::AchievementCriteriaId,
                       CriteriaProgressSnapshot,
                       openwow::game::AchievementCriteriaIdHash>;

struct AggregatedCriteriaState final {
  std::uint64_t sum_value = 0;
  std::uint64_t max_value = 0;
  openwow::game::AchievementCriteriaId max_criteria_id;
  bool has_max_criteria = false;
  bool max_value_tied = false;
  std::uint64_t completed_count = 0;
  const CriteriaProgressSnapshot* representative_progress = nullptr;
  const openwow::data::dbc::AchievementCriteriaEntry*
      last_progress_criteria = nullptr;
};

struct FormattedCriteriaProgress final {
  std::uint32_t quantity = 0;
  std::string text;
};

CriteriaProgressMap BuildCriteriaProgressMap(
    const std::unordered_map<openwow::game::AchievementCriteriaId,
                             openwow::game::CriteriaProgress,
                             openwow::game::AchievementCriteriaIdHash>&
        criteria);
CriteriaProgressMap BuildCriteriaProgressMap(
    const std::vector<openwow::game::CriteriaProgress>& criteria);

bool HasCompletedAchievement(
    const std::unordered_map<openwow::game::AchievementId,
                             openwow::game::CompletedAchievement,
                             openwow::game::AchievementIdHash>& achievements,
    openwow::game::AchievementId achievement_id);
bool HasCompletedAchievement(
    const std::vector<openwow::game::CompletedAchievement>& achievements,
    openwow::game::AchievementId achievement_id);

const openwow::data::dbc::AchievementCriteriaEntry*
LookupAchievementCriteriaEntry(
    const openwow::data::dbc::DbcLoader& dbc,
    openwow::game::AchievementCriteriaId criteria_id);
std::vector<const openwow::data::dbc::AchievementCriteriaEntry*>
CollectAchievementCriteriaEntries(
    const openwow::data::dbc::DbcLoader& dbc,
    openwow::game::AchievementId achievement_id);
const openwow::data::dbc::AchievementEntry* ResolveCriteriaOwnerAchievement(
    const openwow::data::dbc::DbcLoader& dbc,
    const openwow::data::dbc::AchievementCriteriaEntry& criteria);
const openwow::data::dbc::AchievementEntry* ResolveCriteriaRootAchievement(
    const openwow::data::dbc::DbcLoader& dbc,
    const openwow::data::dbc::AchievementEntry& achievement);

AggregatedCriteriaState BuildAggregatedCriteriaState(
    const openwow::data::dbc::DbcLoader& dbc,
    const openwow::data::dbc::AchievementEntry& achievement,
    const CriteriaProgressMap& progress_snapshots,
    std::uint32_t tie_clear_flags);
FormattedCriteriaProgress FormatAchievementProgressString(
    lua_State* state, const openwow::data::dbc::DbcLoader& dbc,
    const openwow::data::dbc::AchievementEntry& achievement,
    std::uint64_t sum_value, std::uint64_t max_value,
    openwow::game::AchievementCriteriaId max_criteria_id,
    bool has_max_criteria, std::uint64_t completed_count,
    const CriteriaProgressSnapshot* current_progress,
    std::uint32_t effective_criteria_flags, bool has_completed_achievement,
    const openwow::data::dbc::AchievementCriteriaEntry* suffix_criteria,
    std::uint32_t override_max_quantity);

}

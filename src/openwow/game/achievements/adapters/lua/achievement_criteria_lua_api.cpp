#include "openwow/game/achievements/adapters/lua/achievement_criteria_lua_api.h"

#include "openwow/game/achievements/adapters/lua/achievement_progress_formatter.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/lua_numeric.h"
#include "openwow/ui/runtime/lua/lua_call.h"

#include <cstdint>
#include <string>

namespace openwow::ui::game::detail {
namespace {

using AchievementEntry = openwow::data::dbc::AchievementEntry;
using CriteriaEntry = openwow::data::dbc::AchievementCriteriaEntry;

constexpr std::uint32_t kHiddenAchievementFlag = 0x2;
constexpr std::uint32_t kAggregateMaxFlag = 0x10;
constexpr std::uint32_t kAggregateCountFlag = 0x20;
constexpr std::uint32_t kAppendDenominatorFlag = 0x80;
constexpr std::uint32_t kShowDenominatorFlag = 0x1;
constexpr std::uint32_t kHiddenIndexedCriteriaFlag = 0x2;

enum class CriteriaIndexMode {
  kStandard,
  kAggregate,
};

struct CriteriaSelection final {
  const CriteriaEntry* criteria = nullptr;
  CriteriaIndexMode mode = CriteriaIndexMode::kStandard;
};

const AchievementEntry* LookupAchievementEntry(
    const openwow::data::dbc::DbcLoader& dbc,
    const openwow::game::AchievementId achievement_id) {
  return dbc.achievement().LookupEntry(achievement_id.value);
}

CriteriaSelection ResolveCriteriaByIndex(
    const openwow::data::dbc::DbcLoader& dbc,
    const AchievementEntry& achievement, std::uint32_t criteria_index) {
  const bool aggregate_indexing =
      (achievement.flags &
       (kAggregateMaxFlag | kAggregateCountFlag |
        kAppendDenominatorFlag)) != 0;
  CriteriaIndexMode mode = CriteriaIndexMode::kStandard;
  if (aggregate_indexing) {
    mode = CriteriaIndexMode::kAggregate;
  }

  const auto* root = ResolveCriteriaRootAchievement(dbc, achievement);
  for (const auto* criteria : CollectAchievementCriteriaEntries(
           dbc, openwow::game::AchievementId{root->id})) {
    if ((criteria->flags & kHiddenIndexedCriteriaFlag) != 0 &&
        !aggregate_indexing) {
      continue;
    }
    if (--criteria_index == 0) {
      return {criteria, mode};
    }
  }
  return {nullptr, mode};
}

std::string ResolveCriteriaProgressPlayerName(
    openwow::game::WorldSession& session,
    const CriteriaProgressSnapshot& progress) {
  if (progress.player_guid.IsEmpty()) {
    return {};
  }
  const auto player_guid = progress.player_guid.GetRawValue();
  if (const auto* player_name =
          session.query_cache().GetPlayerName(player_guid)) {
    return player_name->name;
  }
  (void)session.query_cache().RequestNameQuery(player_guid);
  return {};
}

bool IsCriteriaCompleteForLua(
    const AchievementEntry& achievement, const CriteriaEntry& criteria,
    const CriteriaProgressSnapshot* progress,
    const CriteriaIndexMode index_mode,
    const bool has_completed_achievement) {
  if (progress == nullptr || index_mode == CriteriaIndexMode::kAggregate) {
    return has_completed_achievement && achievement.count == 0;
  }
  if (has_completed_achievement && achievement.count == 0) {
    return true;
  }
  return progress->counter.value >= criteria.quantity;
}

std::uint32_t ResolveCriteriaRequiredQuantity(
    const AchievementEntry& achievement, const CriteriaEntry& criteria) {
  if ((achievement.flags & kAggregateCountFlag) != 0 &&
      (achievement.flags & kAppendDenominatorFlag) != 0 &&
      achievement.count != 0) {
    return achievement.count;
  }
  return criteria.quantity;
}

int PushAchievementCriteriaInfoResult(
    lua_State* state, openwow::game::WorldSession& session,
    const openwow::data::dbc::DbcLoader& dbc,
    const AchievementEntry& achievement, const CriteriaEntry& criteria,
    const CriteriaProgressMap& progress_snapshots,
    const bool has_completed_achievement,
    const CriteriaIndexMode index_mode) {
  const auto aggregate = BuildAggregatedCriteriaState(
      dbc, achievement, progress_snapshots, achievement.flags);
  const auto progress_it = progress_snapshots.find(
      openwow::game::AchievementCriteriaId{criteria.id});
  const auto* selected_progress =
      progress_it != progress_snapshots.end() ? &progress_it->second
                                              : nullptr;
  std::uint32_t effective_flags = criteria.flags;
  if ((achievement.flags & kAppendDenominatorFlag) != 0) {
    effective_flags |= kShowDenominatorFlag;
  }
  const auto required_quantity =
      ResolveCriteriaRequiredQuantity(achievement, criteria);
  const auto override_quantity =
      required_quantity != criteria.quantity ? required_quantity : 0;
  const auto formatted = FormatAchievementProgressString(
      state, dbc, achievement, aggregate.sum_value, aggregate.max_value,
      aggregate.max_criteria_id, aggregate.has_max_criteria,
      aggregate.completed_count, selected_progress, effective_flags,
      has_completed_achievement, &criteria, override_quantity);

  openwow::ui::lua::LuaCall results(state);
  results.PushString(criteria.description)
      .PushNumber(criteria.type)
      .PushBoolean(IsCriteriaCompleteForLua(
          achievement, criteria, selected_progress, index_mode,
          has_completed_achievement))
      .PushNumber(formatted.quantity)
      .PushNumber(required_quantity);

  if (selected_progress != nullptr &&
      index_mode == CriteriaIndexMode::kStandard) {
    const auto player_name =
        ResolveCriteriaProgressPlayerName(session, *selected_progress);
    if (!player_name.empty()) {
      results.PushString(player_name);
    } else {
      results.PushNil();
    }
  } else {
    results.PushNil();
  }

  return results.PushNumber(effective_flags)
      .PushNumber(criteria.asset)
      .PushString(formatted.text)
      .PushNumber(criteria.id)
      .ResultCount();
}

int PushAchievementCriteriaInfoById(
    lua_State* state, openwow::game::WorldSession& session,
    const openwow::data::dbc::DbcLoader& dbc,
    const openwow::game::AchievementCriteriaId criteria_id) {
  const auto* criteria = LookupAchievementCriteriaEntry(dbc, criteria_id);
  if (criteria == nullptr) {
    return 0;
  }
  const auto* achievement =
      ResolveCriteriaOwnerAchievement(dbc, *criteria);
  if (achievement == nullptr ||
      (achievement->flags & kHiddenAchievementFlag) != 0) {
    return 0;
  }

  const auto progress =
      BuildCriteriaProgressMap(session.achievements().criteria());
  return PushAchievementCriteriaInfoResult(
      state, session, dbc, *achievement, *criteria, progress,
      HasCompletedAchievement(
          session.achievements().completed(),
          openwow::game::AchievementId{achievement->id}),
      CriteriaIndexMode::kStandard);
}

}

int LuaGetAchievementNumCriteria(lua_State* state) {
  const openwow::ui::lua::LuaCall arguments(state);
  const auto achievement_id = openwow::game::AchievementId{
      static_cast<std::uint32_t>(TruncateLuaNumberToSseI32(
          arguments.RequireNumber(
              1, "Usage: GetAchievementNumCriteria(achievementID)")))};

  const auto* dbc = GetDbcLoader(state);
  if (dbc == nullptr) {
    return openwow::ui::lua::LuaCall(state).PushNumber(0u).ResultCount();
  }
  const auto* achievement = LookupAchievementEntry(*dbc, achievement_id);
  if (achievement == nullptr ||
      (achievement->flags & kHiddenAchievementFlag) != 0) {
    return 0;
  }

  const auto* root = ResolveCriteriaRootAchievement(*dbc, *achievement);
  std::uint32_t count = 0;
  if ((achievement->flags &
       (kAggregateMaxFlag | kAggregateCountFlag |
        kAppendDenominatorFlag)) != 0) {
    count = 1;
  }
  for (const auto* criteria : CollectAchievementCriteriaEntries(
           *dbc, openwow::game::AchievementId{root->id})) {
    if ((criteria->flags & kHiddenIndexedCriteriaFlag) == 0) {
      ++count;
    }
  }
  return openwow::ui::lua::LuaCall(state).PushNumber(count).ResultCount();
}

int LuaGetAchievementCriteriaInfo(lua_State* state) {
  const openwow::ui::lua::LuaCall arguments(state);
  const auto first_argument = arguments.RequireNumber(
      1, "Usage: GetAchievementCrieriaInfo(achievementID, criteriaIndex)");

  auto* session = GetWorldSession(state);
  const auto* dbc = GetDbcLoader(state);
  if (session == nullptr || dbc == nullptr) {
    return 0;
  }

  if (!arguments.IsNumber(2)) {
    const auto criteria_id = TruncateLuaNumberToSseI32(first_argument);
    if (criteria_id < 0) {
      return 0;
    }
    return PushAchievementCriteriaInfoById(
        state, *session, *dbc,
        openwow::game::AchievementCriteriaId{
            static_cast<std::uint32_t>(criteria_id)});
  }

  const auto achievement_id = openwow::game::AchievementId{
      TruncateLuaNumberToWrappedLowU32(first_argument)};
  const auto criteria_index = static_cast<std::uint32_t>(
      TruncateLuaNumberToSseI32(arguments.Number(2)));
  const auto* achievement = LookupAchievementEntry(*dbc, achievement_id);
  if (achievement == nullptr ||
      (achievement->flags & kHiddenAchievementFlag) != 0) {
    return 0;
  }

  const auto selection =
      ResolveCriteriaByIndex(*dbc, *achievement, criteria_index);
  if (selection.criteria == nullptr) {
    return 0;
  }

  const auto progress =
      BuildCriteriaProgressMap(session->achievements().criteria());
  return PushAchievementCriteriaInfoResult(
      state, *session, *dbc, *achievement, *selection.criteria, progress,
      HasCompletedAchievement(
          session->achievements().completed(), achievement_id),
      selection.mode);
}

}

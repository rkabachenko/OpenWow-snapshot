#include "openwow/game/achievements/adapters/lua/achievement_statistic_lua_api.h"

#include "openwow/game/achievements/adapters/lua/achievement_progress_formatter.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/runtime/lua/lua_call.h"

namespace openwow::ui::game::detail {
namespace {

template <typename CriteriaRange>
int PushStatistic(lua_State* state,
                  const openwow::game::AchievementId achievement_id,
                  const CriteriaRange& criteria) {
  const auto* dbc = GetDbcLoader(state);
  if (dbc == nullptr) {
    return 0;
  }
  const auto* achievement =
      dbc->achievement().LookupEntry(achievement_id.value);
  if (achievement == nullptr) {
    return 0;
  }

  const auto progress = BuildCriteriaProgressMap(criteria);
  const auto* root = ResolveCriteriaRootAchievement(*dbc, *achievement);
  const auto aggregate = BuildAggregatedCriteriaState(
      *dbc, *achievement, progress,
      root != nullptr ? root->flags : achievement->flags);
  const auto* suffix_criteria =
      aggregate.representative_progress != nullptr
          ? LookupAchievementCriteriaEntry(
                *dbc, aggregate.representative_progress->criteria_id)
          : nullptr;
  const std::uint32_t effective_flags =
      aggregate.last_progress_criteria != nullptr
          ? aggregate.last_progress_criteria->flags
          : 0;
  const auto formatted = FormatAchievementProgressString(
      state, *dbc, *achievement, aggregate.sum_value, aggregate.max_value,
      aggregate.max_criteria_id, aggregate.has_max_criteria,
      aggregate.completed_count, aggregate.representative_progress,
      effective_flags, false, suffix_criteria, 0);
  return openwow::ui::lua::LuaCall(state).PushString(formatted.text).ResultCount();
}

}

int LuaGetStatistic(lua_State* state) {
  const openwow::ui::lua::LuaCall arguments(state);
  const auto achievement_id = openwow::game::AchievementId{
      static_cast<std::uint32_t>(TruncateLuaNumberToSseI32(
          arguments.RequireNumber(
              1, "Usage: GetStatistic(achievementID)")))};
  const auto* session = GetWorldSession(state);
  if (session == nullptr) {
    return 0;
  }
  return PushStatistic(state, achievement_id,
                       session->achievements().criteria());
}

int LuaGetComparisonStatistic(lua_State* state) {
  const openwow::ui::lua::LuaCall arguments(state);
  const auto achievement_id = openwow::game::AchievementId{
      static_cast<std::uint32_t>(TruncateLuaNumberToSseI32(
          arguments.RequireNumber(
              1, "Usage: GetComparisonStatistic(achievementID)")))};
  const auto* session = GetWorldSession(state);
  if (session == nullptr) {
    return 0;
  }
  return PushStatistic(
      state, achievement_id,
      session->achievements().last_inspect().criteria);
}

}

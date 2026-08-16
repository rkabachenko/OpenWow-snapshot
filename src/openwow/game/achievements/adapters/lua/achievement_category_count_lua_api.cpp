#include "openwow/game/achievements/adapters/lua/achievement_category_count_lua_api.h"

#include "openwow/game/achievements/adapters/lua/achievement_category_count_query.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/runtime/lua/lua_call.h"

namespace openwow::ui::game::detail {

int LuaGetCategoryNumAchievements(lua_State* state) {
  const openwow::ui::lua::LuaCall arguments(state);
  const auto category_id = TruncateLuaNumberToSseI32(
      arguments.RequireNumber(
          1, "Usage: GetCategoryNumAchievements(categoryID)"));
  const auto selector = AchievementCategorySelector::FromLuaValue(
      category_id, arguments.IsBoolean(2) && arguments.Boolean(2));

  const auto* session = GetWorldSession(state);
  const auto* dbc = GetDbcLoader(state);
  if (session == nullptr || dbc == nullptr) {
    return openwow::ui::lua::LuaCall(state)
        .PushNumber(0u)
        .PushNumber(0u)
        .ResultCount();
  }

  const auto local_completed_ids = CollectAchievementIds(
      session->achievements().completed_list(),
      [](const auto& achievement) { return achievement.id; });
  const auto comparison_completed_ids = CollectAchievementIds(
      session->achievements().last_inspect().achievements,
      [](const auto& achievement) { return achievement.id; });
  const auto count = CountLocalCategoryAchievements(
      *dbc, *session, selector, local_completed_ids,
      comparison_completed_ids);
  return openwow::ui::lua::LuaCall(state)
      .PushNumber(count.total)
      .PushNumber(count.completed)
      .ResultCount();
}

int LuaGetComparisonCategoryNumAchievements(lua_State* state) {
  const openwow::ui::lua::LuaCall arguments(state);
  const auto category_id = TruncateLuaNumberToSseI32(
      arguments.RequireNumber(
          1, "Usage: GetComparisonCategoryNumAchievements(categoryID)"));
  const auto selector =
      AchievementCategorySelector::FromLuaValue(category_id);

  const auto* session = GetWorldSession(state);
  const auto* dbc = GetDbcLoader(state);
  if (session == nullptr || dbc == nullptr) {
    return openwow::ui::lua::LuaCall(state).PushNumber(0u).ResultCount();
  }

  const auto comparison_completed_ids = CollectAchievementIds(
      session->achievements().last_inspect().achievements,
      [](const auto& achievement) { return achievement.id; });
  const auto local_completed_ids = CollectAchievementIds(
      session->achievements().completed(),
      [](const auto& entry) { return entry.first; });
  const auto local_completed_child_parent_ids =
      BuildCompletedChildParentAchievementSet(*dbc, local_completed_ids);
  const auto* comparison_unit =
      session->objects().Get(session->achievements().comparison_unit());

  const auto count = CountComparisonCategoryAchievements(
      *dbc, comparison_unit, selector, comparison_completed_ids,
      local_completed_child_parent_ids);
  return openwow::ui::lua::LuaCall(state).PushNumber(count).ResultCount();
}

}

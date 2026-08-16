#include "openwow/game/achievements/adapters/lua/achievement_completion_count_lua_api.h"

#include "openwow/game/achievements/adapters/lua/achievement_completion_query.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/runtime/lua/lua_call.h"

#include <cstdint>
#include <unordered_set>

namespace openwow::ui::game::detail {

int LuaGetNumCompletedAchievements(lua_State* state) {
  const auto* session = GetWorldSession(state);
  const auto* dbc = GetDbcLoader(state);
  if (session == nullptr || dbc == nullptr) {
    return openwow::ui::lua::LuaCall(state)
        .PushNumber(0u)
        .PushNumber(0u)
        .ResultCount();
  }

  const auto& completed = session->achievements().completed();
  const auto count = CountCompletedAchievements(
      *dbc, session->objects().GetLocalPlayer(),
      [&completed](const openwow::game::AchievementId achievement_id) {
        return completed.contains(achievement_id);
      });
  return openwow::ui::lua::LuaCall(state)
      .PushNumber(count.total)
      .PushNumber(count.completed)
      .ResultCount();
}

int LuaGetNumComparisonCompletedAchievements(lua_State* state) {
  const auto* session = GetWorldSession(state);
  const auto* dbc = GetDbcLoader(state);
  if (session == nullptr || dbc == nullptr) {
    return openwow::ui::lua::LuaCall(state)
        .PushNumber(0u)
        .PushNumber(0u)
        .ResultCount();
  }

  const auto& inspect = session->achievements().last_inspect();
  std::unordered_set<openwow::game::AchievementId,
                     openwow::game::AchievementIdHash>
      completed_ids;
  completed_ids.reserve(inspect.achievements.size());
  for (const auto& achievement : inspect.achievements) {
    completed_ids.insert(achievement.id);
  }

  const auto count = CountCompletedAchievements(
      *dbc, session->objects().Get(inspect.target),
      [&completed_ids](const openwow::game::AchievementId achievement_id) {
        return completed_ids.contains(achievement_id);
      });
  return openwow::ui::lua::LuaCall(state)
      .PushNumber(count.total)
      .PushNumber(count.completed)
      .ResultCount();
}

}

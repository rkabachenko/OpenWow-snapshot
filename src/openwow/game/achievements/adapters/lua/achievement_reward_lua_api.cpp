#include "openwow/game/achievements/adapters/lua/achievement_reward_lua_api.h"

#include "openwow/game/achievements/adapters/lua/achievement_lua_visibility.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/lua_numeric.h"
#include "openwow/ui/runtime/lua/lua_call.h"

namespace openwow::ui::game::detail {

int LuaGetAchievementNumRewards(lua_State* state) {
  const openwow::ui::lua::LuaCall arguments(state);
  const auto achievement_id = TruncateLuaNumberToSseI32(
      arguments.RequireNumber(
          1, "Usage: GetAchievementNumRewards(achievementID)"));

  const auto* dbc = GetDbcLoader(state);
  if (dbc == nullptr ||
      FindVisibleAchievement(*dbc, achievement_id) == nullptr) {
    return openwow::ui::lua::LuaCall(state).PushNil().ResultCount();
  }
  return openwow::ui::lua::LuaCall(state).PushNumber(1u).ResultCount();
}

int LuaGetAchievementReward(lua_State* state) {
  const openwow::ui::lua::LuaCall arguments(state);
  const auto achievement_id = TruncateLuaNumberToSseI32(
      arguments.RequireNumber(
          1,
          "Usage: GetAchievementNumRewards(achievementID, rewardIndex)"));
  static_cast<void>(arguments.RequireNumber(
      2, "Usage: GetAchievementNumRewards(achievementID, rewardIndex)"));

  const auto* dbc = GetDbcLoader(state);
  const auto* achievement =
      dbc != nullptr ? FindVisibleAchievement(*dbc, achievement_id) : nullptr;
  if (achievement == nullptr) {
    return openwow::ui::lua::LuaCall(state).PushNil().ResultCount();
  }
  return openwow::ui::lua::LuaCall(state)
      .PushNumber(achievement->points)
      .ResultCount();
}

}

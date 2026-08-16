#include "openwow/game/achievements/adapters/lua/achievement_tracking_lua_api.h"

#include "openwow/game/achievements/application/tracked_achievement_state.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/lua_numeric.h"
#include "openwow/ui/lua_result_capacity.h"
#include "openwow/ui/runtime/lua/lua_call.h"

#include <cstdint>

namespace openwow::ui::game::detail {
namespace {

std::int32_t ReadTrackedAchievementId(lua_State* state) {
  const openwow::ui::lua::LuaCall arguments(state);
  if (!arguments.IsNumber(1)) {
    return 0;
  }
  return TruncateLuaNumberToSseI32(arguments.Number(1));
}

}

int LuaGetTrackedAchievements(lua_State* state) {
  const auto tracked_achievements =
      openwow::game::TrackedAchievementState::Get().GetTrackedAchievements();
  const int result_count = openwow::ui::ReserveLuaResultCapacity(
      state, tracked_achievements.size(), "tracked achievement IDs");
  openwow::ui::lua::LuaCall results(state);
  for (const auto id : tracked_achievements) {
    results.PushNumber(id.value);
  }
  return result_count;
}

int LuaAddTrackedAchievement(lua_State* state) {
  const auto achievement_id = ReadTrackedAchievementId(state);
  const auto* dbc = GetDbcLoader(state);
  if (achievement_id <= 0 || dbc == nullptr ||
      dbc->achievement().LookupEntry(
          static_cast<std::uint32_t>(achievement_id)) == nullptr) {
    openwow::ui::lua::LuaCall(state).UsageError(
        "Usage: AddTrackedAchievement(achievementID)");
  }

  openwow::game::TrackedAchievementState::Get().AddTrackedAchievement(
      openwow::game::AchievementId{
          static_cast<std::uint32_t>(achievement_id)});
  return 0;
}

int LuaRemoveTrackedAchievement(lua_State* state) {
  const auto achievement_id = ReadTrackedAchievementId(state);
  if (achievement_id == 0) {
    openwow::ui::lua::LuaCall(state).UsageError(
        "Usage: RemoveTrackedAchievement(achievementID)");
  }

  openwow::game::TrackedAchievementState::Get().RemoveTrackedAchievement(
      openwow::game::AchievementId{
          static_cast<std::uint32_t>(achievement_id)});
  return 0;
}

int LuaIsTrackedAchievement(lua_State* state) {
  const auto achievement_id = ReadTrackedAchievementId(state);
  if (achievement_id == 0) {
    return openwow::ui::lua::LuaCall(state).PushBoolean(false).ResultCount();
  }

  return openwow::ui::lua::LuaCall(state)
      .PushBoolean(
          openwow::game::TrackedAchievementState::Get().IsTrackedAchievement(
              openwow::game::AchievementId{
                  static_cast<std::uint32_t>(achievement_id)}))
      .ResultCount();
}

int LuaGetNumTrackedAchievements(lua_State* state) {
  return openwow::ui::lua::LuaCall(state)
      .PushNumber(openwow::game::TrackedAchievementState::Get()
                  .GetNumTrackedAchievements())
      .ResultCount();
}

}

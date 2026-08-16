#include "openwow/game/achievements/adapters/lua/achievement_navigation_lua_api.h"

#include "openwow/data/formats/dbc/dbc_entries_extended.h"
#include "openwow/game/achievements/model/achievement_state_types.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/lua_numeric.h"
#include "openwow/ui/runtime/lua/lua_call.h"

#include <cstdint>

namespace openwow::ui::game::detail {
namespace {

const openwow::data::dbc::AchievementEntry* FindNextAchievement(
    const openwow::data::dbc::DbcLoader& dbc,
    const std::uint32_t achievement_id) {
  for (const auto& achievement : dbc.achievement()) {
    if (achievement.parent_achievement == achievement_id) {
      return &achievement;
    }
  }
  return nullptr;
}

bool HasLocalCompletedAchievement(lua_State* state,
                                  const std::uint32_t achievement_id) {
  const auto* session = GetWorldSession(state);
  return session != nullptr &&
         session->achievements().FindCompletedAchievement(
             openwow::game::AchievementId{achievement_id}) != nullptr;
}

}

int LuaGetPreviousAchievement(lua_State* state) {
  const openwow::ui::lua::LuaCall arguments(state);
  const auto achievement_id = TruncateLuaNumberToSseI32(
      arguments.RequireNumber(
          1, "Usage: GetPreviousAchievement(achievementID)"));
  if (achievement_id < 0) {
    return 0;
  }

  const auto* dbc = GetDbcLoader(state);
  const auto* achievement =
      dbc != nullptr
          ? dbc->achievement().LookupEntry(
                static_cast<std::uint32_t>(achievement_id))
          : nullptr;
  if (achievement == nullptr || achievement->parent_achievement == 0) {
    return 0;
  }

  return openwow::ui::lua::LuaCall(state)
      .PushNumber(achievement->parent_achievement)
      .ResultCount();
}

int LuaGetNextAchievement(lua_State* state) {
  const openwow::ui::lua::LuaCall arguments(state);
  const auto achievement_id = static_cast<std::uint32_t>(
      TruncateLuaNumberToSseI32(
          arguments.RequireNumber(
              1, "Usage: GetNextAchievement(achievementID)")));

  const auto* dbc = GetDbcLoader(state);
  const auto* achievement =
      dbc != nullptr ? FindNextAchievement(*dbc, achievement_id) : nullptr;
  if (achievement == nullptr) {
    return 0;
  }

  openwow::ui::lua::LuaCall results(state);
  results.PushNumber(achievement->id);
  if (!HasLocalCompletedAchievement(state, achievement->id)) {
    return results.ResultCount();
  }
  return results.PushBoolean(true).ResultCount();
}

}

#include "openwow/game/achievements/adapters/lua/achievement_comparison_session_lua_api.h"

#include "openwow/data/formats/dbc/dbc_entries_extended.h"
#include "openwow/game/achievements/adapters/protocol/achievement_protocol.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/lua_numeric.h"
#include "openwow/ui/runtime/lua/lua_call.h"

#include <algorithm>
#include <string>

namespace openwow::ui::game::detail {

int LuaSetAchievementComparisonUnit(lua_State* state) {
  auto* session = GetWorldSession(state);
  if (session == nullptr) {
    return 0;
  }

  const openwow::ui::lua::LuaCall arguments(state);
  const auto unit_id = arguments.RequireString(
      1, "Usage: AddAchievementComparisonUnit(unitToken)");
  const auto guid = ResolveUnitId(session, std::string(unit_id));
  if (guid.IsEmpty()) {
    return 0;
  }

  session->achievements().SetComparisonUnit(guid);
  session->Send(
      openwow::game::achievement::protocol::BuildQueryInspectAchievements(
          guid));
  return openwow::ui::lua::LuaCall(state).PushNumber(1.0).ResultCount();
}

int LuaClearAchievementComparisonUnit(lua_State* state) {
  auto* session = GetWorldSession(state);
  if (session != nullptr) {
    session->achievements().ClearComparisonAchievementData();
  }
  return 0;
}

int LuaGetAchievementComparisonInfo(lua_State* state) {
  const openwow::ui::lua::LuaCall arguments(state);
  const auto achievement_id = openwow::ui::SaturateLuaNumberToU32(
      arguments.RequireNumber(
          1, "Usage: GetAchievementComparisonInfo(achievementID)"));

  auto* session = GetWorldSession(state);
  const auto* dbc = GetDbcLoader(state);
  if (session == nullptr || dbc == nullptr) {
    return 0;
  }

  if (!session->achievements().has_comparison_unit()) {
    return 0;
  }

  const auto& comparison_achievements =
      session->achievements().last_inspect().achievements;
  const auto achievement_it = std::find_if(
      comparison_achievements.begin(), comparison_achievements.end(),
      [achievement_id](
          const openwow::game::CompletedAchievement& achievement) {
        return achievement.id.value == achievement_id;
      });
  const auto* achievement =
      dbc->achievement().LookupEntry(achievement_id);
  if (achievement_it == comparison_achievements.end() ||
      achievement == nullptr) {
    return 0;
  }

  const auto completion_date =
      achievement_it->completion_date.ToWireValue();
  return openwow::ui::lua::LuaCall(state)
      .PushBoolean(true)
      .PushNumber(((completion_date >> 20) & 0xFu) + 1u)
      .PushNumber(((completion_date >> 14) & 0x3Fu) + 1u)
      .PushNumber((completion_date >> 24) & 0x1Fu)
      .PushNumber(achievement->flags)
      .ResultCount();
}

}

#include "openwow/game/achievements/adapters/lua/achievement_points_lua_api.h"

#include "openwow/data/formats/dbc/dbc_entries_extended.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/runtime/lua/lua_call.h"

#include <cstdint>

namespace openwow::ui::game::detail {
namespace {

template <typename AchievementRange>
std::uint32_t SumAchievementPoints(
    const openwow::data::dbc::DbcLoader& dbc,
    const AchievementRange& achievements) {
  std::uint32_t total_points = 0;
  for (const auto& achievement : achievements) {
    const auto* metadata =
        dbc.achievement().LookupEntry(achievement.id.value);
    if (metadata != nullptr) {
      total_points += metadata->points;
    }
  }
  return total_points;
}

}

int LuaGetTotalAchievementPoints(lua_State* state) {
  const auto* session = GetWorldSession(state);
  const auto* dbc = GetDbcLoader(state);
  const auto total_points =
      session != nullptr && dbc != nullptr
          ? SumAchievementPoints(
                *dbc, session->achievements().completed_list())
          : 0u;
  return openwow::ui::lua::LuaCall(state)
      .PushNumber(total_points)
      .ResultCount();
}

int LuaGetComparisonAchievementPoints(lua_State* state) {
  const auto* session = GetWorldSession(state);
  const auto* dbc = GetDbcLoader(state);
  const auto total_points =
      session != nullptr && dbc != nullptr
          ? SumAchievementPoints(
                *dbc,
                session->achievements().last_inspect().achievements)
          : 0u;
  return openwow::ui::lua::LuaCall(state)
      .PushNumber(total_points)
      .ResultCount();
}

}

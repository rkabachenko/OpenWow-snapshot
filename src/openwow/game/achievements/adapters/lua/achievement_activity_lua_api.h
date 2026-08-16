#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaGetLatestCompletedAchievements(lua_State* state);
int LuaGetLatestUpdatedStats(lua_State* state);
int LuaGetLatestCompletedComparisonAchievements(lua_State* state);
int LuaGetLatestUpdatedComparisonStats(lua_State* state);

}

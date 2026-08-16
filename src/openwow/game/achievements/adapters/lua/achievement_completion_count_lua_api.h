#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaGetNumCompletedAchievements(lua_State* state);
int LuaGetNumComparisonCompletedAchievements(lua_State* state);

}

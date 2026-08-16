#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaSetAchievementComparisonUnit(lua_State* state);
int LuaClearAchievementComparisonUnit(lua_State* state);
int LuaGetAchievementComparisonInfo(lua_State* state);

}

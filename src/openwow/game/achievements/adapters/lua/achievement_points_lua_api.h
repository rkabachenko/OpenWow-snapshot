#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaGetTotalAchievementPoints(lua_State* state);
int LuaGetComparisonAchievementPoints(lua_State* state);

}

#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaGetStatistic(lua_State* state);
int LuaGetComparisonStatistic(lua_State* state);

}

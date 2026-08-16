#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaGetCategoryNumAchievements(lua_State* state);
int LuaGetComparisonCategoryNumAchievements(lua_State* state);

}

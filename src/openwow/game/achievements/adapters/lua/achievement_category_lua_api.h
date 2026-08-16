#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaGetCategoryList(lua_State* state);
int LuaGetStatisticsCategoryList(lua_State* state);
int LuaGetCategoryInfo(lua_State* state);
int LuaGetAchievementCategory(lua_State* state);

}

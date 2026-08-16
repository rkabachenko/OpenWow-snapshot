#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaGetTrackedAchievements(lua_State* state);
int LuaAddTrackedAchievement(lua_State* state);
int LuaRemoveTrackedAchievement(lua_State* state);
int LuaIsTrackedAchievement(lua_State* state);
int LuaGetNumTrackedAchievements(lua_State* state);

}

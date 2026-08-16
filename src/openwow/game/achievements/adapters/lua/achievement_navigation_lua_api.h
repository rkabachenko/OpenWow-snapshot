#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaGetPreviousAchievement(lua_State* state);
int LuaGetNextAchievement(lua_State* state);

}

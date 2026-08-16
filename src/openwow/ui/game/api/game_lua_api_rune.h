
#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaGetRuneCount(lua_State* L);
int LuaGetRuneCooldown(lua_State* L);
int LuaGetRuneType(lua_State* L);

}

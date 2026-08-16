#pragma once

struct lua_State;

namespace openwow::ui::runtime::diagnostics {

int LuaDebugProfileStart(lua_State* state);
int LuaDebugProfileStop(lua_State* state);

}

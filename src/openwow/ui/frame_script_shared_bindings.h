
#pragma once

extern "C" {
#include <lua.hpp>
}

namespace openwow::ui {

void FrameScript_RegisterSharedUiLuaBindings(lua_State *L);

void FrameScript_UnregisterSharedUiLuaBindings(lua_State *L);

}

#pragma once

extern "C" {
#include <lua.hpp>
}

namespace openwow::ui::game::frame_api {

int LuaFrame_RegisterEvent(lua_State* L);
int LuaFrame_UnregisterEvent(lua_State* L);
int LuaFrame_RegisterAllEvents(lua_State* L);
int LuaFrame_UnregisterAllEvents(lua_State* L);
int LuaFrame_IsEventRegistered(lua_State* L);

}

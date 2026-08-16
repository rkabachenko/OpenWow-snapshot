#pragma once

struct lua_State;

namespace openwow::ui::game::frame_api {

void ApplyBaseFrameMethods(lua_State* lua);
void ApplyCommonFrameMethods(lua_State* lua);
void ApplyFrameStateMethods(lua_State* lua);

int LuaFrame_GetAttribute(lua_State* lua);
int LuaFrame_SetAttribute(lua_State* lua);
int LuaScriptObject_SetParent(lua_State* lua);
int LuaScriptObject_GetParent(lua_State* lua);
bool StoreLuaFrameScaleAndInvalidate(lua_State* lua, int frame_index,
                                     float scale);
int LuaFrame_SetFrameStrata(lua_State* lua);
int LuaFrame_GetFrameStrata(lua_State* lua);
int LuaFrame_SetFrameLevel(lua_State* lua);
int LuaFrame_GetFrameLevel(lua_State* lua);

}

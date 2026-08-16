#pragma once

struct lua_State;

namespace openwow::ui::game::frame_api {

void ApplyLayoutFrameMethods(lua_State* lua);
void ApplySimpleFrameLayoutMethods(lua_State* lua);
[[nodiscard]] bool LuaFrameMatchesObjectType(const char* object_type,
                                             const char* query);
[[nodiscard]] const char* GetLuaFrameRuntimeTypeName(lua_State* lua,
                                                     int self_index);
int LuaFrame_Raise(lua_State* lua);
int LuaFrame_Lower(lua_State* lua);

}

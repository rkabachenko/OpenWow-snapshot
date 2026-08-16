#pragma once

struct lua_State;

namespace openwow::ui::game::frame_api {

int LuaRegion_IsDragging(lua_State* lua);

int LuaRegion_IsMouseOver(lua_State* lua);
int SetTypedLuaScriptRegionShown(lua_State* lua, const char* expected_type,
                                bool shown);

}

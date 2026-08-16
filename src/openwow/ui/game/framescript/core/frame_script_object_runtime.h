#pragma once

struct lua_State;

namespace openwow::ui::game::frame_api {

int ValidateFrameScriptSelf(lua_State* lua);
int ValidateFrameObjectSelf(lua_State* lua, const char* expected_type);
int ValidateFrameSelf(lua_State* lua);
int ValidateFrameLikeObjectSelf(lua_State* lua);

}

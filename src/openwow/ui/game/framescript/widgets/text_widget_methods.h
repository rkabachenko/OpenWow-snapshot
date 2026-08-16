#pragma once

struct lua_State;

namespace openwow::ui::game::frame_api {

void ApplyMessageFrameTextOverrides(lua_State* lua);
void ApplySimpleHTMLMethods(lua_State* lua);

}

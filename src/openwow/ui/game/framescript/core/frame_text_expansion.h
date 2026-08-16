#pragma once

struct lua_State;

namespace openwow::ui::game::frame_api {

void PushExpandedSimpleRenderScriptText(lua_State* lua, const char* text);

}

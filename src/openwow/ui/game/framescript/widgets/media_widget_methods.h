#pragma once

struct lua_State;

namespace openwow::ui::game::frame_api {

void ApplyMovieFrameMethods(lua_State* lua);
void ApplyMinimapMethods(lua_State* lua);
void ApplyMinimapTextureMethods(lua_State* lua);

}

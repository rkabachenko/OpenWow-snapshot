#pragma once

struct lua_State;

namespace openwow::ui::game::frame_api {

void ApplyPerObjectGameTooltipMethods(lua_State* lua, int object_index);

void UpdateLuaTooltipObjects(lua_State* lua, float elapsed_seconds);

}

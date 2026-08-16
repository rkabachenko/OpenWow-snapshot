#pragma once

struct lua_State;

namespace openwow::ui::game::frame_api {

void ApplyGameTooltipMethods(lua_State* lua);
void ApplyGameTooltipContentMethods(lua_State* lua);
void SynchronizeTooltipFontStringLayouts(lua_State* lua, int tooltip_index);

}

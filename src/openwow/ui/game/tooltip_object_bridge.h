#pragma once

struct lua_State;
namespace openwow::game { class ObjectGuid; }

namespace openwow::ui::game::frame_api {

void ApplyPerObjectGameTooltipMethods(lua_State* lua, int object_index);

void UpdateWorldMouseoverTooltip(lua_State* lua, openwow::game::ObjectGuid guid);

void UpdateLuaTooltipObjects(lua_State* lua, float elapsed_seconds);

}

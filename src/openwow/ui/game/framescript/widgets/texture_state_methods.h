#pragma once

struct lua_State;
namespace openwow::ui::game::frame_api {
void ApplyTextureStateMethods(lua_State* lua, int table_index);
int LuaTexture_SetGradient(lua_State* lua);
int LuaTexture_SetGradientAlpha(lua_State* lua);
}

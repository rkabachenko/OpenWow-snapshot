#pragma once

struct lua_State;

namespace openwow::ui::game::frame_api {

void ApplyModelMethods(lua_State* lua);
void ApplyModelLightMethods(lua_State* lua);
void ApplyPlayerModelSpecificMethods(lua_State* lua);
void ApplyPlayerModelIconTextureMethods(lua_State* lua);
void ApplyDressUpModelSpecificMethods(lua_State* lua);
void ApplyTabardModelSpecificMethods(lua_State* lua);

}

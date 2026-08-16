
#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaNotifyInspect(lua_State* L);
int LuaClearInspectPlayer(lua_State* L);
int LuaCanInspect(lua_State* L);
int LuaHasInspectHonorData(lua_State* L);
int LuaRequestInspectHonorData(lua_State* L);
int LuaGetInspectHonorData(lua_State* L);

}

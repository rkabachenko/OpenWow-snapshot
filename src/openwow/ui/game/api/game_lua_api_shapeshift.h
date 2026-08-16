
#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaGetNumShapeshiftForms(lua_State* L);
int LuaGetShapeshiftFormInfo(lua_State* L);
int LuaGetShapeshiftForm(lua_State* L);
int LuaCastShapeshiftForm(lua_State* L);
int LuaCancelShapeshiftForm(lua_State* L);

}

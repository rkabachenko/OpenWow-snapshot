#pragma once

#include "openwow/ui/runtime/lua/lua_composition.h"

struct lua_State;

namespace openwow::ui::game::detail {

int LuaGetTotemInfo(lua_State* L);
int LuaGetTotemTimeLeft(lua_State* L);
int LuaDestroyTotem(lua_State* L);

int LuaGetMultiCastTotemSpells(lua_State* L);
int LuaSetMultiCastSpell(lua_State* L);

[[nodiscard]] openwow::ui::lua::NativeBindingCatalog TotemConstantCatalog();

}

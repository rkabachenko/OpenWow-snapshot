
#pragma once

#include "openwow/ui/runtime/lua/lua_composition.h"

#include <cstdint>

struct lua_State;

namespace openwow::game::actions::macros::adapters::lua {

[[nodiscard]] openwow::ui::lua::NativeBindingCatalog
MacroConstantCatalog();

int LuaGetNumMacros(lua_State* L);
int LuaGetMacroInfo(lua_State* L);
int LuaGetMacroBody(lua_State* L);
int LuaCreateMacro(lua_State* L);
int LuaEditMacro(lua_State* L);
int LuaDeleteMacro(lua_State* L);
int LuaGetMacroIndexByName(lua_State* L);
int LuaGetRunningMacro(lua_State* L);
int LuaSetMacroItem(lua_State* L);
int LuaGetMacroItem(lua_State* L);
int LuaSetMacroSpell(lua_State* L);
int LuaGetNumMacroIcons(lua_State* L);
int LuaGetNumMacroItemIcons(lua_State* L);
int LuaRunMacro(lua_State* L);
int LuaRunMacroText(lua_State* L);
int LuaSecureCmdOptionParse(lua_State* L);

int LuaGetMacroIconInfo(lua_State* L);
int LuaGetMacroSpell(lua_State* L);
int LuaStopMacro(lua_State* L);
int LuaGetMacroItemIconInfo(lua_State* L);
int LuaGetRunningMacroButton(lua_State* L);

void RunMacroByIndex(lua_State* L, std::uint32_t macro_index,
                     const char* button = nullptr);

}

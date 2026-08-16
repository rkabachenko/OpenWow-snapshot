#pragma once

#include "openwow/ui/runtime/lua/lua_composition.h"

struct lua_State;

namespace openwow::ui::game::detail {

int LuaCombatLog_Object_IsA(lua_State* L);
int LuaCombatLogClearEntries(lua_State* L);
int LuaCombatLogResetFilter(lua_State* L);

int LuaCombatLogAddFilter(lua_State* L);
int LuaCombatLogSetCurrentEntry(lua_State* L);
int LuaCombatLogSetRetentionTime(lua_State* L);
int LuaCombatTextSetActiveUnit(lua_State* L);
int LuaCombatLogAdvanceEntry(lua_State* L);
int LuaCombatLogGetCurrentEntry(lua_State* L);
int LuaCombatLogGetNumEntries(lua_State* L);
int LuaCombatLogGetRetentionTime(lua_State* L);

[[nodiscard]] openwow::ui::lua::NativeBindingCatalog
CombatLogConstantCatalog();

}

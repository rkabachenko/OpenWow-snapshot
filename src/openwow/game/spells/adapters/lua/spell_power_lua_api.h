#pragma once

#include "openwow/ui/runtime/lua/lua_composition.h"

struct lua_State;

namespace openwow::ui::game::detail {

int LuaGetSpellLink(lua_State* L);
int LuaGetSpellName(lua_State* L);
int LuaGetSpellTexture(lua_State* L);
int LuaGetSpellCritChanceFromIntellect(lua_State* L);
int LuaIsUsableSpell(lua_State* L);
int LuaIsPassiveSpell(lua_State* L);
int LuaIsHarmfulSpell(lua_State* L);
int LuaIsHelpfulSpell(lua_State* L);
int LuaIsConsumableSpell(lua_State* L);
int LuaIsSpellInRange(lua_State* L);
int LuaSpellHasRange(lua_State* L);
int LuaGetSpellAutocast(lua_State* L);
int LuaEnableSpellAutocast(lua_State* L);
int LuaDisableSpellAutocast(lua_State* L);

int LuaSpellTargetItem(lua_State* L);
int LuaIsSelectedSpell(lua_State* L);

[[nodiscard]] openwow::ui::lua::NativeBindingCatalog SpellPowerConstantCatalog();

}

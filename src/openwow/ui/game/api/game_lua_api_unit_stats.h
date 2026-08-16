
#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaUnitStat(lua_State* L);
int LuaUnitDamage(lua_State* L);
int LuaUnitRangedDamage(lua_State* L);
int LuaUnitAttackSpeed(lua_State* L);
int LuaUnitAttackPower(lua_State* L);
int LuaUnitRangedAttackPower(lua_State* L);
int LuaUnitArmor(lua_State* L);
int LuaUnitDefense(lua_State* L);
int LuaUnitAttackBothHands(lua_State* L);

int LuaGetCritChance(lua_State* L);
int LuaGetSpellPenetration(lua_State* L);

}

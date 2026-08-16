
#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaHasPetUI(lua_State* L);
int LuaGetPetActionsUsable(lua_State* L);
int LuaGetPetActionInfo(lua_State* L);

int LuaPetAttack(lua_State* L);
int LuaPetFollow(lua_State* L);
int LuaPetPassiveMode(lua_State* L);
int LuaPetDefensiveMode(lua_State* L);
int LuaPetAggressiveMode(lua_State* L);
int LuaPetAbandon(lua_State* L);
int LuaPetRename(lua_State* L);
int LuaPetDismiss(lua_State* L);

int LuaPetCanBeAbandoned(lua_State* L);
int LuaPetCanBeRenamed(lua_State* L);
int LuaGetPetExperience(lua_State* L);
int LuaGetPetFoodTypes(lua_State* L);
int LuaGetPetHappiness(lua_State* L);
int LuaUnitCreatureFamily(lua_State* L);
int LuaGetPetIcon(lua_State* L);
int LuaGetPetTalentTree(lua_State* L);
int LuaGetPetTimeRemaining(lua_State* L);

int LuaGetStablePetInfo(lua_State* L);
int LuaGetNumStableSlots(lua_State* L);
int LuaSetPetStablePaperdoll(lua_State* L);
int LuaStablePet(lua_State* L);
int LuaClickStablePet(lua_State* L);

int LuaGetPetActionCooldown(lua_State* L);

int LuaBuyStableSlot(lua_State* L);
int LuaCastPetAction(lua_State* L);
int LuaClosePetStables(lua_State* L);
int LuaPetStopAttack(lua_State* L);
int LuaPetWait(lua_State* L);
int LuaPickupStablePet(lua_State* L);
int LuaGetPetSpellBonusDamage(lua_State* L);
int LuaGetSelectedStablePet(lua_State* L);
int LuaIsPetAttackAction(lua_State* L);
int LuaIsPetAttackActive(lua_State* L);
int LuaPetCanBeDismissed(lua_State* L);
int LuaPetHasActionBar(lua_State* L);

int LuaTogglePetAutocast(lua_State* L);
int LuaGetPetActionSlotUsable(lua_State* L);
int LuaUnstablePet(lua_State* L);

}

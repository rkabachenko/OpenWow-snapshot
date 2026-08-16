#include "openwow/game/actors/pets/adapters/lua/pet_lua_bindings.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/runtime/lua/lua_composition.h"

extern "C" {
#include <lua.hpp>
}

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
int LuaGetStablePetInfo(lua_State* L);
int LuaGetNumStableSlots(lua_State* L);
int LuaSetPetStablePaperdoll(lua_State* L);
int LuaGetPetHappiness(lua_State* L);
int LuaUnitCreatureFamily(lua_State* L);
int LuaGetPetIcon(lua_State* L);
int LuaGetPetTalentTree(lua_State* L);
int LuaGetPetTimeRemaining(lua_State* L);
int LuaGetPetActionCooldown(lua_State* L);
int LuaTogglePetAutocast(lua_State* L);
int LuaGetNumCompanions(lua_State* L);
int LuaGetCompanionInfo(lua_State* L);
int LuaGetCompanionCooldown(lua_State* L);
int LuaCallCompanion(lua_State* L);
int LuaDismissCompanion(lua_State* L);
int LuaSummonRandomCritter(lua_State* L);
int LuaGetNumStablePets(lua_State* L);
int LuaStablePet(lua_State* L);
int LuaClickStablePet(lua_State* L);
int LuaGetStablePetFoodTypes(lua_State* L);
int LuaBuyStableSlot(lua_State* L);
int LuaCastPetAction(lua_State* L);
int LuaClosePetStables(lua_State* L);
int LuaGetPetActionSlotUsable(lua_State* L);
int LuaGetPetSpellBonusDamage(lua_State* L);
int LuaGetSelectedStablePet(lua_State* L);
int LuaIsPetAttackAction(lua_State* L);
int LuaIsPetAttackActive(lua_State* L);
int LuaPetCanBeDismissed(lua_State* L);
int LuaPetHasActionBar(lua_State* L);
int LuaPetStopAttack(lua_State* L);
int LuaPetWait(lua_State* L);
int LuaPickupStablePet(lua_State* L);
int LuaPickupCompanion(lua_State* L);
int LuaUnstablePet(lua_State* L);

}

namespace openwow::ui::game {

using namespace detail;

namespace {

constexpr openwow::ui::LuaGlobalBinding kPetLuaBindings[] = {
    {"HasPetUI", LuaHasPetUI},
    {"GetPetActionsUsable", LuaGetPetActionsUsable},
    {"GetPetActionInfo", LuaGetPetActionInfo},
    {"PetAttack", LuaPetAttack},
    {"PetFollow", LuaPetFollow},
    {"PetPassiveMode", LuaPetPassiveMode},
    {"PetDefensiveMode", LuaPetDefensiveMode},
    {"PetAggressiveMode", LuaPetAggressiveMode},
    {"PetAbandon", LuaPetAbandon},
    {"PetRename", LuaPetRename},
    {"PetDismiss", LuaPetDismiss},
    {"PetCanBeAbandoned", LuaPetCanBeAbandoned},
    {"PetCanBeRenamed", LuaPetCanBeRenamed},
    {"GetPetExperience", LuaGetPetExperience},
    {"GetPetFoodTypes", LuaGetPetFoodTypes},
    {"GetStablePetInfo", LuaGetStablePetInfo},
    {"GetNumStableSlots", LuaGetNumStableSlots},
    {"SetPetStablePaperdoll", LuaSetPetStablePaperdoll},
    {"GetPetHappiness", LuaGetPetHappiness},
    {"UnitCreatureFamily", LuaUnitCreatureFamily},
    {"GetPetIcon", LuaGetPetIcon},
    {"GetPetTalentTree", LuaGetPetTalentTree},
    {"GetPetTimeRemaining", LuaGetPetTimeRemaining},
    {"GetPetActionCooldown", LuaGetPetActionCooldown},
    {"TogglePetAutocast", LuaTogglePetAutocast},
    {"GetNumCompanions", LuaGetNumCompanions},
    {"GetCompanionInfo", LuaGetCompanionInfo},
    {"GetCompanionCooldown", LuaGetCompanionCooldown},
    {"CallCompanion", LuaCallCompanion},
    {"DismissCompanion", LuaDismissCompanion},
    {"SummonRandomCritter", LuaSummonRandomCritter},
    {"GetNumStablePets", LuaGetNumStablePets},
    {"StablePet", LuaStablePet},
    {"ClickStablePet", LuaClickStablePet},
    {"GetStablePetFoodTypes", LuaGetStablePetFoodTypes},
    {"BuyStableSlot", LuaBuyStableSlot},
    {"CastPetAction", LuaCastPetAction},
    {"ClosePetStables", LuaClosePetStables},
    {"GetPetActionSlotUsable", LuaGetPetActionSlotUsable},
    {"GetPetSpellBonusDamage", LuaGetPetSpellBonusDamage},
    {"GetSelectedStablePet", LuaGetSelectedStablePet},
    {"IsPetAttackAction", LuaIsPetAttackAction},
    {"IsPetAttackActive", LuaIsPetAttackActive},
    {"PetCanBeDismissed", LuaPetCanBeDismissed},
    {"PetHasActionBar", LuaPetHasActionBar},
    {"PetStopAttack", LuaPetStopAttack},
    {"PetWait", LuaPetWait},
    {"PickupStablePet", LuaPickupStablePet},
    {"PickupCompanion", LuaPickupCompanion},
    {"UnstablePet", LuaUnstablePet},
};

}

openwow::ui::lua::NativeBindingCatalog PetNativeBindingCatalog() {
  return openwow::ui::lua::NativeFunctionCatalog(
      "game.actors.pets", openwow::ui::lua::BindingScope::kWorld, kPetLuaBindings);
}

}

#include "openwow/game/spells/adapters/lua/spell_lua_bindings.h"
#include "openwow/ui/game/api/game_lua_api_cast.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/runtime/lua/lua_composition.h"

namespace openwow::ui::game::detail {

int LuaGetSpellInfo(lua_State* L);
int LuaGetSpellCooldown(lua_State* L);
int LuaGetSpellCount(lua_State* L);
int LuaIsSpellKnown(lua_State* L);
int LuaGetNumSpellTabs(lua_State* L);
int LuaGetSpellTabInfo(lua_State* L);
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
int LuaGetNumShapeshiftForms(lua_State* L);
int LuaGetShapeshiftFormInfo(lua_State* L);
int LuaGetShapeshiftForm(lua_State* L);
int LuaCastShapeshiftForm(lua_State* L);
int LuaCancelShapeshiftForm(lua_State* L);
int LuaCancelUnitBuff(lua_State* L);
int LuaGetSpellBonusDamage(lua_State* L);
int LuaGetSpellBonusHealing(lua_State* L);
int LuaPickupSpell(lua_State* L);
int LuaGetRuneCount(lua_State* L);
int LuaGetRuneCooldown(lua_State* L);
int LuaGetRuneType(lua_State* L);
int LuaGetTotemInfo(lua_State* L);
int LuaGetTotemTimeLeft(lua_State* L);
int LuaDestroyTotem(lua_State* L);
int LuaHasPetSpells(lua_State* L);
int LuaGetMultiCastTotemSpells(lua_State* L);
int LuaGetShapeshiftFormCooldown(lua_State* L);
int LuaSetMultiCastSpell(lua_State* L);
int LuaToggleSpellAutocast(lua_State* L);
int LuaFindSpellBookSlotByID(lua_State* L);
int LuaGetKnownSlotFromHighestRankSlot(lua_State* L);
int LuaSpellTargetItem(lua_State* L);
int LuaIsSelectedSpell(lua_State* L);

}

namespace openwow::ui::game {

using namespace detail;

namespace {

constexpr openwow::ui::LuaGlobalBinding kSpellLuaBindings[] = {
    {"SpellCanTargetUnit", kSpellCanTargetUnit.handler},
    {"SpellTargetUnit", kSpellTargetUnit.handler},
    {"IsCurrentSpell", kIsCurrentSpell.handler},
    {"IsAutoRepeatSpell", kIsAutoRepeatSpell.handler},
    {"IsAttackSpell", kIsAttackSpell.handler},
    {"GetSpellInfo", LuaGetSpellInfo},
    {"GetSpellCooldown", LuaGetSpellCooldown},
    {"GetSpellCount", LuaGetSpellCount},
    {"IsSpellKnown", LuaIsSpellKnown},
    {"GetNumSpellTabs", LuaGetNumSpellTabs},
    {"GetSpellTabInfo", LuaGetSpellTabInfo},
    {"CastSpellByName", kCastSpellByName.handler},
    {"CastSpellByID", kCastSpellByID.handler},
    {"SpellStopCasting", kSpellStopCasting.handler},
    {"SpellIsTargeting", kSpellIsTargeting.handler},
    {"SpellStopTargeting", kSpellStopTargeting.handler},
    {"GetSpellLink", LuaGetSpellLink},
    {"GetSpellName", LuaGetSpellName},
    {"GetSpellTexture", LuaGetSpellTexture},
    {"GetSpellCritChanceFromIntellect", LuaGetSpellCritChanceFromIntellect},
    {"IsUsableSpell", LuaIsUsableSpell},
    {"IsPassiveSpell", LuaIsPassiveSpell},
    {"IsHarmfulSpell", LuaIsHarmfulSpell},
    {"IsHelpfulSpell", LuaIsHelpfulSpell},
    {"IsConsumableSpell", LuaIsConsumableSpell},
    {"IsSpellInRange", LuaIsSpellInRange},
    {"SpellHasRange", LuaSpellHasRange},
    {"GetSpellAutocast", LuaGetSpellAutocast},
    {"EnableSpellAutocast", LuaEnableSpellAutocast},
    {"DisableSpellAutocast", LuaDisableSpellAutocast},
    {"GetNumShapeshiftForms", LuaGetNumShapeshiftForms},
    {"GetShapeshiftFormInfo", LuaGetShapeshiftFormInfo},
    {"GetShapeshiftForm", LuaGetShapeshiftForm},
    {"CastShapeshiftForm", LuaCastShapeshiftForm},
    {"CancelShapeshiftForm", LuaCancelShapeshiftForm},
    {"CancelUnitBuff", LuaCancelUnitBuff},
    {"GetSpellBonusDamage", LuaGetSpellBonusDamage},
    {"GetSpellBonusHealing", LuaGetSpellBonusHealing},
    {"PickupSpell", LuaPickupSpell},
    {"GetRuneCount", LuaGetRuneCount},
    {"GetRuneCooldown", LuaGetRuneCooldown},
    {"GetRuneType", LuaGetRuneType},
    {"GetTotemInfo", LuaGetTotemInfo},
    {"GetTotemTimeLeft", LuaGetTotemTimeLeft},
    {"DestroyTotem", LuaDestroyTotem},
    {"HasPetSpells", LuaHasPetSpells},
    {"CastSpell", kCastSpell.handler},
    {"GetMultiCastTotemSpells", LuaGetMultiCastTotemSpells},
    {"GetShapeshiftFormCooldown", LuaGetShapeshiftFormCooldown},
    {"SetMultiCastSpell", LuaSetMultiCastSpell},
    {"SpellCanTargetItem", kSpellCanTargetItem.handler},
    {"SpellCanTargetGlyph", kSpellCanTargetGlyph.handler},
    {"ToggleSpellAutocast", LuaToggleSpellAutocast},
    {"FindSpellBookSlotByID", LuaFindSpellBookSlotByID},
    {"GetKnownSlotFromHighestRankSlot", LuaGetKnownSlotFromHighestRankSlot},
    {"SpellTargetItem", LuaSpellTargetItem},
    {"IsSelectedSpell", LuaIsSelectedSpell},
};

}

openwow::ui::lua::NativeBindingCatalog SpellNativeBindingCatalog() {
  return openwow::ui::lua::NativeFunctionCatalog(
      "game.spells", openwow::ui::lua::BindingScope::kWorld,
      kSpellLuaBindings);
}

}

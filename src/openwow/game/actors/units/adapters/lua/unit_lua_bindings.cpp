#include "openwow/game/actors/units/adapters/lua/unit_lua_bindings.h"
#include "openwow/ui/game/api/game_lua_api_cast.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/runtime/lua/lua_composition.h"

namespace openwow::ui::game::detail {

int LuaUnitName(lua_State* L);
int LuaUnitLevel(lua_State* L);
int LuaUnitClass(lua_State* L);
int LuaUnitRace(lua_State* L);
int LuaUnitSex(lua_State* L);
int LuaUnitHealth(lua_State* L);
int LuaUnitHealthMax(lua_State* L);
int LuaUnitPower(lua_State* L);
int LuaUnitPowerMax(lua_State* L);
int LuaUnitMana(lua_State* L);
int LuaUnitManaMax(lua_State* L);
int LuaUnitPowerType(lua_State* L);
int LuaUnitExists(lua_State* L);
int LuaUnitIsDeadOrGhost(lua_State* L);
int LuaUnitIsDead(lua_State* L);
int LuaUnitIsPlayer(lua_State* L);
int LuaUnitIsEnemy(lua_State* L);
int LuaUnitIsFriend(lua_State* L);
int LuaUnitIsUnit(lua_State* L);
int LuaUnitIsConnected(lua_State* L);
int LuaUnitIsAFK(lua_State* L);
int LuaUnitIsDND(lua_State* L);
int LuaUnitIsCorpse(lua_State* L);
int LuaUnitIsGhost(lua_State* L);
int LuaUnitIsPVP(lua_State* L);
int LuaUnitIsPVPFreeForAll(lua_State* L);
int LuaUnitPlayerControlled(lua_State* L);
int LuaUnitCanAttack(lua_State* L);
int LuaUnitCanCooperate(lua_State* L);
int LuaUnitBuff(lua_State* L);
int LuaUnitDebuff(lua_State* L);
int LuaUnitAura(lua_State* L);
int LuaUnitGUID(lua_State* L);
int LuaGetPlayerInfoByGUID(lua_State* L);
int LuaUnitFactionGroup(lua_State* L);
int LuaUnitClassification(lua_State* L);
int LuaUnitCreatureType(lua_State* L);
int LuaUnitXP(lua_State* L);
int LuaUnitXPMax(lua_State* L);
int LuaUnitReaction(lua_State* L);
int LuaUnitThreatSituation(lua_State* L);
int LuaUnitDetailedThreatSituation(lua_State* L);
int LuaUnitGroupRolesAssigned(lua_State* L);
int LuaUnitIsCharmed(lua_State* L);
int LuaUnitIsPossessed(lua_State* L);
int LuaUnitOnTaxi(lua_State* L);
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
int LuaUnitHasRelicSlot(lua_State* L);
int LuaTargetUnit(lua_State* L);
int LuaClearTarget(lua_State* L);
int LuaAssistUnit(lua_State* L);
int LuaFocusUnit(lua_State* L);
int LuaClearFocus(lua_State* L);
int LuaUnitInParty(lua_State* L);
int LuaUnitInRaid(lua_State* L);
int LuaGetUnitSpeed(lua_State* L);
int LuaUnitResistance(lua_State* L);
int LuaGetUnitMaxHealthModifier(lua_State* L);
int LuaCheckInteractDistance(lua_State* L);
int LuaUnitIsTappedByPlayer(lua_State* L);
int LuaUnitIsTappedByAllThreatList(lua_State* L);
int LuaUnitPlayerOrPetInParty(lua_State* L);
int LuaUnitPlayerOrPetInRaid(lua_State* L);
int LuaUnitIsVisible(lua_State* L);
int LuaGetThreatStatusColor(lua_State* L);
int LuaGetPlayerFacing(lua_State* L);
int LuaUnitIsRaidOfficer(lua_State* L);
int LuaFollowUnit(lua_State* L);
int LuaUnitAffectingCombat(lua_State* L);
int LuaGetArmorPenetration(lua_State* L);
int LuaUnitInRange(lua_State* L);
int LuaDeclineName(lua_State* L);
int LuaGetAttackPowerForStat(lua_State* L);
int LuaGetPowerRegen(lua_State* L);
int LuaGetResSicknessDuration(lua_State* L);
int LuaGetUnitHealthModifier(lua_State* L);
int LuaGetUnitPowerModifier(lua_State* L);
int LuaTargetLastEnemy(lua_State* L);
int LuaTargetLastFriend(lua_State* L);
int LuaTargetLastTarget(lua_State* L);
int LuaTargetNearest(lua_State* L);
int LuaTargetNearestEnemy(lua_State* L);
int LuaTargetNearestEnemyPlayer(lua_State* L);
int LuaTargetNearestFriend(lua_State* L);
int LuaTargetNearestFriendPlayer(lua_State* L);
int LuaTargetNearestPartyMember(lua_State* L);
int LuaTargetNearestRaidMember(lua_State* L);
int LuaUnitCanAssist(lua_State* L);
int LuaUnitCharacterPoints(lua_State* L);
int LuaUnitInBattleground(lua_State* L);
int LuaUnitIsFeignDeath(lua_State* L);
int LuaUnitIsTapped(lua_State* L);
int LuaUnitPVPName(lua_State* L);
int LuaUnitClassBase(lua_State* L);
int LuaUnitIsSameServer(lua_State* L);
int LuaUnitIsTrivial(lua_State* L);
int LuaUnitPVPRank(lua_State* L);
int LuaUnitRangedAttack(lua_State* L);
int LuaUnitSelectionColor(lua_State* L);
int LuaUnitIsInMyGuild(lua_State* L);
int LuaGetUnitPitch(lua_State* L);
int LuaInteractUnit(lua_State* L);
int LuaTargetDirectionEnemy(lua_State* L);
int LuaTargetDirectionFriend(lua_State* L);
int LuaTargetDirectionFinished(lua_State* L);
int LuaTargetTotem(lua_State* L);

}

namespace openwow::ui::game {

using namespace detail;

namespace {

constexpr openwow::ui::LuaGlobalBinding kUnitLuaBindings[] = {
    {"UnitName", LuaUnitName},
    {"UnitLevel", LuaUnitLevel},
    {"UnitClass", LuaUnitClass},
    {"UnitRace", LuaUnitRace},
    {"UnitSex", LuaUnitSex},
    {"UnitHealth", LuaUnitHealth},
    {"UnitHealthMax", LuaUnitHealthMax},
    {"UnitPower", LuaUnitPower},
    {"UnitPowerMax", LuaUnitPowerMax},
    {"UnitMana", LuaUnitMana},
    {"UnitManaMax", LuaUnitManaMax},
    {"UnitPowerType", LuaUnitPowerType},
    {"UnitExists", LuaUnitExists},
    {"UnitIsDeadOrGhost", LuaUnitIsDeadOrGhost},
    {"UnitIsDead", LuaUnitIsDead},
    {"UnitIsPlayer", LuaUnitIsPlayer},
    {"UnitIsEnemy", LuaUnitIsEnemy},
    {"UnitIsFriend", LuaUnitIsFriend},
    {"UnitIsUnit", LuaUnitIsUnit},
    {"UnitIsConnected", LuaUnitIsConnected},
    {"UnitIsAFK", LuaUnitIsAFK},
    {"UnitIsDND", LuaUnitIsDND},
    {"UnitIsCorpse", LuaUnitIsCorpse},
    {"UnitIsGhost", LuaUnitIsGhost},
    {"UnitIsPVP", LuaUnitIsPVP},
    {"UnitIsPVPFreeForAll", LuaUnitIsPVPFreeForAll},
    {"UnitPlayerControlled", LuaUnitPlayerControlled},
    {"UnitCanAttack", LuaUnitCanAttack},
    {"UnitCanCooperate", LuaUnitCanCooperate},
    {"UnitBuff", LuaUnitBuff},
    {"UnitDebuff", LuaUnitDebuff},
    {"UnitAura", LuaUnitAura},
    {"UnitGUID", LuaUnitGUID},
    {"GetPlayerInfoByGUID", LuaGetPlayerInfoByGUID},
    {"UnitFactionGroup", LuaUnitFactionGroup},
    {"UnitClassification", LuaUnitClassification},
    {"UnitCreatureType", LuaUnitCreatureType},
    {"UnitXP", LuaUnitXP},
    {"UnitXPMax", LuaUnitXPMax},
    {"UnitReaction", LuaUnitReaction},
    {"UnitThreatSituation", LuaUnitThreatSituation},
    {"UnitDetailedThreatSituation", LuaUnitDetailedThreatSituation},
    {"UnitGroupRolesAssigned", LuaUnitGroupRolesAssigned},
    {"UnitIsCharmed", LuaUnitIsCharmed},
    {"UnitIsPossessed", LuaUnitIsPossessed},
    {"UnitOnTaxi", LuaUnitOnTaxi},
    {"UnitStat", LuaUnitStat},
    {"UnitDamage", LuaUnitDamage},
    {"UnitRangedDamage", LuaUnitRangedDamage},
    {"UnitAttackSpeed", LuaUnitAttackSpeed},
    {"UnitAttackPower", LuaUnitAttackPower},
    {"UnitRangedAttackPower", LuaUnitRangedAttackPower},
    {"UnitArmor", LuaUnitArmor},
    {"UnitDefense", LuaUnitDefense},
    {"UnitAttackBothHands", LuaUnitAttackBothHands},
    {"GetCritChance", LuaGetCritChance},
    {"GetSpellPenetration", LuaGetSpellPenetration},
    {"UnitCastingInfo", kUnitCastingInfo.handler},
    {"UnitChannelInfo", kUnitChannelInfo.handler},
    {"UnitHasRelicSlot", LuaUnitHasRelicSlot},
    {"TargetUnit", LuaTargetUnit},
    {"ClearTarget", LuaClearTarget},
    {"AssistUnit", LuaAssistUnit},
    {"FocusUnit", LuaFocusUnit},
    {"ClearFocus", LuaClearFocus},
    {"UnitInParty", LuaUnitInParty},
    {"UnitInRaid", LuaUnitInRaid},
    {"GetUnitSpeed", LuaGetUnitSpeed},
    {"UnitResistance", LuaUnitResistance},
    {"GetUnitMaxHealthModifier", LuaGetUnitMaxHealthModifier},
    {"CheckInteractDistance", LuaCheckInteractDistance},
    {"UnitIsTappedByPlayer", LuaUnitIsTappedByPlayer},
    {"UnitIsTappedByAllThreatList", LuaUnitIsTappedByAllThreatList},
    {"UnitPlayerOrPetInParty", LuaUnitPlayerOrPetInParty},
    {"UnitPlayerOrPetInRaid", LuaUnitPlayerOrPetInRaid},
    {"UnitIsVisible", LuaUnitIsVisible},
    {"GetThreatStatusColor", LuaGetThreatStatusColor},
    {"GetPlayerFacing", LuaGetPlayerFacing},
    {"UnitIsRaidOfficer", LuaUnitIsRaidOfficer},
    {"FollowUnit", LuaFollowUnit},
    {"UnitAffectingCombat", LuaUnitAffectingCombat},
    {"GetArmorPenetration", LuaGetArmorPenetration},
    {"UnitInRange", LuaUnitInRange},
    {"DeclineName", LuaDeclineName},
    {"GetAttackPowerForStat", LuaGetAttackPowerForStat},
    {"GetPowerRegen", LuaGetPowerRegen},
    {"GetResSicknessDuration", LuaGetResSicknessDuration},
    {"GetUnitHealthModifier", LuaGetUnitHealthModifier},
    {"GetUnitPowerModifier", LuaGetUnitPowerModifier},
    {"TargetLastEnemy", LuaTargetLastEnemy},
    {"TargetLastFriend", LuaTargetLastFriend},
    {"TargetLastTarget", LuaTargetLastTarget},
    {"TargetNearest", LuaTargetNearest},
    {"TargetNearestEnemy", LuaTargetNearestEnemy},
    {"TargetNearestEnemyPlayer", LuaTargetNearestEnemyPlayer},
    {"TargetNearestFriend", LuaTargetNearestFriend},
    {"TargetNearestFriendPlayer", LuaTargetNearestFriendPlayer},
    {"TargetNearestPartyMember", LuaTargetNearestPartyMember},
    {"TargetNearestRaidMember", LuaTargetNearestRaidMember},
    {"UnitCanAssist", LuaUnitCanAssist},
    {"UnitCharacterPoints", LuaUnitCharacterPoints},
    {"UnitInBattleground", LuaUnitInBattleground},
    {"UnitIsFeignDeath", LuaUnitIsFeignDeath},
    {"UnitIsTapped", LuaUnitIsTapped},
    {"UnitPVPName", LuaUnitPVPName},
    {"UnitClassBase", LuaUnitClassBase},
    {"UnitIsSameServer", LuaUnitIsSameServer},
    {"UnitIsTrivial", LuaUnitIsTrivial},
    {"UnitPVPRank", LuaUnitPVPRank},
    {"UnitRangedAttack", LuaUnitRangedAttack},
    {"UnitSelectionColor", LuaUnitSelectionColor},
    {"UnitIsInMyGuild", LuaUnitIsInMyGuild},
    {"GetUnitPitch", LuaGetUnitPitch},
    {"InteractUnit", LuaInteractUnit},
    {"TargetDirectionEnemy", LuaTargetDirectionEnemy},
    {"TargetDirectionFriend", LuaTargetDirectionFriend},
    {"TargetDirectionFinished", LuaTargetDirectionFinished},
    {"TargetTotem", LuaTargetTotem},
};

}

openwow::ui::lua::NativeBindingCatalog UnitNativeBindingCatalog() {
  return openwow::ui::lua::NativeFunctionCatalog(
      "game.actors.units", openwow::ui::lua::BindingScope::kWorld, kUnitLuaBindings);
}

}

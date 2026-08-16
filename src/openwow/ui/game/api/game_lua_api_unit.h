
#pragma once

#include <string>

struct lua_State;

namespace openwow::data::dbc {
struct VehicleSeatEntry;
}

namespace openwow::game {
class WorldSession;
}

namespace openwow::ui::game::detail {

const openwow::data::dbc::VehicleSeatEntry *
ResolveLuaUnitVehicleSeatEntry(openwow::game::WorldSession *session,
                               const std::string &unit_id);

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
int LuaUnitIsDead(lua_State* L);
int LuaUnitIsDeadOrGhost(lua_State* L);
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

int LuaGetPlayerMapPosition(lua_State* L);

int LuaUnitGUID(lua_State* L);

int LuaUnitFactionGroup(lua_State* L);

int LuaUnitClassification(lua_State* L);
int LuaUnitCreatureType(lua_State* L);

int LuaUnitXP(lua_State* L);
int LuaUnitXPMax(lua_State* L);

int LuaUnitReaction(lua_State* L);
int LuaUnitThreatSituation(lua_State* L);
int LuaUnitDetailedThreatSituation(lua_State* L);
int LuaUnitGroupRolesAssigned(lua_State* L);
int LuaUnitHasVehicleUI(lua_State* L);
int LuaUnitTargetsVehicleInRaidUI(lua_State* L);
int LuaUnitInVehicle(lua_State* L);
int LuaUnitInVehicleControlSeat(lua_State* L);
int LuaUnitUsingVehicle(lua_State* L);
int LuaUnitIsCharmed(lua_State* L);
int LuaUnitIsPossessed(lua_State* L);
int LuaUnitOnTaxi(lua_State* L);

int LuaUnitInParty(lua_State* L);
int LuaUnitInRaid(lua_State* L);
int LuaUnitIsPartyLeader(lua_State* L);
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

int LuaGetAttackPowerForStat(lua_State* L);
int LuaGetPowerRegen(lua_State* L);
int LuaGetResSicknessDuration(lua_State* L);
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
int LuaUnitHasRelicSlot(lua_State* L);
int LuaUnitInBattleground(lua_State* L);
int LuaUnitIsFeignDeath(lua_State* L);
int LuaUnitIsTapped(lua_State* L);
int LuaUnitPVPName(lua_State* L);
int LuaUnitVehicleSeatInfo(lua_State* L);

int LuaUnitClassBase(lua_State* L);
int LuaUnitIsSameServer(lua_State* L);
int LuaUnitIsTrivial(lua_State* L);
int LuaUnitPVPRank(lua_State* L);
int LuaUnitRangedAttack(lua_State* L);
int LuaUnitSelectionColor(lua_State* L);

int LuaGetPlayerInfoByGUID(lua_State* L);

int LuaGetArmorPenetration(lua_State* L);
int LuaUnitInRange(lua_State* L);

int LuaUnitAffectingCombat(lua_State* L);
int LuaHasFullControl(lua_State* L);
int LuaGetMaxCombatRatingBonus(lua_State* L);
int LuaGetUnitManaRegenRateFromSpirit(lua_State* L);
int LuaTargetDirectionEnemy(lua_State* L);
int LuaTargetDirectionFriend(lua_State* L);
int LuaTargetDirectionFinished(lua_State* L);
int LuaTargetTotem(lua_State* L);
int LuaGetUnitPitch(lua_State* L);
int LuaInteractUnit(lua_State* L);

}

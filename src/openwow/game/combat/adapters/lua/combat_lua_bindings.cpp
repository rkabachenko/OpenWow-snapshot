#include "openwow/game/combat/adapters/lua/combat_lua_bindings.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/runtime/lua/lua_composition.h"

extern "C" {
#include <lua.hpp>
}

namespace openwow::ui::game::detail {

int LuaGetCombatRating(lua_State* L);
int LuaGetCombatRatingBonus(lua_State* L);
int LuaGetDodgeChance(lua_State* L);
int LuaGetParryChance(lua_State* L);
int LuaGetBlockChance(lua_State* L);
int LuaGetShieldBlock(lua_State* L);
int LuaGetRangedCritChance(lua_State* L);
int LuaGetSpellCritChance(lua_State* L);
int LuaGetExpertise(lua_State* L);
int LuaGetExpertisePercent(lua_State* L);
int LuaGetManaRegen(lua_State* L);
int LuaGetComboPoints(lua_State* L);
int LuaGetBattlefieldStatus(lua_State* L);
int LuaGetBattlefieldTimeWaited(lua_State* L);
int LuaGetBattlefieldEstimatedWaitTime(lua_State* L);
int LuaGetBattlefieldPortExpiration(lua_State* L);
int LuaAcceptBattlefieldPort(lua_State* L);
int LuaGetBattlefieldInstanceRunTime(lua_State* L);
int LuaRequestBattlegroundInstanceInfo(lua_State* L);
int LuaJoinBattlefield(lua_State* L);
int LuaLeaveBattlefield(lua_State* L);
int LuaGetBattlefieldWinner(lua_State* L);
int LuaGetNumBattlefieldScores(lua_State* L);
int LuaGetBattlefieldScore(lua_State* L);
int LuaRequestBattlefieldScoreData(lua_State* L);
int LuaGetBattlefieldStatInfo(lua_State* L);
int LuaGetBattlefieldStatData(lua_State* L);
int LuaSortBGList(lua_State* L);
int LuaGetNumBattlegroundTypes(lua_State* L);
int LuaGetBattlegroundInfo(lua_State* L);
int LuaGetHolidayBGHonorCurrencyBonuses(lua_State* L);
int LuaGetRandomBGHonorCurrencyBonuses(lua_State* L);
int LuaGetBattlefieldFlagPosition(lua_State* L);
int LuaGetWorldPVPQueueStatus(lua_State* L);
int LuaIsActiveBattlefieldArena(lua_State* L);
int LuaGetBattlefieldArenaFaction(lua_State* L);
int LuaGetBattlefieldTeamInfo(lua_State* L);
int LuaIsPVPTimerRunning(lua_State* L);
int LuaGetPVPTimer(lua_State* L);
int LuaTogglePVP(lua_State* L);
int LuaGetPVPDesired(lua_State* L);
int LuaSetPVP(lua_State* L);
int LuaNotifyInspect(lua_State* L);
int LuaClearInspectPlayer(lua_State* L);
int LuaCanInspect(lua_State* L);
int LuaHasInspectHonorData(lua_State* L);
int LuaRequestInspectHonorData(lua_State* L);
int LuaGetInspectHonorData(lua_State* L);
int LuaCombatLog_Object_IsA(lua_State* L);
int LuaCombatLogClearEntries(lua_State* L);
int LuaCombatLogResetFilter(lua_State* L);
int LuaGetPVPSessionStats(lua_State* L);
int LuaGetPVPYesterdayStats(lua_State* L);
int LuaGetPVPLifetimeStats(lua_State* L);
int LuaGetHonorCurrency(lua_State* L);
int LuaGetArenaCurrency(lua_State* L);
int LuaGetMaxArenaCurrency(lua_State* L);
int LuaGetArenaTeam(lua_State* L);
int LuaGetBattlefieldInstanceExpiration(lua_State* L);
int LuaGetBattlefieldMapIconScale(lua_State* L);
int LuaGetNumBattlefieldFlagPositions(lua_State* L);
int LuaGetPVPRankInfo(lua_State* L);
int LuaAttackTarget(lua_State* L);
int LuaStartAttack(lua_State* L);
int LuaStopAttack(lua_State* L);
int LuaHasFullControl(lua_State* L);
int LuaAcceptDuel(lua_State* L);
int LuaCancelDuel(lua_State* L);
int LuaAcceptArenaTeam(lua_State* L);
int LuaDeclineArenaTeam(lua_State* L);
int LuaGetArenaTeamRosterInfo(lua_State* L);
int LuaArenaTeamLeave(lua_State* L);
int LuaArenaTeamDisband(lua_State* L);
int LuaArenaTeamInviteByName(lua_State* L);
int LuaArenaTeamUninviteByName(lua_State* L);
int LuaArenaTeamSetLeaderByName(lua_State* L);
int LuaIsArenaTeamCaptain(lua_State* L);
int LuaRequestBattlefieldPositions(lua_State* L);
int LuaGetNumBattlefieldPositions(lua_State* L);
int LuaGetBattlefieldPosition(lua_State* L);
int LuaSetBattlefieldScoreFaction(lua_State* L);
int LuaCanJoinBattlefieldAsGroup(lua_State* L);
int LuaCombatLogAddFilter(lua_State* L);
int LuaCombatLogAdvanceEntry(lua_State* L);
int LuaCombatLogGetCurrentEntry(lua_State* L);
int LuaCombatLogGetNumEntries(lua_State* L);
int LuaCombatLogGetRetentionTime(lua_State* L);
int LuaCombatLogSetCurrentEntry(lua_State* L);
int LuaCombatLogSetRetentionTime(lua_State* L);
int LuaCombatTextSetActiveUnit(lua_State* L);
int LuaGetArenaTeamGdfInfo(lua_State* L);
int LuaGetArenaTeamRosterSelection(lua_State* L);
int LuaGetArenaTeamRosterShowOffline(lua_State* L);
int LuaCloseArenaTeamRoster(lua_State* L);
int LuaGetSelectedBattlefield(lua_State* L);
int LuaGetBattlefieldInstanceInfo(lua_State* L);
int LuaGetInspectArenaTeamData(lua_State* L);
int LuaGetNumBattlefieldStats(lua_State* L);
int LuaGetNumBattlefieldVehicles(lua_State* L);
int LuaGetNumBattlefields(lua_State* L);
int LuaGetPVPRankProgress(lua_State* L);
int LuaIsBattlefieldArena(lua_State* L);
int LuaReportPlayerIsPVPAFK(lua_State* L);
int LuaSetSelectedBattlefield(lua_State* L);
int LuaStartDuel(lua_State* L);
int LuaArenaTeamRoster(lua_State* L);
int LuaGetCurrentArenaSeason(lua_State* L);
int LuaGetPreviousArenaSeason(lua_State* L);
int LuaBuyPetition(lua_State* L);
int LuaGetMaxCombatRatingBonus(lua_State* L);
int LuaGetBattlefieldInfo(lua_State* L);
int LuaGetBattlefieldVehicleInfo(lua_State* L);
int LuaSortArenaTeamRoster(lua_State* L);
int LuaSortBattlefieldScoreData(lua_State* L);
int LuaTurnInArenaPetition(lua_State* L);

}

namespace openwow::ui::game {

using namespace detail;

namespace {

constexpr openwow::ui::LuaGlobalBinding kCombatLuaBindings[] = {
    {"GetCombatRating", LuaGetCombatRating},
    {"GetCombatRatingBonus", LuaGetCombatRatingBonus},
    {"GetDodgeChance", LuaGetDodgeChance},
    {"GetParryChance", LuaGetParryChance},
    {"GetBlockChance", LuaGetBlockChance},
    {"GetShieldBlock", LuaGetShieldBlock},
    {"GetRangedCritChance", LuaGetRangedCritChance},
    {"GetSpellCritChance", LuaGetSpellCritChance},
    {"GetExpertise", LuaGetExpertise},
    {"GetExpertisePercent", LuaGetExpertisePercent},
    {"GetManaRegen", LuaGetManaRegen},
    {"GetComboPoints", LuaGetComboPoints},
    {"GetBattlefieldStatus", LuaGetBattlefieldStatus},
    {"GetBattlefieldTimeWaited", LuaGetBattlefieldTimeWaited},
    {"GetBattlefieldEstimatedWaitTime", LuaGetBattlefieldEstimatedWaitTime},
    {"GetBattlefieldPortExpiration", LuaGetBattlefieldPortExpiration},
    {"AcceptBattlefieldPort", LuaAcceptBattlefieldPort},
    {"GetBattlefieldInstanceRunTime", LuaGetBattlefieldInstanceRunTime},
    {"RequestBattlegroundInstanceInfo", LuaRequestBattlegroundInstanceInfo},
    {"JoinBattlefield", LuaJoinBattlefield},
    {"LeaveBattlefield", LuaLeaveBattlefield},
    {"GetBattlefieldWinner", LuaGetBattlefieldWinner},
    {"GetNumBattlefieldScores", LuaGetNumBattlefieldScores},
    {"GetBattlefieldScore", LuaGetBattlefieldScore},
    {"RequestBattlefieldScoreData", LuaRequestBattlefieldScoreData},
    {"GetBattlefieldStatInfo", LuaGetBattlefieldStatInfo},
    {"GetBattlefieldStatData", LuaGetBattlefieldStatData},
    {"SortBGList", LuaSortBGList},
    {"GetNumBattlegroundTypes", LuaGetNumBattlegroundTypes},
    {"GetBattlegroundInfo", LuaGetBattlegroundInfo},
    {"GetHolidayBGHonorCurrencyBonuses", LuaGetHolidayBGHonorCurrencyBonuses},
    {"GetRandomBGHonorCurrencyBonuses", LuaGetRandomBGHonorCurrencyBonuses},
    {"GetBattlefieldFlagPosition", LuaGetBattlefieldFlagPosition},
    {"GetWorldPVPQueueStatus", LuaGetWorldPVPQueueStatus},
    {"IsActiveBattlefieldArena", LuaIsActiveBattlefieldArena},
    {"GetBattlefieldArenaFaction", LuaGetBattlefieldArenaFaction},
    {"GetBattlefieldTeamInfo", LuaGetBattlefieldTeamInfo},
    {"IsPVPTimerRunning", LuaIsPVPTimerRunning},
    {"GetPVPTimer", LuaGetPVPTimer},
    {"TogglePVP", LuaTogglePVP},
    {"GetPVPDesired", LuaGetPVPDesired},
    {"SetPVP", LuaSetPVP},
    {"NotifyInspect", LuaNotifyInspect},
    {"ClearInspectPlayer", LuaClearInspectPlayer},
    {"CanInspect", LuaCanInspect},
    {"HasInspectHonorData", LuaHasInspectHonorData},
    {"RequestInspectHonorData", LuaRequestInspectHonorData},
    {"GetInspectHonorData", LuaGetInspectHonorData},
    {"CombatLog_Object_IsA", LuaCombatLog_Object_IsA},
    {"CombatLogClearEntries", LuaCombatLogClearEntries},
    {"CombatLogResetFilter", LuaCombatLogResetFilter},
    {"GetPVPSessionStats", LuaGetPVPSessionStats},
    {"GetPVPYesterdayStats", LuaGetPVPYesterdayStats},
    {"GetPVPLifetimeStats", LuaGetPVPLifetimeStats},
    {"GetHonorCurrency", LuaGetHonorCurrency},
    {"GetArenaCurrency", LuaGetArenaCurrency},
    {"GetMaxArenaCurrency", LuaGetMaxArenaCurrency},
    {"GetArenaTeam", LuaGetArenaTeam},
    {"GetBattlefieldInstanceExpiration", LuaGetBattlefieldInstanceExpiration},
    {"GetBattlefieldMapIconScale", LuaGetBattlefieldMapIconScale},
    {"GetNumBattlefieldFlagPositions", LuaGetNumBattlefieldFlagPositions},
    {"GetPVPRankInfo", LuaGetPVPRankInfo},
    {"AttackTarget", LuaAttackTarget},
    {"StartAttack", LuaStartAttack},
    {"StopAttack", LuaStopAttack},
    {"HasFullControl", LuaHasFullControl},
    {"AcceptDuel", LuaAcceptDuel},
    {"CancelDuel", LuaCancelDuel},
    {"AcceptArenaTeam", LuaAcceptArenaTeam},
    {"DeclineArenaTeam", LuaDeclineArenaTeam},
    {"GetArenaTeamRosterInfo", LuaGetArenaTeamRosterInfo},
    {"ArenaTeamLeave", LuaArenaTeamLeave},
    {"ArenaTeamDisband", LuaArenaTeamDisband},
    {"ArenaTeamInviteByName", LuaArenaTeamInviteByName},
    {"ArenaTeamUninviteByName", LuaArenaTeamUninviteByName},
    {"ArenaTeamSetLeaderByName", LuaArenaTeamSetLeaderByName},
    {"IsArenaTeamCaptain", LuaIsArenaTeamCaptain},
    {"RequestBattlefieldPositions", LuaRequestBattlefieldPositions},
    {"GetNumBattlefieldPositions", LuaGetNumBattlefieldPositions},
    {"GetBattlefieldPosition", LuaGetBattlefieldPosition},
    {"SetBattlefieldScoreFaction", LuaSetBattlefieldScoreFaction},
    {"CanJoinBattlefieldAsGroup", LuaCanJoinBattlefieldAsGroup},
    {"CombatLogAddFilter", LuaCombatLogAddFilter},
    {"CombatLogAdvanceEntry", LuaCombatLogAdvanceEntry},
    {"CombatLogGetCurrentEntry", LuaCombatLogGetCurrentEntry},
    {"CombatLogGetNumEntries", LuaCombatLogGetNumEntries},
    {"CombatLogGetRetentionTime", LuaCombatLogGetRetentionTime},
    {"CombatLogSetCurrentEntry", LuaCombatLogSetCurrentEntry},
    {"CombatLogSetRetentionTime", LuaCombatLogSetRetentionTime},
    {"CombatTextSetActiveUnit", LuaCombatTextSetActiveUnit},
    {"GetArenaTeamGdfInfo", LuaGetArenaTeamGdfInfo},
    {"GetArenaTeamRosterSelection", LuaGetArenaTeamRosterSelection},
    {"GetArenaTeamRosterShowOffline", LuaGetArenaTeamRosterShowOffline},
    {"CloseArenaTeamRoster", LuaCloseArenaTeamRoster},
    {"GetSelectedBattlefield", LuaGetSelectedBattlefield},
    {"GetBattlefieldInstanceInfo", LuaGetBattlefieldInstanceInfo},
    {"GetInspectArenaTeamData", LuaGetInspectArenaTeamData},
    {"GetNumBattlefieldStats", LuaGetNumBattlefieldStats},
    {"GetNumBattlefieldVehicles", LuaGetNumBattlefieldVehicles},
    {"GetNumBattlefields", LuaGetNumBattlefields},
    {"GetPVPRankProgress", LuaGetPVPRankProgress},
    {"IsBattlefieldArena", LuaIsBattlefieldArena},
    {"ReportPlayerIsPVPAFK", LuaReportPlayerIsPVPAFK},
    {"SetSelectedBattlefield", LuaSetSelectedBattlefield},
    {"StartDuel", LuaStartDuel},
    {"ArenaTeamRoster", LuaArenaTeamRoster},
    {"GetCurrentArenaSeason", LuaGetCurrentArenaSeason},
    {"GetPreviousArenaSeason", LuaGetPreviousArenaSeason},
    {"BuyPetition", LuaBuyPetition},
    {"GetMaxCombatRatingBonus", LuaGetMaxCombatRatingBonus},
    {"GetBattlefieldInfo", LuaGetBattlefieldInfo},
    {"GetBattlefieldVehicleInfo", LuaGetBattlefieldVehicleInfo},
    {"SortArenaTeamRoster", LuaSortArenaTeamRoster},
    {"SortBattlefieldScoreData", LuaSortBattlefieldScoreData},
    {"TurnInArenaPetition", LuaTurnInArenaPetition},
};

}

openwow::ui::lua::NativeBindingCatalog CombatNativeBindingCatalog() {
  return openwow::ui::lua::NativeFunctionCatalog(
      "game.combat", openwow::ui::lua::BindingScope::kWorld, kCombatLuaBindings);
}

}

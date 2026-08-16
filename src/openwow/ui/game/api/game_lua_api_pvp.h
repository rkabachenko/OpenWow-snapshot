
#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

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
int LuaGetNumBattlefieldVehicles(lua_State* L);

int LuaIsActiveBattlefieldArena(lua_State* L);
int LuaGetBattlefieldArenaFaction(lua_State* L);
int LuaGetBattlefieldTeamInfo(lua_State* L);

int LuaIsPVPTimerRunning(lua_State* L);
int LuaGetPVPTimer(lua_State* L);
int LuaTogglePVP(lua_State* L);
int LuaGetPVPDesired(lua_State* L);
int LuaSetPVP(lua_State* L);

int LuaGetBattlefieldInstanceExpiration(lua_State* L);
int LuaGetBattlefieldMapIconScale(lua_State* L);
int LuaGetNumBattlefieldFlagPositions(lua_State* L);

int LuaReportPlayerIsPVPAFK(lua_State* L);
int LuaSetArenaTeamRosterSelection(lua_State* L);
int LuaSetArenaTeamRosterShowOffline(lua_State* L);
int LuaCanJoinBattlefieldAsGroup(lua_State* L);
int LuaGetArenaTeamGdfInfo(lua_State* L);
int LuaGetArenaTeamRosterSelection(lua_State* L);
int LuaGetArenaTeamRosterShowOffline(lua_State* L);
int LuaGetSelectedBattlefield(lua_State* L);
int LuaGetBattlefieldInstanceInfo(lua_State* L);
int LuaGetInspectArenaTeamData(lua_State* L);
int LuaGetNumBattlefieldStats(lua_State* L);
int LuaGetNumBattlefields(lua_State* L);
int LuaGetPVPRankProgress(lua_State* L);
int LuaIsBattlefieldArena(lua_State* L);
int LuaSetSelectedBattlefield(lua_State* L);

int LuaPlayerIsPVPInactive(lua_State* L);

int LuaAcceptDuel(lua_State* L);
int LuaCancelDuel(lua_State* L);
int LuaCommentatorSetMode(lua_State* L);
int LuaCommentatorToggleMode(lua_State* L);
int LuaCommentatorGetMode(lua_State* L);
int LuaCommentatorGetNumMaps(lua_State* L);
int LuaCommentatorGetMapInfo(lua_State* L);
int LuaCommentatorGetInstanceInfo(lua_State* L);
int LuaCommentatorEnterInstance(lua_State* L);
int LuaCommentatorExitInstance(lua_State* L);
int LuaCommentatorGetNumPlayers(lua_State* L);
int LuaCommentatorGetPlayerInfo(lua_State* L);
int LuaCommentatorFollowPlayer(lua_State* L);
int LuaCommentatorLookatPlayer(lua_State* L);
int LuaCommentatorZoomIn(lua_State* L);
int LuaCommentatorZoomOut(lua_State* L);
int LuaCommentatorSetCamera(lua_State* L);
int LuaCommentatorGetCamera(lua_State* L);
int LuaCommentatorGetCurrentMapID(lua_State* L);
int LuaCommentatorStartInstance(lua_State* L);
int LuaCommentatorAddPlayer(lua_State* L);
int LuaCommentatorRemovePlayer(lua_State* L);
int LuaCommentatorSetBattlemaster(lua_State* L);
int LuaCommentatorSetMoveSpeed(lua_State* L);
int LuaCommentatorSetCameraCollision(lua_State* L);
int LuaCommentatorSetTargetHeightOffset(lua_State* L);
int LuaCommentatorSetMapAndInstanceIndex(lua_State* L);
int LuaCommentatorSetPlayerIndex(lua_State* L);
int LuaCommentatorUpdatePlayerInfo(lua_State* L);
int LuaCommentatorUpdateMapInfo(lua_State* L);
int LuaCommentatorSetSkirmishMatchmakingMode(lua_State* L);
int LuaCommentatorRequestSkirmishQueueData(lua_State* L);
int LuaCommentatorGetSkirmishQueueCount(lua_State* L);
int LuaCommentatorGetSkirmishQueuePlayerInfo(lua_State* L);
int LuaCommentatorStartSkirmishMatch(lua_State* L);
int LuaCommentatorRequestSkirmishMode(lua_State* L);
int LuaCommentatorGetSkirmishMode(lua_State* L);
int LuaBattlefieldMgrEntryInviteResponse(lua_State* L);
int LuaBattlefieldMgrQueueRequest(lua_State* L);
int LuaBattlefieldMgrQueueInviteResponse(lua_State* L);
int LuaBattlefieldMgrExitRequest(lua_State* L);
int LuaGetBattlefieldInfo(lua_State* L);
int LuaGetBattlefieldVehicleInfo(lua_State* L);
int LuaSortBattlefieldScoreData(lua_State* L);

}

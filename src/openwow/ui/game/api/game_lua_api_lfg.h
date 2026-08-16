
#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaGetLFGQueueStats(lua_State *L);
int LuaGetLFGDungeonInfo(lua_State *L);
int LuaGetLFGDungeonRewards(lua_State *L);
int LuaGetLFGDungeonRewardInfo(lua_State *L);
int LuaGetLFGDungeonRewardLink(lua_State *L);
int LuaGetLFGProposal(lua_State *L);
int LuaGetLFGCompletionReward(lua_State *L);
int LuaCompleteLFGRoleCheck(lua_State *L);
int LuaGetAvailableRoles(lua_State *L);
int LuaSetLFGRoles(lua_State *L);
int LuaGetLFGRoles(lua_State *L);
int LuaJoinLFG(lua_State *L);
int LuaLeaveLFG(lua_State *L);
int LuaGetLFGRandomDungeonInfo(lua_State *L);
int LuaGetNumRandomDungeons(lua_State *L);
int LuaSetLFGDungeon(lua_State *L);
int LuaClearAllLFGDungeons(lua_State *L);
int LuaGetLFGTypes(lua_State *L);
int LuaSetLFGComment(lua_State *L);
int LuaCanPartyLFGBackfill(lua_State *L);
int LuaIsPartyLFG(lua_State *L);
int LuaIsInLFGDungeon(lua_State *L);
int LuaPartyLFGStartBackfill(lua_State *L);

int LuaRefreshLFGList(lua_State *L);
int LuaRequestLFDPartyLockInfo(lua_State *L);
int LuaRequestLFDPlayerLockInfo(lua_State *L);
int LuaGetLFDChoiceCollapseState(lua_State *L);
int LuaGetLFDChoiceEnabledState(lua_State *L);
int LuaGetLFDChoiceInfo(lua_State *L);
int LuaGetLFDChoiceLockedState(lua_State *L);
int LuaGetLFDChoiceOrder(lua_State *L);
int LuaGetLFDLockInfo(lua_State *L);
int LuaGetLFDLockPlayerCount(lua_State *L);
int LuaGetLFGCompletionRewardItem(lua_State *L);
int LuaGetLFGInfoLocal(lua_State *L);
int LuaGetLFGInfoServer(lua_State *L);
int LuaGetLFGProposalEncounter(lua_State *L);
int LuaGetLFGProposalMember(lua_State *L);
int LuaGetLFDQueuedList(lua_State *L);
int LuaGetLastQueueStatusIndex(lua_State *L);
int LuaIsListedInLFR(lua_State *L);
int LuaGetLFGRandomCooldownExpiration(lua_State *L);
int LuaGetLFGRoleUpdate(lua_State *L);
int LuaGetLFGRoleUpdateMember(lua_State *L);
int LuaGetLFGRoleUpdateSlot(lua_State *L);

int LuaClearLFGDungeon(lua_State *L);
int LuaIsLFGDungeonJoinable(lua_State *L);
int LuaSearchLFGGetNumResults(lua_State *L);
int LuaSearchLFGGetEncounterResults(lua_State *L);
int LuaSearchLFGGetPartyResults(lua_State *L);
int LuaSearchLFGGetResults(lua_State *L);
int LuaSearchLFGGetJoinedID(lua_State *L);
int LuaSearchLFGJoin(lua_State *L);
int LuaSearchLFGLeave(lua_State *L);
int LuaSearchLFGSort(lua_State *L);

int LuaGetLFRChoiceOrder(lua_State *L);
int LuaSetLFGDungeonEnabled(lua_State *L);
int LuaSetLFGHeaderCollapsed(lua_State *L);
int LuaUnitHasLFGDeserter(lua_State *L);
int LuaUnitHasLFGRandomCooldown(lua_State *L);

}

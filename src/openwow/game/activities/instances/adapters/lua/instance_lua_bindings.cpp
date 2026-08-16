#include "openwow/game/activities/instances/adapters/lua/instance_lua_bindings.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/runtime/lua/lua_composition.h"

extern "C" {
#include <lua.hpp>
}

namespace openwow::ui::game::detail {

int LuaIsInInstance(lua_State* L);
int LuaGetInstanceInfo(lua_State* L);
int LuaGetInstanceDifficulty(lua_State* L);
int LuaGetDungeonDifficulty(lua_State* L);
int LuaSetDungeonDifficulty(lua_State* L);
int LuaGetRaidDifficulty(lua_State* L);
int LuaSetRaidDifficulty(lua_State* L);
int LuaCanChangePlayerDifficulty(lua_State* L);
int LuaChangePlayerDifficulty(lua_State* L);
int LuaCanMapChangeDifficulty(lua_State* L);
int LuaResetInstances(lua_State* L);
int LuaGetNumSavedInstances(lua_State* L);
int LuaGetSavedInstanceInfo(lua_State* L);
int LuaGetNumWorldStateUI(lua_State* L);
int LuaGetWorldStateUIInfo(lua_State* L);
int LuaGetLFGQueueStats(lua_State* L);
int LuaGetLFGDungeonInfo(lua_State* L);
int LuaGetLFGDungeonRewards(lua_State* L);
int LuaGetLFGDungeonRewardInfo(lua_State* L);
int LuaGetLFGDungeonRewardLink(lua_State* L);
int LuaGetLFGProposal(lua_State* L);
int LuaGetLFGCompletionReward(lua_State* L);
int LuaCompleteLFGRoleCheck(lua_State* L);
int LuaGetAvailableRoles(lua_State* L);
int LuaSetLFGRoles(lua_State* L);
int LuaGetLFGRoles(lua_State* L);
int LuaJoinLFG(lua_State* L);
int LuaLeaveLFG(lua_State* L);
int LuaIsPartyLFG(lua_State* L);
int LuaIsInLFGDungeon(lua_State* L);
int LuaPartyLFGStartBackfill(lua_State* L);
int LuaGetLFGRandomDungeonInfo(lua_State* L);
int LuaGetNumRandomDungeons(lua_State* L);
int LuaSetLFGDungeon(lua_State* L);
int LuaClearAllLFGDungeons(lua_State* L);
int LuaSetLFGComment(lua_State* L);
int LuaClearLFGDungeon(lua_State* L);
int LuaIsLFGDungeonJoinable(lua_State* L);
int LuaSearchLFGGetNumResults(lua_State* L);
int LuaSearchLFGGetEncounterResults(lua_State* L);
int LuaSearchLFGGetPartyResults(lua_State* L);
int LuaSearchLFGGetResults(lua_State* L);
int LuaSearchLFGGetJoinedID(lua_State* L);
int LuaSearchLFGJoin(lua_State* L);
int LuaSearchLFGLeave(lua_State* L);
int LuaSearchLFGSort(lua_State* L);
int LuaGetLFGTypes(lua_State* L);
int LuaCanShowResetInstances(lua_State* L);
int LuaSetLFGBootVote(lua_State* L);
int LuaGetLFGBootProposal(lua_State* L);
int LuaGetRandomDungeonBestChoice(lua_State* L);
int LuaAcceptProposal(lua_State* L);
int LuaRejectProposal(lua_State* L);
int LuaLFGTeleport(lua_State* L);
int LuaGetLFGDeserterExpiration(lua_State* L);
int LuaRefreshLFGList(lua_State* L);
int LuaGetInstanceBootTimeRemaining(lua_State* L);
int LuaGetInstanceLockTimeRemaining(lua_State* L);
int LuaGetInstanceLockTimeRemainingEncounter(lua_State* L);
int LuaGetLFDChoiceCollapseState(lua_State* L);
int LuaGetLFDChoiceEnabledState(lua_State* L);
int LuaGetLFDChoiceInfo(lua_State* L);
int LuaGetLFDChoiceLockedState(lua_State* L);
int LuaGetLFDChoiceOrder(lua_State* L);
int LuaGetLFDLockInfo(lua_State* L);
int LuaGetLFDLockPlayerCount(lua_State* L);
int LuaGetLFGCompletionRewardItem(lua_State* L);
int LuaGetLFGInfoLocal(lua_State* L);
int LuaGetLFGInfoServer(lua_State* L);
int LuaGetLFGProposalEncounter(lua_State* L);
int LuaGetLFGProposalMember(lua_State* L);
int LuaGetLFDQueuedList(lua_State* L);
int LuaGetLastQueueStatusIndex(lua_State* L);
int LuaIsListedInLFR(lua_State* L);
int LuaGetLFGRandomCooldownExpiration(lua_State* L);
int LuaGetLFGRoleUpdate(lua_State* L);
int LuaGetLFGRoleUpdateMember(lua_State* L);
int LuaGetLFGRoleUpdateSlot(lua_State* L);
int LuaGetPartyLFGBackfillInfo(lua_State* L);
int LuaRequestLFDPartyLockInfo(lua_State* L);
int LuaRequestLFDPlayerLockInfo(lua_State* L);
int LuaGetLFRChoiceOrder(lua_State* L);
int LuaSetLFGDungeonEnabled(lua_State* L);
int LuaSetLFGHeaderCollapsed(lua_State* L);
int LuaUnitHasLFGDeserter(lua_State* L);
int LuaUnitHasLFGRandomCooldown(lua_State* L);
int LuaSetSavedInstanceExtend(lua_State* L);

}

namespace openwow::ui::game {

using namespace detail;

namespace {

constexpr openwow::ui::LuaGlobalBinding kInstanceLuaBindings[] = {
    {"IsInInstance", LuaIsInInstance},
    {"GetInstanceInfo", LuaGetInstanceInfo},
    {"GetInstanceDifficulty", LuaGetInstanceDifficulty},
    {"GetDungeonDifficulty", LuaGetDungeonDifficulty},
    {"SetDungeonDifficulty", LuaSetDungeonDifficulty},
    {"GetRaidDifficulty", LuaGetRaidDifficulty},
    {"SetRaidDifficulty", LuaSetRaidDifficulty},
    {"CanChangePlayerDifficulty", LuaCanChangePlayerDifficulty},
    {"ChangePlayerDifficulty", LuaChangePlayerDifficulty},
    {"CanMapChangeDifficulty", LuaCanMapChangeDifficulty},
    {"ResetInstances", LuaResetInstances},
    {"GetNumSavedInstances", LuaGetNumSavedInstances},
    {"GetSavedInstanceInfo", LuaGetSavedInstanceInfo},
    {"GetNumWorldStateUI", LuaGetNumWorldStateUI},
    {"GetWorldStateUIInfo", LuaGetWorldStateUIInfo},
    {"GetLFGQueueStats", LuaGetLFGQueueStats},
    {"GetLFGDungeonInfo", LuaGetLFGDungeonInfo},
    {"GetLFGDungeonRewards", LuaGetLFGDungeonRewards},
    {"GetLFGDungeonRewardInfo", LuaGetLFGDungeonRewardInfo},
    {"GetLFGDungeonRewardLink", LuaGetLFGDungeonRewardLink},
    {"GetLFGProposal", LuaGetLFGProposal},
    {"GetLFGCompletionReward", LuaGetLFGCompletionReward},
    {"CompleteLFGRoleCheck", LuaCompleteLFGRoleCheck},
    {"GetAvailableRoles", LuaGetAvailableRoles},
    {"SetLFGRoles", LuaSetLFGRoles},
    {"GetLFGRoles", LuaGetLFGRoles},
    {"JoinLFG", LuaJoinLFG},
    {"LeaveLFG", LuaLeaveLFG},
    {"IsPartyLFG", LuaIsPartyLFG},
    {"IsInLFGDungeon", LuaIsInLFGDungeon},
    {"PartyLFGStartBackfill", LuaPartyLFGStartBackfill},
    {"GetLFGRandomDungeonInfo", LuaGetLFGRandomDungeonInfo},
    {"GetNumRandomDungeons", LuaGetNumRandomDungeons},
    {"SetLFGDungeon", LuaSetLFGDungeon},
    {"ClearAllLFGDungeons", LuaClearAllLFGDungeons},
    {"SetLFGComment", LuaSetLFGComment},
    {"ClearLFGDungeon", LuaClearLFGDungeon},
    {"IsLFGDungeonJoinable", LuaIsLFGDungeonJoinable},
    {"SearchLFGGetNumResults", LuaSearchLFGGetNumResults},
    {"SearchLFGGetEncounterResults", LuaSearchLFGGetEncounterResults},
    {"SearchLFGGetPartyResults", LuaSearchLFGGetPartyResults},
    {"SearchLFGGetResults", LuaSearchLFGGetResults},
    {"SearchLFGGetJoinedID", LuaSearchLFGGetJoinedID},
    {"SearchLFGJoin", LuaSearchLFGJoin},
    {"SearchLFGLeave", LuaSearchLFGLeave},
    {"SearchLFGSort", LuaSearchLFGSort},
    {"GetLFGTypes", LuaGetLFGTypes},
    {"CanShowResetInstances", LuaCanShowResetInstances},
    {"SetLFGBootVote", LuaSetLFGBootVote},
    {"GetLFGBootProposal", LuaGetLFGBootProposal},
    {"GetRandomDungeonBestChoice", LuaGetRandomDungeonBestChoice},
    {"AcceptProposal", LuaAcceptProposal},
    {"RejectProposal", LuaRejectProposal},
    {"LFGTeleport", LuaLFGTeleport},
    {"GetLFGDeserterExpiration", LuaGetLFGDeserterExpiration},
    {"RefreshLFGList", LuaRefreshLFGList},
    {"GetInstanceBootTimeRemaining", LuaGetInstanceBootTimeRemaining},
    {"GetInstanceLockTimeRemaining", LuaGetInstanceLockTimeRemaining},
    {"GetInstanceLockTimeRemainingEncounter", LuaGetInstanceLockTimeRemainingEncounter},
    {"GetLFDChoiceCollapseState", LuaGetLFDChoiceCollapseState},
    {"GetLFDChoiceEnabledState", LuaGetLFDChoiceEnabledState},
    {"GetLFDChoiceInfo", LuaGetLFDChoiceInfo},
    {"GetLFDChoiceLockedState", LuaGetLFDChoiceLockedState},
    {"GetLFDChoiceOrder", LuaGetLFDChoiceOrder},
    {"GetLFDLockInfo", LuaGetLFDLockInfo},
    {"GetLFDLockPlayerCount", LuaGetLFDLockPlayerCount},
    {"GetLFGCompletionRewardItem", LuaGetLFGCompletionRewardItem},
    {"GetLFGInfoLocal", LuaGetLFGInfoLocal},
    {"GetLFGInfoServer", LuaGetLFGInfoServer},
    {"GetLFGProposalEncounter", LuaGetLFGProposalEncounter},
    {"GetLFGProposalMember", LuaGetLFGProposalMember},

    {"GetLFGQueuedList", LuaGetLFDQueuedList},
    {"GetLastQueueStatusIndex", LuaGetLastQueueStatusIndex},
    {"IsListedInLFR", LuaIsListedInLFR},
    {"GetLFGRandomCooldownExpiration", LuaGetLFGRandomCooldownExpiration},
    {"GetLFGRoleUpdate", LuaGetLFGRoleUpdate},
    {"GetLFGRoleUpdateMember", LuaGetLFGRoleUpdateMember},
    {"GetLFGRoleUpdateSlot", LuaGetLFGRoleUpdateSlot},
    {"GetPartyLFGBackfillInfo", LuaGetPartyLFGBackfillInfo},
    {"RequestLFDPartyLockInfo", LuaRequestLFDPartyLockInfo},
    {"RequestLFDPlayerLockInfo", LuaRequestLFDPlayerLockInfo},
    {"GetLFRChoiceOrder", LuaGetLFRChoiceOrder},
    {"SetLFGDungeonEnabled", LuaSetLFGDungeonEnabled},
    {"SetLFGHeaderCollapsed", LuaSetLFGHeaderCollapsed},
    {"UnitHasLFGDeserter", LuaUnitHasLFGDeserter},
    {"UnitHasLFGRandomCooldown", LuaUnitHasLFGRandomCooldown},
    {"SetSavedInstanceExtend", LuaSetSavedInstanceExtend},
};

}

openwow::ui::lua::NativeBindingCatalog InstanceNativeBindingCatalog() {
  return openwow::ui::lua::NativeFunctionCatalog(
      "game.activities.instances", openwow::ui::lua::BindingScope::kWorld, kInstanceLuaBindings);
}

}

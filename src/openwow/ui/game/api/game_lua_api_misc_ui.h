
#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaGetCurrencyListSize(lua_State* L);
int LuaGetCurrencyListInfo(lua_State* L);

int LuaGetMapLandmarkInfo(lua_State* L);
int LuaGetNumMapLandmarks(lua_State* L);
int LuaProcessMapClick(lua_State* L);
int LuaSetMapZoom(lua_State* L);

int LuaGetContainerItemPurchaseInfo(lua_State* L);

int LuaGetLocale(lua_State* L);
int LuaGetWintergraspWaitTime(lua_State* L);
int LuaCanQueueForWintergrasp(lua_State* L);
int LuaCanShowResetInstances(lua_State* L);
int LuaGetGuildBankWithdrawMoney(lua_State* L);
int LuaIsXPUserDisabled(lua_State* L);
int LuaGetCursorMoney(lua_State* L);
int LuaGetCurrentKeyBoardFocus(lua_State* L);
int LuaGetMouseFocus(lua_State* L);
int LuaGetMouseButtonClicked(lua_State* L);
int LuaIsMouseButtonDown(lua_State* L);

int LuaGetPVPRankInfo(lua_State* L);

int LuaGetItemGem(lua_State* L);
int LuaGetSocketItemInfo(lua_State* L);
int LuaGetExistingSocketInfo(lua_State* L);
int LuaGetExistingSocketLink(lua_State* L);
int LuaGetNewSocketInfo(lua_State* L);
int LuaGetNewSocketLink(lua_State* L);
int LuaGetNumSockets(lua_State* L);
int LuaClickSocketButton(lua_State* L);
int LuaSocketInventoryItem(lua_State* L);
int LuaCloseSocketInfo(lua_State* L);
int LuaAcceptSockets(lua_State* L);

int LuaFollowUnit(lua_State* L);
int LuaAttackTarget(lua_State* L);
int LuaStartAttack(lua_State* L);
int LuaStopAttack(lua_State* L);
int LuaIsOutdoors(lua_State* L);

int LuaRandomRoll(lua_State* L);

int LuaDeclineInvite(lua_State* L);
int LuaEquipPendingItem(lua_State* L);
int LuaClearChannelWatch(lua_State* L);
int LuaSetChannelWatch(lua_State* L);
int LuaSetAllowLowLevelRaid(lua_State* L);
int LuaCanResetTutorials(lua_State* L);
int LuaEquipmentSetContainsLockedItems(lua_State* L);
int LuaFrameXML_Debug(lua_State* L);
int LuaGetAccountExpansionLevel(lua_State* L);
int LuaGetAllowLowLevelRaid(lua_State* L);
int LuaGetClickFrame(lua_State* L);
int LuaGetMinigameState(lua_State* L);
int LuaGetMinigameType(lua_State* L);
int LuaGetPossessInfo(lua_State* L);
int LuaGetRealNumPartyMembers(lua_State* L);
int LuaGetRealNumRaidMembers(lua_State* L);
int LuaGetUnitHealthModifier(lua_State* L);
int LuaGetUnitPowerModifier(lua_State* L);
int LuaHasKey(lua_State* L);
int LuaIsPossessBarVisible(lua_State* L);
int LuaAcceptAreaSpiritHeal(lua_State* L);
int LuaCancelAreaSpiritHeal(lua_State* L);
int LuaGetAreaSpiritHealerTime(lua_State* L);
int LuaGetCorpseMapPosition(lua_State* L);
int LuaGetDeathReleasePosition(lua_State* L);
int LuaStartDuel(lua_State* L);

int LuaGetCoinIcon(lua_State* L);
int LuaRegisterForSavePerCharacter(lua_State* L);
int LuaDeclineName(lua_State* L);
int LuaFillLocalizedClassList(lua_State* L);
int LuaGetFactionForRace(lua_State* L);

int LuaShowingHelm(lua_State* L);
int LuaShowingCloak(lua_State* L);
int LuaShowHelm(lua_State* L);
int LuaShowCloak(lua_State* L);
int LuaGetNumFrames(lua_State* L);
int LuaEnumerateFrames(lua_State* L);
int LuaGetModifiedClickAction(lua_State* L);

}

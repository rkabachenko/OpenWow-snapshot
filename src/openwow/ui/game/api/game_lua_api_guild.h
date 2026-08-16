
#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaGetGuildRosterMOTD(lua_State* L);
int LuaGetGuildRosterShowOffline(lua_State* L);
int LuaSetGuildRosterShowOffline(lua_State* L);
int LuaSortGuildRoster(lua_State* L);
int LuaCloseGuildRoster(lua_State* L);
int LuaGuildPromote(lua_State* L);
int LuaGuildDemote(lua_State* L);
int LuaGuildSetLeader(lua_State* L);
int LuaGuildSetMOTD(lua_State* L);
int LuaGuildUninvite(lua_State* L);
int LuaGetGuildInfoText(lua_State* L);

int LuaGetNumGuildBankTabs(lua_State* L);
int LuaGetCurrentGuildBankTab(lua_State* L);
int LuaSetCurrentTab(lua_State* L);
int LuaGetGuildBankTabInfo(lua_State* L);
int LuaGetGuildBankItemInfo(lua_State* L);
int LuaGetGuildBankItemLink(lua_State* L);
int LuaGetGuildBankTabPermissions(lua_State* L);
int LuaQueryGuildBankTab(lua_State* L);
int LuaQueryGuildBankLog(lua_State* L);
int LuaQueryGuildBankText(lua_State* L);
int LuaGetNumGuildBankMoneyTransactions(lua_State* L);
int LuaGetGuildBankMoneyTransaction(lua_State* L);
int LuaGetNumGuildBankTransactions(lua_State* L);
int LuaGetGuildBankTransaction(lua_State* L);

int LuaAcceptGuild(lua_State* L);
int LuaBuyGuildBankTab(lua_State* L);
int LuaBuyGuildCharter(lua_State* L);
int LuaCloseGuildBankFrame(lua_State* L);
int LuaCloseGuildRegistrar(lua_State* L);
int LuaDeclineGuild(lua_State* L);
int LuaDepositGuildBankMoney(lua_State* L);
int LuaGuildRosterSetOfficerNote(lua_State* L);
int LuaGuildRosterSetPublicNote(lua_State* L);
int LuaQueryGuildEventLog(lua_State* L);
int LuaSetGuildBankTabInfo(lua_State* L);
int LuaSetGuildBankTabPermissions(lua_State* L);
int LuaSetGuildBankText(lua_State* L);
int LuaSetGuildBankWithdrawLimit(lua_State* L);
int LuaSetGuildRosterSelection(lua_State* L);
int LuaWithdrawGuildBankMoney(lua_State* L);
int LuaGuildInfo(lua_State* L);
int LuaOfferPetition(lua_State* L);
int LuaCanEditGuildEvent(lua_State* L);
int LuaCanEditGuildInfo(lua_State* L);
int LuaCanEditGuildTabInfo(lua_State* L);
int LuaCanEditMOTD(lua_State* L);
int LuaCanEditOfficerNote(lua_State* L);
int LuaCanEditPublicNote(lua_State* L);
int LuaCanGuildDemote(lua_State* L);
int LuaCanGuildInvite(lua_State* L);
int LuaCanGuildPromote(lua_State* L);
int LuaCanGuildRemove(lua_State* L);
int LuaCanViewOfficerNote(lua_State* L);
int LuaCanWithdrawGuildBankMoney(lua_State* L);
int LuaGetGuildBankTabCost(lua_State* L);
int LuaGetGuildBankText(lua_State* L);
int LuaGetGuildBankWithdrawLimit(lua_State* L);
int LuaGetGuildCharterCost(lua_State* L);
int LuaGetGuildEventInfo(lua_State* L);
int LuaGetGuildRosterLastOnline(lua_State* L);
int LuaGetGuildRosterSelection(lua_State* L);
int LuaGetGuildTabardFileNames(lua_State* L);
int LuaGetNumGuildEvents(lua_State* L);
int LuaGetTabardCreationCost(lua_State* L);
int LuaGetTabardInfo(lua_State* L);
int LuaGuildControlGetRankFlags(lua_State* L);
int LuaGuildControlSetRankFlag(lua_State* L);
int LuaGuildControlDelRank(lua_State* L);
int LuaIsGuildLeader(lua_State* L);

int LuaAutoStoreGuildBankItem(lua_State* L);
int LuaPickupGuildBankItem(lua_State* L);
int LuaPickupGuildBankMoney(lua_State* L);

int LuaCanGuildBankRepair(lua_State* L);
int LuaGetGuildBankMoney(lua_State* L);
int LuaGuildControlGetNumRanks(lua_State* L);
int LuaGuildControlGetRankName(lua_State* L);
int LuaGuildControlAddRank(lua_State* L);
int LuaGuildControlSaveRank(lua_State* L);
int LuaGuildControlSetRank(lua_State* L);
int LuaSetGuildBankTabWithdraw(lua_State* L);
int LuaSetGuildInfoText(lua_State* L);
int LuaSplitGuildBankItem(lua_State* L);
int LuaUnitIsInMyGuild(lua_State* L);

}

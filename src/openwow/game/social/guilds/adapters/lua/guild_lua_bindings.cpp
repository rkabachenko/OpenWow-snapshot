#include "openwow/game/social/guilds/adapters/lua/guild_lua_bindings.h"
#include "openwow/game/calendar/adapters/lua/calendar_lua_api.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/runtime/lua/lua_composition.h"

extern "C" {
#include <lua.hpp>
}

namespace openwow::ui::game::detail {

int LuaGetGuildRosterInfo(lua_State* L);
int LuaGetNumGuildMembers(lua_State* L);
int LuaGuildRoster(lua_State* L);
int LuaGetGuildInfo(lua_State* L);
int LuaIsInGuild(lua_State* L);
int LuaGuildInvite(lua_State* L);
int LuaGuildLeave(lua_State* L);
int LuaGuildDisband(lua_State* L);
int LuaGetGuildBankWithdrawMoney(lua_State* L);
int LuaCanGuildBankRepair(lua_State* L);
int LuaGetGuildBankMoney(lua_State* L);
int LuaGuildControlGetNumRanks(lua_State* L);
int LuaGuildControlGetRankName(lua_State* L);
int LuaGuildControlSetRank(lua_State* L);
int LuaGetGuildRosterMOTD(lua_State* L);
int LuaGetGuildRosterShowOffline(lua_State* L);
int LuaSetGuildRosterShowOffline(lua_State* L);
int LuaSortGuildRoster(lua_State* L);
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
int LuaCanEditGuildEvent(lua_State* L);
int LuaCanEditGuildInfo(lua_State* L);
int LuaCanEditGuildTabInfo(lua_State* L);
int LuaCanGuildDemote(lua_State* L);
int LuaCanGuildInvite(lua_State* L);
int LuaCanGuildPromote(lua_State* L);
int LuaCanGuildRemove(lua_State* L);
int LuaCanWithdrawGuildBankMoney(lua_State* L);
int LuaCloseGuildBankFrame(lua_State* L);
int LuaCloseGuildRoster(lua_State* L);
int LuaCloseGuildRegistrar(lua_State* L);
int LuaCloseTabardCreation(lua_State* L);
int LuaDeclineGuild(lua_State* L);
int LuaDepositGuildBankMoney(lua_State* L);
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
int LuaGuildInfo(lua_State* L);
int LuaGuildRosterSetOfficerNote(lua_State* L);
int LuaGuildRosterSetPublicNote(lua_State* L);
int LuaIsGuildLeader(lua_State* L);
int LuaOfferPetition(lua_State* L);
int LuaQueryGuildEventLog(lua_State* L);
int LuaSetGuildBankTabInfo(lua_State* L);
int LuaSetGuildBankTabPermissions(lua_State* L);
int LuaSetGuildBankText(lua_State* L);
int LuaSetGuildBankWithdrawLimit(lua_State* L);
int LuaSetGuildRosterSelection(lua_State* L);
int LuaWithdrawGuildBankMoney(lua_State* L);
int LuaGuildControlAddRank(lua_State* L);
int LuaGuildControlSaveRank(lua_State* L);
int LuaAutoStoreGuildBankItem(lua_State* L);
int LuaPickupGuildBankItem(lua_State* L);
int LuaPickupGuildBankMoney(lua_State* L);
int LuaSetGuildBankTabWithdraw(lua_State* L);
int LuaSetGuildInfoText(lua_State* L);
int LuaSplitGuildBankItem(lua_State* L);

}

namespace openwow::ui::game {

using namespace detail;

namespace {

constexpr openwow::ui::LuaGlobalBinding kGuildLuaBindings[] = {
    {"GetGuildRosterInfo", LuaGetGuildRosterInfo},
    {"GetNumGuildMembers", LuaGetNumGuildMembers},
    {"GuildRoster", LuaGuildRoster},
    {"GetGuildInfo", LuaGetGuildInfo},
    {"IsInGuild", LuaIsInGuild},
    {"GuildInvite", LuaGuildInvite},
    {"GuildLeave", LuaGuildLeave},
    {"GuildDisband", LuaGuildDisband},
    {"GetGuildBankWithdrawMoney", LuaGetGuildBankWithdrawMoney},
    {"CanGuildBankRepair", LuaCanGuildBankRepair},
    {"GetGuildBankMoney", LuaGetGuildBankMoney},
    {"GuildControlGetNumRanks", LuaGuildControlGetNumRanks},
    {"GuildControlGetRankName", LuaGuildControlGetRankName},
    {"GuildControlSetRank", LuaGuildControlSetRank},
    {"GetGuildRosterMOTD", LuaGetGuildRosterMOTD},
    {"GetGuildRosterShowOffline", LuaGetGuildRosterShowOffline},
    {"SetGuildRosterShowOffline", LuaSetGuildRosterShowOffline},
    {"SortGuildRoster", LuaSortGuildRoster},
    {"GuildPromote", LuaGuildPromote},
    {"GuildDemote", LuaGuildDemote},
    {"GuildSetLeader", LuaGuildSetLeader},
    {"GuildSetMOTD", LuaGuildSetMOTD},
    {"GuildUninvite", LuaGuildUninvite},
    {"GetGuildInfoText", LuaGetGuildInfoText},
    {"GetNumGuildBankTabs", LuaGetNumGuildBankTabs},
    {"GetCurrentGuildBankTab", LuaGetCurrentGuildBankTab},

    {"SetCurrentGuildBankTab", LuaSetCurrentTab},
    {"GetGuildBankTabInfo", LuaGetGuildBankTabInfo},
    {"GetGuildBankItemInfo", LuaGetGuildBankItemInfo},
    {"GetGuildBankItemLink", LuaGetGuildBankItemLink},
    {"GetGuildBankTabPermissions", LuaGetGuildBankTabPermissions},
    {"QueryGuildBankTab", LuaQueryGuildBankTab},
    {"QueryGuildBankLog", LuaQueryGuildBankLog},
    {"QueryGuildBankText", LuaQueryGuildBankText},
    {"GetNumGuildBankMoneyTransactions", LuaGetNumGuildBankMoneyTransactions},
    {"GetGuildBankMoneyTransaction", LuaGetGuildBankMoneyTransaction},
    {"GetNumGuildBankTransactions", LuaGetNumGuildBankTransactions},
    {"GetGuildBankTransaction", LuaGetGuildBankTransaction},
    {"AcceptGuild", LuaAcceptGuild},
    {"BuyGuildBankTab", LuaBuyGuildBankTab},
    {"BuyGuildCharter", LuaBuyGuildCharter},
    {"CalendarMassInviteGuild", LuaCalendarMassInviteGuild},
    {"CanEditGuildEvent", LuaCanEditGuildEvent},
    {"CanEditGuildInfo", LuaCanEditGuildInfo},
    {"CanEditGuildTabInfo", LuaCanEditGuildTabInfo},
    {"CanGuildDemote", LuaCanGuildDemote},
    {"CanGuildInvite", LuaCanGuildInvite},
    {"CanGuildPromote", LuaCanGuildPromote},
    {"CanGuildRemove", LuaCanGuildRemove},
    {"CanWithdrawGuildBankMoney", LuaCanWithdrawGuildBankMoney},
    {"CloseGuildBankFrame", LuaCloseGuildBankFrame},
    {"CloseGuildRoster", LuaCloseGuildRoster},
    {"CloseGuildRegistrar", LuaCloseGuildRegistrar},
    {"CloseTabardCreation", LuaCloseTabardCreation},
    {"DeclineGuild", LuaDeclineGuild},
    {"DepositGuildBankMoney", LuaDepositGuildBankMoney},
    {"GetGuildBankTabCost", LuaGetGuildBankTabCost},
    {"GetGuildBankText", LuaGetGuildBankText},
    {"GetGuildBankWithdrawLimit", LuaGetGuildBankWithdrawLimit},
    {"GetGuildCharterCost", LuaGetGuildCharterCost},
    {"GetGuildEventInfo", LuaGetGuildEventInfo},
    {"GetGuildRosterLastOnline", LuaGetGuildRosterLastOnline},
    {"GetGuildRosterSelection", LuaGetGuildRosterSelection},
    {"GetGuildTabardFileNames", LuaGetGuildTabardFileNames},
    {"GetNumGuildEvents", LuaGetNumGuildEvents},
    {"GetTabardCreationCost", LuaGetTabardCreationCost},
    {"GetTabardInfo", LuaGetTabardInfo},
    {"GuildControlGetRankFlags", LuaGuildControlGetRankFlags},
    {"GuildControlSetRankFlag", LuaGuildControlSetRankFlag},
    {"GuildControlDelRank", LuaGuildControlDelRank},
    {"GuildInfo", LuaGuildInfo},
    {"GuildRosterSetOfficerNote", LuaGuildRosterSetOfficerNote},
    {"GuildRosterSetPublicNote", LuaGuildRosterSetPublicNote},
    {"IsGuildLeader", LuaIsGuildLeader},
    {"OfferPetition", LuaOfferPetition},
    {"QueryGuildEventLog", LuaQueryGuildEventLog},
    {"SetGuildBankTabInfo", LuaSetGuildBankTabInfo},
    {"SetGuildBankTabPermissions", LuaSetGuildBankTabPermissions},
    {"SetGuildBankText", LuaSetGuildBankText},
    {"SetGuildBankWithdrawLimit", LuaSetGuildBankWithdrawLimit},
    {"SetGuildRosterSelection", LuaSetGuildRosterSelection},
    {"WithdrawGuildBankMoney", LuaWithdrawGuildBankMoney},
    {"GuildControlAddRank", LuaGuildControlAddRank},
    {"GuildControlSaveRank", LuaGuildControlSaveRank},
    {"AutoStoreGuildBankItem", LuaAutoStoreGuildBankItem},
    {"PickupGuildBankItem", LuaPickupGuildBankItem},
    {"PickupGuildBankMoney", LuaPickupGuildBankMoney},
    {"SetGuildBankTabWithdraw", LuaSetGuildBankTabWithdraw},
    {"SetGuildInfoText", LuaSetGuildInfoText},
    {"SplitGuildBankItem", LuaSplitGuildBankItem},
};

}

openwow::ui::lua::NativeBindingCatalog GuildNativeBindingCatalog() {
  return openwow::ui::lua::NativeFunctionCatalog(
      "game.social.guilds", openwow::ui::lua::BindingScope::kWorld, kGuildLuaBindings);
}

}

#include "openwow/game/social/contacts/adapters/lua/social_lua_bindings.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/runtime/lua/lua_composition.h"

extern "C" {
#include <lua.hpp>
}

namespace openwow::ui::game::detail {

int LuaGetNumFriends(lua_State* L);
int LuaGetFriendInfo(lua_State* L);
int LuaAddFriend(lua_State* L);
int LuaRemoveFriend(lua_State* L);
int LuaSetSelectedFriend(lua_State* L);
int LuaGetSelectedFriend(lua_State* L);
int LuaGetNumIgnores(lua_State* L);
int LuaGetIgnoreName(lua_State* L);
int LuaAddIgnore(lua_State* L);
int LuaDelIgnore(lua_State* L);
int LuaInviteUnit(lua_State* L);
int LuaUninviteUnit(lua_State* L);
int LuaAcceptGroup(lua_State* L);
int LuaDeclineGroup(lua_State* L);
int LuaLeaveParty(lua_State* L);
int LuaGetNumPartyMembers(lua_State* L);
int LuaGetNumRaidMembers(lua_State* L);
int LuaGetPartyLeaderIndex(lua_State* L);
int LuaGetRaidRosterInfo(lua_State* L);
int LuaSendWho(lua_State* L);
int LuaSetWhoToUI(lua_State* L);
int LuaGetNumWhoResults(lua_State* L);
int LuaGetWhoInfo(lua_State* L);
int LuaSortWho(lua_State* L);
int LuaBNSendWhisper(lua_State* L);
int LuaSetRaidSubgroup(lua_State* L);
int LuaSwapRaidSubgroup(lua_State* L);
int LuaPromoteToLeader(lua_State* L);
int LuaDemoteAssistant(lua_State* L);
int LuaConvertToRaid(lua_State* L);
int LuaGetReadyCheckStatus(lua_State* L);
int LuaDoReadyCheck(lua_State* L);
int LuaConfirmReadyCheck(lua_State* L);
int LuaGetReadyCheckTimeLeft(lua_State* L);
int LuaGetRaidTargetIndex(lua_State* L);
int LuaSetRaidTarget(lua_State* L);
int LuaPromoteToAssistant(lua_State* L);
int LuaIsPartyLeader(lua_State* L);
int LuaIsRaidLeader(lua_State* L);
int LuaIsRealPartyLeader(lua_State* L);
int LuaIsRealRaidLeader(lua_State* L);
int LuaIsRaidOfficer(lua_State* L);
int LuaRequestRaidInfo(lua_State* L);
int LuaAddMute(lua_State* L);
int LuaAddOrDelIgnore(lua_State* L);
int LuaApi_AddOrDelMute(lua_State* L);
int LuaApi_AddOrRemoveFriend(lua_State* L);
int LuaBNGetFriendInfo(lua_State* L);
int LuaBNGetFriendInfoByID(lua_State* L);
int LuaBNGetInfo(lua_State* L);
int LuaBNGetNumFriends(lua_State* L);
int LuaBNGetSelectedBlock(lua_State* L);
int LuaBNGetSelectedFriend(lua_State* L);
int LuaBNIsFriend(lua_State* L);
int LuaBNIsSelf(lua_State* L);
int LuaBNSendFriendInvite(lua_State* L);
int LuaBNSetCustomMessage(lua_State* L);
int LuaBNSetSelectedBlock(lua_State* L);
int LuaBNSetSelectedFriend(lua_State* L);
int LuaCancelSummon(lua_State* L);
int LuaClearPartyAssignment(lua_State* L);
int LuaDeclineInvite(lua_State* L);
int LuaDelMute(lua_State* L);
int LuaGetMuteName(lua_State* L);
int LuaGetNumMutes(lua_State* L);
int LuaGetPartyAssignment(lua_State* L);
int LuaGetPartyMember(lua_State* L);
int LuaGetRaidRosterSelection(lua_State* L);
int LuaGetRealNumPartyMembers(lua_State* L);
int LuaGetRealNumRaidMembers(lua_State* L);
int LuaCanSummonFriend(lua_State* L);
int LuaIsReferAFriendLinked(lua_State* L);
int LuaGetSelectedIgnore(lua_State* L);
int LuaGetSummonFriendCooldown(lua_State* L);
int LuaIsIgnored(lua_State* L);
int LuaIsIgnoredOrMuted(lua_State* L);
int LuaIsMuted(lua_State* L);
int LuaSetFriendNotes(lua_State* L);
int LuaSetPartyAssignment(lua_State* L);
int LuaSetSelectedIgnore(lua_State* L);
int LuaShowFriends(lua_State* L);
int LuaSummonFriend(lua_State* L);
int LuaSetRaidRosterSelection(lua_State* L);
int LuaSetSelectedMute(lua_State* L);
int LuaGetSelectedMute(lua_State* L);
int LuaBNConnected(lua_State* L);
int LuaBNFeaturesEnabled(lua_State* L);
int LuaBNFeaturesEnabledAndConnected(lua_State* L);
int LuaIsBNLogin(lua_State* L);
int LuaBNGetNumFriendToons(lua_State* L);
int LuaBNRemoveFriend(lua_State* L);
int LuaBNSetFriendNote(lua_State* L);
int LuaBNGetNumFriendInvites(lua_State* L);
int LuaBNGetFriendInviteInfo(lua_State* L);
int LuaBNSendFriendInviteByID(lua_State* L);
int LuaBNAcceptFriendInvite(lua_State* L);
int LuaBNDeclineFriendInvite(lua_State* L);
int LuaBNReportFriendInvite(lua_State* L);
int LuaBNSetAFK(lua_State* L);
int LuaBNSetDND(lua_State* L);
int LuaBNGetCustomMessageTable(lua_State* L);
int LuaBNSetFocus(lua_State* L);
int LuaBNCreateConversation(lua_State* L);
int LuaBNInviteToConversation(lua_State* L);
int LuaBNLeaveConversation(lua_State* L);
int LuaBNSendConversationMessage(lua_State* L);
int LuaBNGetNumConversationMembers(lua_State* L);
int LuaBNGetConversationInfo(lua_State* L);
int LuaBNGetNumBlocked(lua_State* L);
int LuaBNIsBlocked(lua_State* L);
int LuaBNSetBlocked(lua_State* L);
int LuaBNGetNumBlockedToons(lua_State* L);
int LuaBNGetBlockedToonInfo(lua_State* L);
int LuaBNIsToonBlocked(lua_State* L);
int LuaBNSetToonBlocked(lua_State* L);
int LuaBNSetSelectedToonBlock(lua_State* L);
int LuaBNGetSelectedToonBlock(lua_State* L);
int LuaBNReportPlayer(lua_State* L);
int LuaBNGetNumFOF(lua_State* L);
int LuaBNGetFOFInfo(lua_State* L);
int LuaBNRequestFOF(lua_State* L);
int LuaBNSetMatureLanguageFilter(lua_State* L);
int LuaBNGetMatureLanguageFilter(lua_State* L);
int LuaBNGetMaxPlayersInConversation(lua_State* L);
int LuaBNGetFriendToonInfo(lua_State* L);
int LuaBNGetToonInfo(lua_State* L);
int LuaBNGetConversationMemberInfo(lua_State* L);
int LuaBNListConversation(lua_State* L);
int LuaBNGetBlockedInfo(lua_State* L);

}

namespace openwow::ui::game {

using namespace detail;

namespace {

constexpr openwow::ui::LuaGlobalBinding kSocialLuaBindings[] = {
    {"GetNumFriends", LuaGetNumFriends},
    {"GetFriendInfo", LuaGetFriendInfo},
    {"AddFriend", LuaAddFriend},
    {"RemoveFriend", LuaRemoveFriend},
    {"SetSelectedFriend", LuaSetSelectedFriend},
    {"GetSelectedFriend", LuaGetSelectedFriend},
    {"GetNumIgnores", LuaGetNumIgnores},
    {"GetIgnoreName", LuaGetIgnoreName},
    {"AddIgnore", LuaAddIgnore},
    {"DelIgnore", LuaDelIgnore},
    {"InviteUnit", LuaInviteUnit},
    {"UninviteUnit", LuaUninviteUnit},
    {"AcceptGroup", LuaAcceptGroup},
    {"DeclineGroup", LuaDeclineGroup},
    {"LeaveParty", LuaLeaveParty},
    {"GetNumPartyMembers", LuaGetNumPartyMembers},
    {"GetNumRaidMembers", LuaGetNumRaidMembers},
    {"GetPartyLeaderIndex", LuaGetPartyLeaderIndex},
    {"GetRaidRosterInfo", LuaGetRaidRosterInfo},
    {"SendWho", LuaSendWho},
    {"SetWhoToUI", LuaSetWhoToUI},
    {"GetNumWhoResults", LuaGetNumWhoResults},
    {"GetWhoInfo", LuaGetWhoInfo},
    {"SortWho", LuaSortWho},
    {"BNSendWhisper", LuaBNSendWhisper},
    {"SetRaidSubgroup", LuaSetRaidSubgroup},
    {"SwapRaidSubgroup", LuaSwapRaidSubgroup},
    {"PromoteToLeader", LuaPromoteToLeader},
    {"DemoteAssistant", LuaDemoteAssistant},
    {"ConvertToRaid", LuaConvertToRaid},
    {"GetReadyCheckStatus", LuaGetReadyCheckStatus},
    {"DoReadyCheck", LuaDoReadyCheck},
    {"ConfirmReadyCheck", LuaConfirmReadyCheck},
    {"GetReadyCheckTimeLeft", LuaGetReadyCheckTimeLeft},
    {"GetRaidTargetIndex", LuaGetRaidTargetIndex},
    {"SetRaidTarget", LuaSetRaidTarget},
    {"PromoteToAssistant", LuaPromoteToAssistant},
    {"IsPartyLeader", LuaIsPartyLeader},
    {"IsRaidLeader", LuaIsRaidLeader},
    {"IsRealPartyLeader", LuaIsRealPartyLeader},
    {"IsRealRaidLeader", LuaIsRealRaidLeader},
    {"IsRaidOfficer", LuaIsRaidOfficer},
    {"RequestRaidInfo", LuaRequestRaidInfo},
    {"AddMute", LuaAddMute},
    {"AddOrDelIgnore", LuaAddOrDelIgnore},
    {"AddOrDelMute", LuaApi_AddOrDelMute},
    {"AddOrRemoveFriend", LuaApi_AddOrRemoveFriend},
    {"BNGetFriendInfo", LuaBNGetFriendInfo},
    {"BNGetFriendInfoByID", LuaBNGetFriendInfoByID},
    {"BNGetInfo", LuaBNGetInfo},
    {"BNGetNumFriends", LuaBNGetNumFriends},
    {"BNGetSelectedBlock", LuaBNGetSelectedBlock},
    {"BNGetSelectedFriend", LuaBNGetSelectedFriend},
    {"BNIsFriend", LuaBNIsFriend},
    {"BNIsSelf", LuaBNIsSelf},
    {"BNSendFriendInvite", LuaBNSendFriendInvite},
    {"BNSetCustomMessage", LuaBNSetCustomMessage},
    {"BNSetSelectedBlock", LuaBNSetSelectedBlock},
    {"BNSetSelectedFriend", LuaBNSetSelectedFriend},
    {"CancelSummon", LuaCancelSummon},
    {"ClearPartyAssignment", LuaClearPartyAssignment},
    {"DeclineInvite", LuaDeclineInvite},
    {"DelMute", LuaDelMute},
    {"GetMuteName", LuaGetMuteName},
    {"GetNumMutes", LuaGetNumMutes},
    {"GetPartyAssignment", LuaGetPartyAssignment},
    {"GetPartyMember", LuaGetPartyMember},
    {"GetRaidRosterSelection", LuaGetRaidRosterSelection},
    {"GetRealNumPartyMembers", LuaGetRealNumPartyMembers},
    {"GetRealNumRaidMembers", LuaGetRealNumRaidMembers},
    {"CanSummonFriend", LuaCanSummonFriend},
    {"IsReferAFriendLinked", LuaIsReferAFriendLinked},
    {"GetSelectedIgnore", LuaGetSelectedIgnore},
    {"GetSummonFriendCooldown", LuaGetSummonFriendCooldown},
    {"IsIgnored", LuaIsIgnored},
    {"IsIgnoredOrMuted", LuaIsIgnoredOrMuted},
    {"IsMuted", LuaIsMuted},
    {"SetFriendNotes", LuaSetFriendNotes},
    {"SetPartyAssignment", LuaSetPartyAssignment},
    {"SetSelectedIgnore", LuaSetSelectedIgnore},
    {"ShowFriends", LuaShowFriends},
    {"SummonFriend", LuaSummonFriend},
    {"SetRaidRosterSelection", LuaSetRaidRosterSelection},
    {"SetSelectedMute", LuaSetSelectedMute},
    {"GetSelectedMute", LuaGetSelectedMute},
    {"BNConnected", LuaBNConnected},
    {"BNFeaturesEnabled", LuaBNFeaturesEnabled},
    {"BNFeaturesEnabledAndConnected", LuaBNFeaturesEnabledAndConnected},
    {"IsBNLogin", LuaIsBNLogin},
    {"BNGetNumFriendToons", LuaBNGetNumFriendToons},
    {"BNRemoveFriend", LuaBNRemoveFriend},
    {"BNSetFriendNote", LuaBNSetFriendNote},
    {"BNGetNumFriendInvites", LuaBNGetNumFriendInvites},
    {"BNGetFriendInviteInfo", LuaBNGetFriendInviteInfo},
    {"BNSendFriendInviteByID", LuaBNSendFriendInviteByID},
    {"BNAcceptFriendInvite", LuaBNAcceptFriendInvite},
    {"BNDeclineFriendInvite", LuaBNDeclineFriendInvite},
    {"BNReportFriendInvite", LuaBNReportFriendInvite},
    {"BNSetAFK", LuaBNSetAFK},
    {"BNSetDND", LuaBNSetDND},
    {"BNGetCustomMessageTable", LuaBNGetCustomMessageTable},
    {"BNSetFocus", LuaBNSetFocus},
    {"BNCreateConversation", LuaBNCreateConversation},
    {"BNInviteToConversation", LuaBNInviteToConversation},
    {"BNLeaveConversation", LuaBNLeaveConversation},
    {"BNSendConversationMessage", LuaBNSendConversationMessage},
    {"BNGetNumConversationMembers", LuaBNGetNumConversationMembers},
    {"BNGetConversationInfo", LuaBNGetConversationInfo},
    {"BNGetNumBlocked", LuaBNGetNumBlocked},
    {"BNIsBlocked", LuaBNIsBlocked},
    {"BNSetBlocked", LuaBNSetBlocked},
    {"BNGetNumBlockedToons", LuaBNGetNumBlockedToons},
    {"BNGetBlockedToonInfo", LuaBNGetBlockedToonInfo},
    {"BNIsToonBlocked", LuaBNIsToonBlocked},
    {"BNSetToonBlocked", LuaBNSetToonBlocked},
    {"BNSetSelectedToonBlock", LuaBNSetSelectedToonBlock},
    {"BNGetSelectedToonBlock", LuaBNGetSelectedToonBlock},
    {"BNReportPlayer", LuaBNReportPlayer},
    {"BNGetNumFOF", LuaBNGetNumFOF},
    {"BNGetFOFInfo", LuaBNGetFOFInfo},

    {"BNRequestFOFInfo", LuaBNRequestFOF},
    {"BNSetMatureLanguageFilter", LuaBNSetMatureLanguageFilter},
    {"BNGetMatureLanguageFilter", LuaBNGetMatureLanguageFilter},
    {"BNGetMaxPlayersInConversation", LuaBNGetMaxPlayersInConversation},
    {"BNGetFriendToonInfo", LuaBNGetFriendToonInfo},
    {"BNGetToonInfo", LuaBNGetToonInfo},
    {"BNGetConversationMemberInfo", LuaBNGetConversationMemberInfo},
    {"BNListConversation", LuaBNListConversation},
    {"BNGetBlockedInfo", LuaBNGetBlockedInfo},
};

}

openwow::ui::lua::NativeBindingCatalog SocialNativeBindingCatalog() {
  return openwow::ui::lua::NativeFunctionCatalog(
      "game.social.contacts", openwow::ui::lua::BindingScope::kWorld, kSocialLuaBindings);
}

}

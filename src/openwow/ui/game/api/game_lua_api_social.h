
#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaSendChatMessage(lua_State* L);
int LuaGetDefaultLanguage(lua_State* L);

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

int LuaGetGuildRosterInfo(lua_State* L);
int LuaGetNumGuildMembers(lua_State* L);
int LuaGuildRoster(lua_State* L);
int LuaGetGuildInfo(lua_State* L);
int LuaIsInGuild(lua_State* L);
int LuaGuildInvite(lua_State* L);
int LuaGuildLeave(lua_State* L);
int LuaGuildDisband(lua_State* L);

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

int LuaSetRaidSubgroup(lua_State* L);
int LuaSwapRaidSubgroup(lua_State* L);
int LuaPromoteToLeader(lua_State* L);
int LuaDemoteAssistant(lua_State* L);
int LuaUnitIsRaidOfficer(lua_State* L);
int LuaConvertToRaid(lua_State* L);
int LuaGetReadyCheckStatus(lua_State* L);
int LuaDoReadyCheck(lua_State* L);
int LuaConfirmReadyCheck(lua_State* L);
int LuaGetReadyCheckTimeLeft(lua_State* L);

int LuaAddMute(lua_State* L);
int LuaAddOrDelIgnore(lua_State* L);
int LuaApi_AddOrDelMute(lua_State* L);
int LuaApi_AddOrRemoveFriend(lua_State* L);
int LuaBNSetCustomMessage(lua_State* L);
void ResetBNetCustomMessageThrottleForTesting();
int LuaBNSetSelectedBlock(lua_State* L);
int LuaBNSetSelectedFriend(lua_State* L);
int LuaCancelSummon(lua_State* L);
int LuaDelMute(lua_State* L);
int LuaSetFriendNotes(lua_State* L);
int LuaSetSelectedIgnore(lua_State* L);
int LuaShowFriends(lua_State* L);
int LuaCanSummonFriend(lua_State* L);
int LuaCanGrantLevel(lua_State* L);
int LuaIsReferAFriendLinked(lua_State* L);
int LuaGrantLevel(lua_State* L);
int LuaSummonFriend(lua_State* L);
int LuaBNSendFriendInvite(lua_State* L);
int LuaBNGetFriendInfo(lua_State* L);
int LuaBNGetFriendInfoByID(lua_State* L);
int LuaBNGetInfo(lua_State* L);
int LuaBNGetNumFriends(lua_State* L);
int LuaBNGetSelectedBlock(lua_State* L);
int LuaBNGetSelectedFriend(lua_State* L);
int LuaBNIsFriend(lua_State* L);
int LuaBNIsSelf(lua_State* L);
int LuaGetMuteName(lua_State* L);
int LuaGetNumMutes(lua_State* L);
int LuaGetSelectedIgnore(lua_State* L);
int LuaSetSelectedMute(lua_State* L);
int LuaGetSelectedMute(lua_State* L);
int LuaGetSummonFriendCooldown(lua_State* L);
int LuaIsIgnored(lua_State* L);
int LuaIsIgnoredOrMuted(lua_State* L);
int LuaIsMuted(lua_State* L);

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

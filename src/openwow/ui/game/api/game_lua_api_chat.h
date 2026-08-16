
#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaEnumerateServerChannels(lua_State *L);
int LuaGetChannelList(lua_State *L);
int LuaChannelToggleAnnouncements(lua_State *L);
int LuaChannelVoiceOn(lua_State *L);
int LuaChannelVoiceOff(lua_State *L);
int LuaDisplayChannelVoiceOn(lua_State *L);
int LuaDisplayChannelVoiceOff(lua_State *L);
int LuaChannelModerator(lua_State *L);
int LuaChannelUnmoderator(lua_State *L);
int LuaChannelKick(lua_State *L);
int LuaChannelBan(lua_State *L);
int LuaChannelUnban(lua_State *L);
int LuaChannelInvite(lua_State *L);
int LuaListChannels(lua_State *L);
int LuaGetChannelRosterInfo(lua_State *L);
int LuaIsSilenced(lua_State *L);
int LuaGetSelectedDisplayChannel(lua_State *L);
int LuaSetSelectedDisplayChannel(lua_State *L);
int LuaIsDisplayChannelOwner(lua_State *L);
int LuaIsDisplayChannelModerator(lua_State *L);
int LuaChannelMute(lua_State *L);
int LuaChannelUnmute(lua_State *L);
int LuaChannelSilenceAll(lua_State *L);
int LuaChannelSilenceVoice(lua_State *L);
int LuaChannelUnSilenceAll(lua_State *L);
int LuaChannelUnSilenceVoice(lua_State *L);
int LuaSilenceMember(lua_State *L);
int LuaUnSilenceMember(lua_State *L);
int LuaDisplayChannelOwner(lua_State *L);

int LuaLoggingChat(lua_State *L);
int LuaLoggingCombat(lua_State *L);

int LuaAddChatWindowChannel(lua_State *L);
int LuaAddChatWindowMessages(lua_State *L);
int LuaChangeChatColor(lua_State *L);
int LuaResetChatColors(lua_State *L);
int LuaResetChatWindows(lua_State *L);
int LuaJoinPermanentChannel(lua_State *L);
int LuaJoinTemporaryChannel(lua_State *L);
int LuaRemoveChatWindowChannel(lua_State *L);
int LuaRemoveChatWindowMessages(lua_State *L);
int LuaSetChannelOwner(lua_State *L);
int LuaSetChannelPassword(lua_State *L);
int LuaSetChatWindowAlpha(lua_State *L);
int LuaSetChatWindowColor(lua_State *L);
int LuaSetChatWindowDocked(lua_State *L);
int LuaSetChatWindowLocked(lua_State *L);
int LuaSetChatWindowName(lua_State *L);
int LuaSetChatWindowShown(lua_State *L);
int LuaSetChatWindowSize(lua_State *L);
int LuaSetChatWindowUninteractable(lua_State *L);
int LuaCanComplainChat(lua_State *L);
int LuaGetChatTypeIndex(lua_State *L);
int LuaGetChatWindowChannels(lua_State *L);
int LuaGetChatWindowInfo(lua_State *L);
int LuaGetChatWindowMessages(lua_State *L);
int LuaGetChatWindowSavedPosition(lua_State *L);
int LuaGetChatWindowSavedDimensions(lua_State *L);

int LuaComplainChat(lua_State *L);
int LuaSetChatColorNameByClass(lua_State *L);
int LuaSetChatWindowSavedDimensions(lua_State *L);
int LuaSetChatWindowSavedPosition(lua_State *L);

int LuaDoEmote(lua_State *L);
int LuaConsoleAddMessage(lua_State *L);
int LuaSendSystemMessage(lua_State *L);

}

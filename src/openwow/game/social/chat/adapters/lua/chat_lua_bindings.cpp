#include "openwow/game/social/chat/adapters/lua/chat_lua_bindings.h"
#include "openwow/ui/game/api/game_lua_api_sound.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/runtime/lua/lua_composition.h"

extern "C" {
#include <lua.hpp>
}

namespace openwow::ui::game::detail {

int LuaSendChatMessage(lua_State* L);
int LuaGetDefaultLanguage(lua_State* L);
int LuaGetNumLanguages(lua_State* L);
int LuaGetLanguageByIndex(lua_State* L);
int LuaEnumerateServerChannels(lua_State* L);
int LuaJoinChannelByName(lua_State* L);
int LuaLeaveChannelByName(lua_State* L);
int LuaGetChannelName(lua_State* L);
int LuaExpandChannelHeader(lua_State* L);
int LuaCollapseChannelHeader(lua_State* L);
int LuaGetNumDisplayChannels(lua_State* L);
int LuaGetChannelDisplayInfo(lua_State* L);
int LuaListChannelByName(lua_State* L);
int LuaSendAddonMessage(lua_State* L);
int LuaGetAutoCompleteResults(lua_State* L);
int LuaGetChannelList(lua_State* L);
int LuaChannelToggleAnnouncements(lua_State* L);
int LuaChannelVoiceOn(lua_State* L);
int LuaChannelVoiceOff(lua_State* L);
int LuaDisplayChannelVoiceOn(lua_State* L);
int LuaDisplayChannelVoiceOff(lua_State* L);
int LuaChannelModerator(lua_State* L);
int LuaChannelUnmoderator(lua_State* L);
int LuaChannelKick(lua_State* L);
int LuaChannelBan(lua_State* L);
int LuaChannelUnban(lua_State* L);
int LuaChannelInvite(lua_State* L);
int LuaListChannels(lua_State* L);
int LuaIsDisplayChannelOwner(lua_State* L);
int LuaIsDisplayChannelModerator(lua_State* L);
int LuaChannelMute(lua_State* L);
int LuaChannelUnmute(lua_State* L);
int LuaChannelSilenceAll(lua_State* L);
int LuaChannelSilenceVoice(lua_State* L);
int LuaChannelUnSilenceAll(lua_State* L);
int LuaChannelUnSilenceVoice(lua_State* L);
int LuaDisplayChannelOwner(lua_State* L);
int LuaLoggingChat(lua_State* L);
int LuaLoggingCombat(lua_State* L);
int LuaComplainChat(lua_State* L);
int LuaSetChatColorNameByClass(lua_State* L);
int LuaSetChatWindowSavedDimensions(lua_State* L);
int LuaSetChatWindowSavedPosition(lua_State* L);
int LuaAddChatWindowChannel(lua_State* L);
int LuaAddChatWindowMessages(lua_State* L);
int LuaCanComplainChat(lua_State* L);
int LuaChangeChatColor(lua_State* L);
int LuaResetChatColors(lua_State* L);
int LuaResetChatWindows(lua_State* L);
int LuaGetChatTypeIndex(lua_State* L);
int LuaGetChatWindowChannels(lua_State* L);
int LuaGetChatWindowInfo(lua_State* L);
int LuaGetChatWindowMessages(lua_State* L);
int LuaGetChatWindowSavedPosition(lua_State* L);
int LuaGetChatWindowSavedDimensions(lua_State* L);
int LuaJoinPermanentChannel(lua_State* L);
int LuaJoinTemporaryChannel(lua_State* L);
int LuaRemoveChatWindowChannel(lua_State* L);
int LuaRemoveChatWindowMessages(lua_State* L);
int LuaSetChatWindowAlpha(lua_State* L);
int LuaSetChatWindowColor(lua_State* L);
int LuaSetChatWindowDocked(lua_State* L);
int LuaSetChatWindowLocked(lua_State* L);
int LuaSetChatWindowName(lua_State* L);
int LuaSetChatWindowShown(lua_State* L);
int LuaSetChatWindowSize(lua_State* L);
int LuaSetChatWindowUninteractable(lua_State* L);
int LuaGetAutoCompletePresenceID(lua_State* L);

}

namespace openwow::ui::game {

using namespace detail;

namespace {

constexpr openwow::ui::LuaGlobalBinding kChatLuaBindings[] = {
    {"SendChatMessage", LuaSendChatMessage},
    {"GetDefaultLanguage", LuaGetDefaultLanguage},
    {"GetNumLanguages", LuaGetNumLanguages},
    {"GetLanguageByIndex", LuaGetLanguageByIndex},
    {"EnumerateServerChannels", LuaEnumerateServerChannels},
    {"JoinChannelByName", LuaJoinChannelByName},
    {"LeaveChannelByName", LuaLeaveChannelByName},
    {"GetChannelName", LuaGetChannelName},
    {"ExpandChannelHeader", LuaExpandChannelHeader},
    {"CollapseChannelHeader", LuaCollapseChannelHeader},
    {"GetNumDisplayChannels", LuaGetNumDisplayChannels},
    {"GetChannelDisplayInfo", LuaGetChannelDisplayInfo},
    {"ListChannelByName", LuaListChannelByName},
    {"SendAddonMessage", LuaSendAddonMessage},
    {"GetAutoCompleteResults", LuaGetAutoCompleteResults},
    {"GetChannelList", LuaGetChannelList},
    {"ChannelToggleAnnouncements", LuaChannelToggleAnnouncements},
    {"ChannelVoiceOn", LuaChannelVoiceOn},
    {"ChannelVoiceOff", LuaChannelVoiceOff},
    {"DisplayChannelVoiceOn", LuaDisplayChannelVoiceOn},
    {"DisplayChannelVoiceOff", LuaDisplayChannelVoiceOff},
    {"ChannelModerator", LuaChannelModerator},
    {"ChannelUnmoderator", LuaChannelUnmoderator},
    {"ChannelKick", LuaChannelKick},
    {"ChannelBan", LuaChannelBan},
    {"ChannelUnban", LuaChannelUnban},
    {"ChannelInvite", LuaChannelInvite},
    {"ListChannels", LuaListChannels},
    {"IsDisplayChannelOwner", LuaIsDisplayChannelOwner},
    {"IsDisplayChannelModerator", LuaIsDisplayChannelModerator},
    {"ChannelMute", LuaChannelMute},
    {"ChannelUnmute", LuaChannelUnmute},
    {"ChannelSilenceAll", LuaChannelSilenceAll},
    {"ChannelSilenceVoice", LuaChannelSilenceVoice},
    {"ChannelUnSilenceAll", LuaChannelUnSilenceAll},
    {"ChannelUnSilenceVoice", LuaChannelUnSilenceVoice},
    {"DisplayChannelOwner", LuaDisplayChannelOwner},
    {"LoggingChat", LuaLoggingChat},
    {"LoggingCombat", LuaLoggingCombat},
    {"ComplainChat", LuaComplainChat},
    {"SetChatColorNameByClass", LuaSetChatColorNameByClass},
    {"SetChatWindowSavedDimensions", LuaSetChatWindowSavedDimensions},
    {"SetChatWindowSavedPosition", LuaSetChatWindowSavedPosition},
    {"AddChatWindowChannel", LuaAddChatWindowChannel},
    {"AddChatWindowMessages", LuaAddChatWindowMessages},
    {"CanComplainChat", LuaCanComplainChat},
    {"ChangeChatColor", LuaChangeChatColor},
    {"ResetChatColors", LuaResetChatColors},
    {"ResetChatWindows", LuaResetChatWindows},
    {"GetChatTypeIndex", LuaGetChatTypeIndex},
    {"GetChatWindowChannels", LuaGetChatWindowChannels},
    {"GetChatWindowInfo", LuaGetChatWindowInfo},
    {"GetChatWindowMessages", LuaGetChatWindowMessages},
    {"GetChatWindowSavedPosition", LuaGetChatWindowSavedPosition},
    {"GetChatWindowSavedDimensions", LuaGetChatWindowSavedDimensions},
    {"GetNumChannelMembers", detail::kGetNumChannelMembers.handler},
    {"JoinPermanentChannel", LuaJoinPermanentChannel},
    {"JoinTemporaryChannel", LuaJoinTemporaryChannel},
    {"RemoveChatWindowChannel", LuaRemoveChatWindowChannel},
    {"RemoveChatWindowMessages", LuaRemoveChatWindowMessages},
    {"SetChatWindowAlpha", LuaSetChatWindowAlpha},
    {"SetChatWindowColor", LuaSetChatWindowColor},
    {"SetChatWindowDocked", LuaSetChatWindowDocked},
    {"SetChatWindowLocked", LuaSetChatWindowLocked},
    {"SetChatWindowName", LuaSetChatWindowName},
    {"SetChatWindowShown", LuaSetChatWindowShown},
    {"SetChatWindowSize", LuaSetChatWindowSize},
    {"SetChatWindowUninteractable", LuaSetChatWindowUninteractable},
    {"GetAutoCompletePresenceID", LuaGetAutoCompletePresenceID},
};

}

openwow::ui::lua::NativeBindingCatalog ChatNativeBindingCatalog() {
  return openwow::ui::lua::NativeFunctionCatalog(
      "game.social.chat", openwow::ui::lua::BindingScope::kWorld, kChatLuaBindings);
}

}

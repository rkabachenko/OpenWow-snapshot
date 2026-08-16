
#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaGetNumLanguages(lua_State* L);
int LuaGetLanguageByIndex(lua_State* L);

int LuaJoinChannelImpl(lua_State* L, bool permanent, const char* api_name);
int LuaJoinChannelByName(lua_State* L);
int LuaLeaveChannelByName(lua_State* L);
int LuaGetChannelName(lua_State* L);
int LuaExpandChannelHeader(lua_State* L);
int LuaCollapseChannelHeader(lua_State* L);
int LuaGetNumDisplayChannels(lua_State* L);
int LuaGetChannelDisplayInfo(lua_State* L);
int LuaListChannelByName(lua_State* L);

int LuaSendAddonMessage(lua_State* L);

int LuaBNSendWhisper(lua_State* L);
int LuaGetAutoCompleteResults(lua_State* L);

}

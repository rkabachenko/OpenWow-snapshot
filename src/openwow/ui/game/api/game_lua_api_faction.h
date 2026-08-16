
#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaGetNumFactions(lua_State* L);
int LuaGetFactionInfo(lua_State* L);
int LuaGetFactionInfoByID(lua_State* L);
int LuaExpandFactionHeader(lua_State* L);
int LuaCollapseFactionHeader(lua_State* L);
int LuaCollapseAllFactionHeaders(lua_State* L);
int LuaExpandAllFactionHeaders(lua_State* L);
int LuaSetFactionActive(lua_State* L);
int LuaSetFactionInactive(lua_State* L);
int LuaIsFactionInactive(lua_State* L);
int LuaSetWatchedFactionIndex(lua_State* L);
int LuaGetWatchedFactionInfo(lua_State* L);
int LuaGetSelectedFaction(lua_State* L);
int LuaSetSelectedFaction(lua_State* L);
int LuaFactionToggleAtWar(lua_State* L);
int LuaGetText(lua_State* L);

}


#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaGetArenaTeamRosterInfo(lua_State* L);
int LuaGetNumArenaTeamMembers(lua_State* L);
int LuaAcceptArenaTeam(lua_State* L);
int LuaDeclineArenaTeam(lua_State* L);
int LuaArenaTeamLeave(lua_State* L);
int LuaArenaTeamDisband(lua_State* L);
int LuaArenaTeamInviteByName(lua_State* L);
int LuaArenaTeamUninviteByName(lua_State* L);
int LuaArenaTeamSetLeaderByName(lua_State* L);
int LuaIsArenaTeamCaptain(lua_State* L);
int LuaCloseArenaTeamRoster(lua_State* L);
int LuaGetCurrentArenaSeason(lua_State* L);
int LuaGetPreviousArenaSeason(lua_State* L);

int LuaGetNumArenaOpponents(lua_State* L);
int LuaRequestBattlefieldPositions(lua_State* L);
int LuaGetNumBattlefieldPositions(lua_State* L);
int LuaGetBattlefieldPosition(lua_State* L);
int LuaSetBattlefieldScoreFaction(lua_State* L);

int LuaSortArenaTeamRoster(lua_State* L);
int LuaTurnInArenaPetition(lua_State* L);

}

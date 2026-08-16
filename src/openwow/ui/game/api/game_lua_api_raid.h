
#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaGetRaidTargetIndex(lua_State* L);
int LuaGetRaidRosterSelection(lua_State* L);
int LuaSetRaidTarget(lua_State* L);
int LuaSetRaidRosterSelection(lua_State* L);
int LuaPromoteToAssistant(lua_State* L);
int LuaIsPartyLeader(lua_State* L);
int LuaIsRaidLeader(lua_State* L);
int LuaIsRealPartyLeader(lua_State* L);
int LuaIsRealRaidLeader(lua_State* L);
int LuaIsRaidOfficer(lua_State* L);
int LuaRequestRaidInfo(lua_State* L);

int LuaClearPartyAssignment(lua_State* L);
int LuaSetPartyAssignment(lua_State* L);
int LuaGetPartyAssignment(lua_State* L);
int LuaGetPartyLFGBackfillInfo(lua_State* L);
int LuaGetPartyMember(lua_State* L);

}

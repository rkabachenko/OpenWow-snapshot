
#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaSetLFGBootVote(lua_State* L);
int LuaGetLFGBootProposal(lua_State* L);
int LuaGetRandomDungeonBestChoice(lua_State* L);
int LuaAcceptProposal(lua_State* L);
int LuaRejectProposal(lua_State* L);
int LuaLFGTeleport(lua_State* L);
int LuaGetLFGDeserterExpiration(lua_State* L);

}

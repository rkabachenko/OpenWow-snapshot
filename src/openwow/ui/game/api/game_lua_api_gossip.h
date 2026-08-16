
#pragma once

struct lua_State;

namespace openwow::game {
class WorldSession;
}

namespace openwow::ui::game::detail {

[[nodiscard]] bool PrepareCurrentGossipText(openwow::game::WorldSession& session);

int LuaCheckBinderDist(lua_State *L);
int LuaGetGossipText(lua_State *L);
int LuaGetNumGossipAvailableQuests(lua_State *L);
int LuaGetNumGossipActiveQuests(lua_State *L);
int LuaGetNumGossipOptions(lua_State *L);
int LuaGetGossipOptions(lua_State *L);
int LuaGetGossipAvailableQuests(lua_State *L);
int LuaGetGossipActiveQuests(lua_State *L);
int LuaSelectGossipOption(lua_State *L);
int LuaSelectGossipAvailableQuest(lua_State *L);
int LuaSelectGossipActiveQuest(lua_State *L);
int LuaCloseGossip(lua_State *L);
int LuaForceGossip(lua_State *L);

}


#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaIsInInstance(lua_State* L);
int LuaGetInstanceInfo(lua_State* L);
int LuaGetInstanceDifficulty(lua_State* L);

int LuaGetDungeonDifficulty(lua_State* L);
int LuaSetDungeonDifficulty(lua_State* L);
int LuaGetRaidDifficulty(lua_State* L);
int LuaSetRaidDifficulty(lua_State* L);
int LuaCanChangePlayerDifficulty(lua_State* L);
int LuaChangePlayerDifficulty(lua_State* L);
int LuaCanMapChangeDifficulty(lua_State* L);

int LuaResetInstances(lua_State* L);
int LuaGetNumSavedInstances(lua_State* L);
int LuaGetSavedInstanceInfo(lua_State* L);

int LuaGetNumWorldStateUI(lua_State* L);
int LuaGetWorldStateUIInfo(lua_State* L);

int LuaGetInstanceBootTimeRemaining(lua_State* L);
int LuaGetInstanceLockTimeRemaining(lua_State* L);
int LuaGetInstanceLockTimeRemainingEncounter(lua_State* L);

}

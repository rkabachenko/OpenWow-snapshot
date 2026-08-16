
#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaClosePetition(lua_State* L);
int LuaClosePetitionVendor(lua_State* L);
int LuaClickPetitionButton(lua_State* L);
int LuaCloseTabardCreation(lua_State* L);
int LuaHasFilledPetition(lua_State* L);
int LuaRenamePetition(lua_State* L);
int LuaSignPetition(lua_State* L);
int LuaTurnInPetition(lua_State* L);
int LuaTurnInGuildCharter(lua_State* L);
int LuaCanSignPetition(lua_State* L);
int LuaGetNumPetitionItems(lua_State* L);
int LuaGetNumPetitionNames(lua_State* L);
int LuaGetPetitionInfo(lua_State* L);
int LuaGetPetitionNameInfo(lua_State* L);

}

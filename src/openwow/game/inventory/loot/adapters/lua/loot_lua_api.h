#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaSetLootPortrait(lua_State* L);
int LuaGetNumLootItems(lua_State* L);
int LuaGetLootSlotInfo(lua_State* L);
int LuaGetLootSlotLink(lua_State* L);
int LuaLootSlot(lua_State* L);
int LuaLootSlotIsItem(lua_State* L);
int LuaLootSlotIsCoin(lua_State* L);
int LuaConfirmLootSlot(lua_State* L);
int LuaCloseLoot(lua_State* L);
int LuaIsFishingLoot(lua_State* L);
int LuaGetLootMethod(lua_State* L);
int LuaSetLootMethod(lua_State* L);
int LuaGetLootThreshold(lua_State* L);
int LuaSetLootThreshold(lua_State* L);
int LuaConfirmLootRoll(lua_State* L);
int LuaGetLootRollItemInfo(lua_State* L);
int LuaGetLootRollItemLink(lua_State* L);
int LuaGetLootRollTimeLeft(lua_State* L);
int LuaRollOnLoot(lua_State* L);
int LuaConfirmBindOnUse(lua_State* L);

int LuaGetMasterLootCandidate(lua_State* L);
int LuaGetOptOutOfLoot(lua_State* L);
int LuaGiveMasterLoot(lua_State* L);
int LuaSetOptOutOfLoot(lua_State* L);

}

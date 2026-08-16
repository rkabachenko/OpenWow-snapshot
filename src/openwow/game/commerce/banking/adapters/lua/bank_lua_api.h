#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaGetNumBankSlots(lua_State* L);
int LuaPurchaseSlot(lua_State* L);
int LuaCloseBankFrame(lua_State* L);
int LuaBankButtonIDToInvSlotID(lua_State* L);
int LuaGetBankSlotCost(lua_State* L);
int LuaGetContainerFreeSlots(lua_State* L);

}

#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaGetMerchantNumItems(lua_State* L);
int LuaGetMerchantItemInfo(lua_State* L);
int LuaGetMerchantItemLink(lua_State* L);
int LuaGetMerchantItemMaxStack(lua_State* L);
int LuaGetMerchantItemCostInfo(lua_State* L);
int LuaGetMerchantItemCostItem(lua_State* L);
int LuaSetMerchantItem(lua_State* L);
int LuaSetMerchantCostItem(lua_State* L);
int LuaBuyMerchantItem(lua_State* L);
int LuaGetBuybackItemInfo(lua_State* L);
int LuaGetBuybackItemLink(lua_State* L);
int LuaBuybackItem(lua_State* L);
int LuaRepairAllItems(lua_State* L);
int LuaCanMerchantRepair(lua_State* L);
int LuaCloseMerchant(lua_State* L);
int LuaGetNumBuybackItems(lua_State* L);

int LuaContainerItemPurchaseItem(lua_State* L);
int LuaContainerRefundItemPurchase(lua_State* L);
int LuaShowBuybackSellCursor(lua_State* L);
int LuaShowContainerSellCursor(lua_State* L);
int LuaShowInventorySellCursor(lua_State* L);
int LuaShowMerchantSellCursor(lua_State* L);

}

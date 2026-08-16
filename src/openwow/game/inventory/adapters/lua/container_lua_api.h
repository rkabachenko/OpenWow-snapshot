
#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaGetContainerNumSlots(lua_State* L);
int LuaGetContainerItemDurability(lua_State* L);
int LuaGetContainerItemInfo(lua_State* L);
int LuaGetContainerItemLink(lua_State* L);
int LuaGetContainerNumFreeSlots(lua_State* L);
int LuaGetContainerItemID(lua_State* L);
int LuaGetContainerItemCooldown(lua_State* L);
int LuaPickupContainerItem(lua_State* L);
int LuaUseContainerItem(lua_State* L);
int LuaSplitContainerItem(lua_State* L);

int LuaGetBagName(lua_State* L);

int LuaGetItemInfo(lua_State* L);
int LuaGetItemCount(lua_State* L);
int LuaGetItemQualityColor(lua_State* L);
int LuaGetItemIcon(lua_State* L);

int LuaGetMoney(lua_State* L);

int LuaContainerIDToInventoryID(lua_State* L);
int LuaGetContainerItemQuestInfo(lua_State* L);
int LuaGetInventoryAlertStatus(lua_State* L);
int LuaUpdateInventoryAlertStatus(lua_State* L);
int LuaSetBagPortraitTexture(lua_State* L);
int LuaSetInventoryPortraitTexture(lua_State* L);
int LuaUseInventoryItem(lua_State* L);

}

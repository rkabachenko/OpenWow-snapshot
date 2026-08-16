#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaItemHasRange(lua_State* L);
int LuaIsItemInRange(lua_State* L);
int LuaUseItemByName(lua_State* L);

int LuaEquipItemByName(lua_State* L);
int LuaIsEquippedItem(lua_State* L);
int LuaIsEquippedItemType(lua_State* L);
int LuaIsCurrentItem(lua_State* L);
int LuaGetItemUniqueness(lua_State* L);
int LuaSocketContainerItem(lua_State* L);

}

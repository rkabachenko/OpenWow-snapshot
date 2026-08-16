
#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaGetItemSpell(lua_State* L);
int LuaIsUsableItem(lua_State* L);
int LuaIsEquippableItem(lua_State* L);
int LuaIsConsumableItem(lua_State* L);
int LuaIsDressableItem(lua_State* L);
int LuaGetItemFamily(lua_State* L);

int LuaGetItemStats(lua_State* L);
int LuaGetItemStatDelta(lua_State* L);
int LuaIsHarmfulItem(lua_State* L);
int LuaIsHelpfulItem(lua_State* L);
int LuaItemTextGetCreator(lua_State* L);
int LuaItemTextGetItem(lua_State* L);
int LuaItemTextGetMaterial(lua_State* L);
int LuaItemTextGetPage(lua_State* L);
int LuaItemTextGetText(lua_State* L);
int LuaItemTextHasNextPage(lua_State* L);
int LuaItemTextNextPage(lua_State* L);
int LuaItemTextPrevPage(lua_State* L);
int LuaCloseItemText(lua_State* L);

}

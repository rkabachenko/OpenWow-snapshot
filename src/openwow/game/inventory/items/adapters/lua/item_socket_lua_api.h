#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaGetSocketItemBoundTradeable(lua_State*);
int LuaGetSocketItemRefundable(lua_State*);
int LuaGetSocketType(lua_State*);
int LuaApi_EndRefund(lua_State*);
int LuaApi_OffhandHasWeapon(lua_State*);

}

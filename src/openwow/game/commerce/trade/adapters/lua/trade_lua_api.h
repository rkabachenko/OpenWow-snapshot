#pragma once

struct lua_State;

namespace openwow::game {
class WorldSession;
}

namespace openwow::ui::game::detail {

int LuaGetTradePlayerItemInfo(lua_State*);
int LuaGetTradePlayerItemLink(lua_State*);
int LuaGetTradeTargetItemInfo(lua_State*);
int LuaGetTradeTargetItemLink(lua_State*);
int LuaGetPlayerTradeMoney(lua_State*);
int LuaSetTradeMoney(lua_State*);
int LuaGetTargetTradeMoney(lua_State*);
int LuaAddTradeMoney(lua_State*);
int LuaClickTradeButton(lua_State*);
int LuaAcceptTrade(lua_State*);
int LuaCancelTradeAccept(lua_State*);
int LuaCancelTrade(lua_State*);
int LuaCloseTrade(lua_State*);
int LuaInitiateTrade(lua_State*);
int LuaBeginTrade(lua_State*);
bool TrySendSocketingGems(openwow::game::WorldSession&);
int LuaBindEnchant(lua_State*);
int LuaPickupTradeMoney(lua_State*);
int LuaReplaceEnchant(lua_State*);
int LuaReplaceTradeEnchant(lua_State*);
int LuaClickTargetTradeButton(lua_State*);
int LuaEndBoundTradeable(lua_State*);

}

#pragma once

#include <cstdint>

#include "openwow/game/inventory/player_inventory_replica.h"

struct lua_State;

namespace openwow::ui::game::detail {

int LuaGetOwnerAuctionItems(lua_State* L);
int LuaGetBidderAuctionItems(lua_State* L);
int LuaGetAuctionItemInfo(lua_State* L);
int LuaGetAuctionItemLink(lua_State* L);
int LuaGetAuctionItemSubClasses(lua_State* L);
int LuaGetAuctionInvTypes(lua_State* L);
int LuaGetAuctionItemClasses(lua_State* L);
int LuaGetNumAuctionItems(lua_State* L);
int LuaSortAuctionItems(lua_State* L);
int LuaSortAuctionClearSort(lua_State* L);
int LuaQueryAuctionItems(lua_State* L);
int LuaCanSendAuctionQuery(lua_State* L);
int LuaPlaceAuctionBid(lua_State* L);
int LuaGetAuctionSellItemInfo(lua_State* L);
int LuaCancelSell(lua_State* L);
int LuaClickAuctionSellItemButton(lua_State* L);
int LuaStartAuction(lua_State* L);
int LuaCalculateAuctionDeposit(lua_State* L);
int LuaCancelAuction(lua_State* L);
int LuaSetSelectedAuctionItem(lua_State* L);
int LuaGetSelectedAuctionItem(lua_State* L);
int LuaCloseAuctionHouse(lua_State* L);

int LuaGetAuctionHouseDepositRate(lua_State* L);
int LuaGetAuctionItemTimeLeft(lua_State* L);
int LuaGetAuctionSort(lua_State* L);
int LuaIsAuctionSortReversed(lua_State* L);
int LuaSortAuctionApplySort(lua_State* L);
int LuaSortAuctionAddSort(lua_State* L);

int LuaCanCancelAuction(lua_State* L);
int LuaSetAuctionsTabShowing(lua_State* L);

bool AuctionTrySelectSellItem(lua_State* L,
                              const ::openwow::game::ItemInstance& item,
                              std::uint8_t source_bag,
                              std::uint8_t source_slot,
                              bool display_error);

}

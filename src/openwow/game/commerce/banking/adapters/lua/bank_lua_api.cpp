#include "openwow/ui/lua_c_api_convenience.h"

#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/game/commerce/banking/adapters/lua/bank_lua_api.h"
#include "openwow/game/inventory/items/adapters/lua/item_lua_adapter.h"
#include "openwow/ui/game/api/game_lua_api_container_common.h"
#include "openwow/game/interaction_sender.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/surfaces/game/runtime/npc_interaction_controller.h"
#include "openwow/ui/game/script_event_dispatch.h"

namespace openwow::ui::game::detail {

namespace {

constexpr int kMaxBankBagSlots =
    ::openwow::game::InventorySlots::kBankBagEnd -
    ::openwow::game::InventorySlots::kBankBagStart;
constexpr int kBankSlotInsufficientFundsMessage = 271;
constexpr std::uint32_t kBankItemScriptSlotBase = 39;
constexpr std::uint32_t kBankBagScriptSlotBase = 63;

int EnsureContainerFreeSlotsTable(lua_State* L) {
  if (lua_type(L, 2) == LUA_TTABLE) {
    return lua_absindex(L, 2);
  }

  lua_newtable(L);
  return lua_gettop(L);
}

int ReturnContainerFreeSlotsTable(lua_State* L, const int table_index) {
  if (table_index != lua_gettop(L)) {
    lua_pushvalue(L, table_index);
  }
  return 1;
}

template <typename SlotIsFree>
void PopulateContainerFreeSlots(lua_State* L, const int table_index,
                                const int slot_count,
                                SlotIsFree&& slot_is_free) {
  int result_index = 1;
  for (int slot = 0; slot < slot_count; ++slot) {
    if (!slot_is_free(slot)) {
      continue;
    }

    lua_pushinteger(L, static_cast<lua_Integer>(slot + 1));
    lua_rawseti(L, table_index, result_index);
    ++result_index;
  }
}

const ::openwow::game::CGContainer_C* ResolveLuaContainer(
    const ItemLuaAdapter& adapter, const int bag_id,
    const bool bank_frame_open) {
  auto& inventory = adapter.inventory();
  const auto bag_guid =
      GetContainerGuidForLuaBagSlot(inventory, bag_id, bank_frame_open);
  if (bag_guid == 0) {
    return nullptr;
  }

  return adapter.objects().GetContainer(::openwow::game::ObjectGuid(bag_guid));
}

std::uint32_t ResolveNextBankBagSlotCost(const ItemLuaAdapter& adapter,
                                         const ::openwow::game::CGPlayer_C& player) {
  const auto* dbc = adapter.dbc();
  if (dbc == nullptr) {
    return 0;
  }

  const auto purchased_slot_count =
      static_cast<std::uint32_t>(player.GetBankBagSlotCount());
  const auto next_slot_id = purchased_slot_count + 1u;
  const auto* entry = dbc->bank_bag_slot_prices().LookupEntry(next_slot_id);
  if (entry == nullptr) {
    return 0;
  }

  return entry->cost;
}

const ::openwow::game::CGPlayer_C* ResolveBankSlotPlayer(
    const ItemLuaAdapter& adapter) {
  return adapter.objects().GetActivePlayer();
}

int LuaPurchaseSlotImpl(lua_State* L) {
  auto& adapter = RequireItemLuaAdapter(L);

  const auto* player = adapter.objects().GetActivePlayer();
  if (player == nullptr) {
    return 0;
  }

  const auto banker_guid = adapter.world_session().bank_npc_guid();
  if (banker_guid == 0) {
    return 0;
  }

  if (player->GetBankBagSlotCount() >= kMaxBankBagSlots) {
    return 0;
  }

  const auto next_slot_cost = ResolveNextBankBagSlotCost(adapter, *player);
  if (player->GetMoney() < next_slot_cost) {
    DisplaySystemMessage(kBankSlotInsufficientFundsMessage);
    return 0;
  }

  adapter.interaction().SendBuyBankSlot(banker_guid);
  return 0;
}

}

int LuaGetNumBankSlots(lua_State* L) {
  auto& adapter = RequireItemLuaAdapter(L);
  int numSlots = 0;
  if (const auto* player = adapter.objects().GetActivePlayer()) {

      std::uint32_t bytes2 = player->GetUInt32(PLAYER_BYTES_2);
      numSlots = static_cast<int>((bytes2 >> 16) & 0xFF);
  }
  lua_pushnumber(L, numSlots);
  if (numSlots > 6) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 2;
}

int LuaPurchaseSlot(lua_State* L) {
  return LuaPurchaseSlotImpl(L);
}

int LuaCloseBankFrame(lua_State* L) {
  RequireItemLuaAdapter(L).CloseBank();
  return 0;
}

int LuaBankButtonIDToInvSlotID(lua_State* L) {
  if (lua_isnumber(L, 1) == 0) {
    return luaL_error(L, "Usage: BankButtonIDToInvSlotID(buttonID)");
  }

  const auto button_id = openwow::ui::TruncateLuaNumberToI32(lua_tonumber(L, 1));
  const auto script_slot_base = lua_isnumber(L, 2) != 0
                                    ? kBankBagScriptSlotBase
                                    : kBankItemScriptSlotBase;

  const auto inventory_slot_id = openwow::ui::SignedI32FromU32Bits(
      static_cast<std::uint32_t>(button_id) + script_slot_base);
  lua_pushnumber(L, static_cast<lua_Number>(inventory_slot_id));
  return 1;
}

int LuaGetBankSlotCost(lua_State* L) {
  std::uint32_t cost = 0;
  auto& adapter = RequireItemLuaAdapter(L);
  if (const auto* player = ResolveBankSlotPlayer(adapter)) {
    cost = ResolveNextBankBagSlotCost(adapter, *player);
  }
  lua_pushnumber(L, cost);
  return 1;
}

int LuaGetContainerFreeSlots(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L,
                      "Usage: GetContainerFreeSlots(index [, returnTable])");
  }

  auto& adapter = RequireItemLuaAdapter(L);
  if (adapter.objects().GetActivePlayer() == nullptr) {
    return 0;
  }

  const int bag_id = static_cast<int>(lua_tonumber(L, 1));
  const bool bank_frame_open = adapter.bank_frame_open();
  auto& inventory = adapter.inventory();
  const int table_index = EnsureContainerFreeSlotsTable(L);

  if (bag_id == 0) {
    PopulateContainerFreeSlots(
        L, table_index,
        static_cast<int>(::openwow::game::PlayerInventoryReplica::kBackpackSize),
        [&inventory](const int slot) {
          return inventory.GetBackpackSlot(static_cast<std::uint8_t>(slot)) ==
                 nullptr;
        });
    return ReturnContainerFreeSlotsTable(L, table_index);
  }

  if (bag_id == -1) {
    if (!bank_frame_open) {
      return 0;
    }

    PopulateContainerFreeSlots(
        L, table_index,
        static_cast<int>(::openwow::game::PlayerInventoryReplica::kBankSlots),
        [&inventory](const int slot) {
          return inventory.GetBankSlot(static_cast<std::uint8_t>(slot)) ==
                 nullptr;
        });
    return ReturnContainerFreeSlotsTable(L, table_index);
  }

  const auto* container =
      ResolveLuaContainer(adapter, bag_id, bank_frame_open);
  if (container == nullptr) {
    return 0;
  }

  PopulateContainerFreeSlots(
      L, table_index, static_cast<int>(container->GetNumSlots()),
      [container](const int slot) {
        return container->GetSlot(static_cast<std::uint8_t>(slot)).IsEmpty();
      });
  return ReturnContainerFreeSlotsTable(L, table_index);
}

}

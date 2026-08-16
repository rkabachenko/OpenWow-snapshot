#include "openwow/game/inventory/items/adapters/lua/item_socket_lua_api.h"
#include "openwow/game/inventory/items/adapters/lua/item_lua_adapter.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/item_interactions.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/items/item_trade_eligibility.h"
#include "openwow/game/interaction_sender.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgitem.h"
#include "openwow/game/objects/cgplayer.h"

#include <lua.hpp>

#include <array>
#include <optional>
#include <utility>

namespace openwow::ui::game::detail {
namespace {
enum class PrimarySocketColor : std::uint8_t {
  kMeta = 1,
  kRed = 2,
  kYellow = 4,
  kBlue = 8,
};

constexpr std::array<std::pair<std::uint8_t, const char *>, 4> kSocketTypeNames{{
    {static_cast<std::uint8_t>(PrimarySocketColor::kMeta), "Meta"},
    {static_cast<std::uint8_t>(PrimarySocketColor::kRed), "Red"},
    {static_cast<std::uint8_t>(PrimarySocketColor::kYellow), "Yellow"},
    {static_cast<std::uint8_t>(PrimarySocketColor::kBlue), "Blue"},
}};
const openwow::game::ItemInstance *FindInventoryItemByGuid(
    const openwow::game::PlayerInventoryReplica& inventory,
    const openwow::game::actions::held_cursor::HeldCursor* held_cursor,
    const std::uint64_t item_guid) {
  if (item_guid == 0) {
    return nullptr;
  }

  for (std::uint8_t slot = 0; slot < openwow::game::PlayerInventoryReplica::kMaxEquipSlots; ++slot) {
    if (const auto *item = inventory.GetEquipSlot(slot);
        item != nullptr && item->guid == item_guid) {
      return item;
    }
  }

  for (std::uint8_t slot = 0; slot < openwow::game::PlayerInventoryReplica::kBackpackSize; ++slot) {
    if (const auto *item = inventory.GetBackpackSlot(slot);
        item != nullptr && item->guid == item_guid) {
      return item;
    }
  }

  for (std::uint8_t bag = 1; bag <= openwow::game::PlayerInventoryReplica::kMaxBags; ++bag) {
    const auto slot_count = inventory.GetContainerNumSlots(bag);
    for (std::uint8_t slot = 0; slot < static_cast<std::uint8_t>(slot_count); ++slot) {
      if (const auto *item = inventory.GetBagSlot(bag, slot);
          item != nullptr && item->guid == item_guid) {
        return item;
      }
    }
  }

  if (held_cursor != nullptr) {
    const auto* item = held_cursor->live_item();
    if (item != nullptr && item->item.guid == item_guid) {
      return &item->item;
    }
  }

  return nullptr;
}

int PushActivePlayerEquippedItemClassOrNil(lua_State *L, const std::uint8_t slot,
                                                         const std::uint32_t item_class) {
  auto& objects = RequireItemLuaAdapter(L).objects();
  if (const auto* player = objects.GetActivePlayer(); player != nullptr) {
    const auto item_guid = player->GetEquippedItem(slot);
    if (!item_guid.IsEmpty()) {
      if (const auto* item = objects.GetItem(item_guid);
          item != nullptr && item->GetItemClassFromClientDbc() == item_class) {
        lua_pushnumber(L, 1.0);
        return 1;
      }
    }
  }

  lua_pushnil(L);
  return 1;
}

std::optional<std::uint32_t> GetSocketUiCurrentPlayedTime(lua_State *L) {
  return RequireItemLuaAdapter(L).CurrentPlayedTime();
}

bool HasSocketTradeTimeRemaining(const openwow::game::ItemInstance &item,
                                 const std::uint32_t current_played_time) {
  return openwow::game::HasBoundTradeWindowRemaining(item.create_played_time,
                                                     current_played_time);
}
}

int LuaGetSocketItemBoundTradeable(lua_State *L) {
  const auto current_played_time = GetSocketUiCurrentPlayedTime(L);
  auto& adapter = RequireItemLuaAdapter(L);
  const auto* dbc = adapter.dbc();
  const auto *item = adapter.item_interactions().socket().has_value()
                         ? FindInventoryItemByGuid(
                               adapter.inventory(), adapter.held_cursor(),
                               adapter.item_interactions()
                                   .socket()->item.GetRawValue())
                         : nullptr;
  const bool is_bound_tradeable =
      item != nullptr && current_played_time.has_value() &&
      (item->flags & openwow::game::ItemFlags::kSoulbound) != 0u &&
      (item->flags & openwow::game::ItemFlags::kTradeWindow) != 0u &&
      HasSocketTradeTimeRemaining(*item, *current_played_time) &&
      !openwow::game::ItemHasBindingEnchantSlot(*item, [dbc](const std::uint32_t enchantment_id) {
        return dbc != nullptr ? dbc->spell_item_enchantment().LookupEntry(enchantment_id) : nullptr;
      });

  if (is_bound_tradeable) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaGetSocketItemRefundable(lua_State *L) {
  const auto current_played_time = GetSocketUiCurrentPlayedTime(L);
  auto& adapter = RequireItemLuaAdapter(L);
  const auto& interactions = adapter.item_interactions();
  const auto item_guid = interactions.socket().has_value()
                             ? interactions.socket()->item.GetRawValue()
                             : 0u;
  const auto *item = FindInventoryItemByGuid(
      adapter.inventory(), adapter.held_cursor(), item_guid);

  const auto* refund_info =
      item_guid != 0 ? interactions.refund_quote(::openwow::game::ObjectGuid(item_guid)) : nullptr;
  const bool is_refundable = item != nullptr && refund_info != nullptr &&
                             current_played_time.has_value() &&
                             HasSocketTradeTimeRemaining(*item, *current_played_time) &&
                             !openwow::game::ItemHasRefundBlockingEnchantmentState(*item);

  if (is_refundable) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaGetSocketType(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    luaL_error(L, "Usage: GetSocketType(index)");
  }

  auto& interactions = RequireItemLuaAdapter(L).item_interactions();
  if (!interactions.socket().has_value()) {
    return 0;
  }

  const auto item_guid = interactions.socket()->item.GetRawValue();
  if (item_guid == 0) {
    return 0;
  }

  const auto socket_index = static_cast<int>(lua_tonumber(L, 1));
  std::uint8_t socket_mask = 0;
  if (socket_index > 0) {
    const auto index = static_cast<std::size_t>(socket_index - 1);
    if (index < interactions.socket()->socket_count) {
      socket_mask = interactions.socket()->socket_masks[index];
    }
  }

  int pushed_results = 0;
  for (const auto &[bit, name] : kSocketTypeNames) {
    if ((socket_mask & bit) == 0) {
      continue;
    }

    lua_pushstring(L, name);
    ++pushed_results;
  }

  if (pushed_results == 0) {
    lua_pushstring(L, "Socket");
    return 1;
  }

  return pushed_results;
}

int LuaApi_EndRefund(lua_State *L) {
  auto& adapter = RequireItemLuaAdapter(L);
  const auto action = static_cast<int>(luaL_checknumber(L, 1));
  if (action == 1) {
    const auto item_guid =
        adapter.item_interactions().pending_modification().GetRawValue();
    if (item_guid != 0) {
      adapter.ConfirmItemTarget(
          ::openwow::game::ObjectGuid(item_guid));
    }
    return 0;
  }

  if (action == 2) {
    const auto& socket = adapter.item_interactions().socket();
    if (socket.has_value()) {
      adapter.interaction().SendSocketGems(
          socket->item.GetRawValue(), socket->gems[0].GetRawValue(),
          socket->gems[1].GetRawValue(), socket->gems[2].GetRawValue());
    }
  }

  return 0;
}

int LuaApi_OffhandHasWeapon(lua_State *L) {
  return PushActivePlayerEquippedItemClassOrNil(
      L, openwow::game::InventorySlots::kOffHand,
      static_cast<std::uint32_t>(openwow::game::ItemClass::Weapon));
}

}

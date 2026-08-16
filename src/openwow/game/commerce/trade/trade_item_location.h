#pragma once

#include "openwow/game/actions/held_cursor/held_cursor.h"
#include "openwow/game/commerce/trade/trade_interaction.h"
#include "openwow/game/inventory/operations/player_item_packet_location.h"
#include "openwow/game/inventory/player_inventory_replica.h"

#include <cstdint>
#include <optional>

namespace openwow::game {

struct TradeContainerSlot {
  std::uint8_t server_bag = InventorySlots::kMainBag;
  std::uint8_t slot = 0;
};

inline const ItemInstance* GetTradeContainerItem(
    const PlayerInventoryReplica& inventory, const std::uint8_t server_bag,
    const std::uint8_t slot) {
  return GetPlayerItemAtPacketLocation(inventory, server_bag, slot);
}

inline std::optional<TradeContainerSlot> ResolveCursorTradeSource(
    const PlayerInventoryReplica& inventory,
    const actions::held_cursor::LiveItem& held_item) {
  const auto& cursor_item = held_item.item;
  if (cursor_item.guid != 0) {
    if (const auto location =
            ResolvePlayerItemPacketLocationByGuid(inventory, cursor_item.guid);
        location.has_value()) {
      return TradeContainerSlot{
          location->packet_bag, location->packet_slot};
    }
  }

  const auto matches_slot =
      [&](const std::uint8_t server_bag,
          const std::uint8_t slot) -> std::optional<TradeContainerSlot> {
    const auto* item =
        GetTradeContainerItem(inventory, server_bag, slot);
    if (!item || item->guid != cursor_item.guid) {
      return std::nullopt;
    }
    return TradeContainerSlot{server_bag, slot};
  };

  const auto recorded_bag = held_item.source_bag;
  const auto recorded_slot = held_item.source_slot;
  if (recorded_bag == InventorySlots::kMainBag) {
    if (auto match = matches_slot(recorded_bag, recorded_slot)) {
      return match;
    }
  } else if (recorded_bag <= PlayerInventoryReplica::kMaxBags) {
    const auto server_bag =
        recorded_bag == 0 ? InventorySlots::kMainBag : recorded_bag;
    if (auto match = matches_slot(server_bag, recorded_slot)) {
      return match;
    }
  }

  for (std::uint8_t slot = 0;
       slot < PlayerInventoryReplica::kBackpackSize; ++slot) {
    if (auto match = matches_slot(
            InventorySlots::kMainBag,
            static_cast<std::uint8_t>(
                InventorySlots::kBackpackStart + slot))) {
      return match;
    }
  }

  for (std::uint8_t bag = 1; bag <= PlayerInventoryReplica::kMaxBags; ++bag) {
    const auto* bag_info = inventory.GetBag(bag);
    if (!bag_info) {
      continue;
    }
    for (std::uint8_t slot = 0; slot < bag_info->num_slots; ++slot) {
      if (auto match = matches_slot(
              static_cast<std::uint8_t>(
                  InventorySlots::kBagSlotsStart + bag - 1),
              slot)) {
        return match;
      }
    }
  }

  return std::nullopt;
}

inline std::optional<TradeAutoPlacement> ResolveAutoTradePlacement(
    const TradeInteraction& trade,
    const PlayerInventoryReplica& inventory,
    const actions::held_cursor::HeldCursor* cursor,
    const std::uint64_t target_guid) {
  const auto* held_item =
      cursor != nullptr ? cursor->live_item() : nullptr;
  if (held_item == nullptr || held_item->item.IsEmpty() ||
      target_guid == 0 || trade.begin_trade_guid() != target_guid) {
    return std::nullopt;
  }

  const auto source = ResolveCursorTradeSource(inventory, *held_item);
  if (!source) {
    return std::nullopt;
  }

  const auto trade_slot = trade.SelectCursorDropTradeSlot(
      held_item->item.guid, held_item->item.IsSoulbound());
  if (!trade_slot) {
    return std::nullopt;
  }

  return TradeAutoPlacement{
      .trade_slot = *trade_slot,
      .source_bag = source->server_bag,
      .source_slot = source->slot,
      .item_guid = held_item->item.guid,
  };
}

}

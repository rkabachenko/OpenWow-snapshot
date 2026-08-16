#pragma once

#include "openwow/game/inventory/player_inventory_replica.h"

#include <cstdint>
#include <optional>

namespace openwow::game {

struct PlayerItemPacketLocation {
  ItemInstance item;
  std::uint8_t packet_bag = InventorySlots::kMainBag;
  std::uint8_t packet_slot = 0;
};

inline const ItemInstance* GetPlayerItemAtPacketLocation(
    const PlayerInventoryReplica& inventory,
    const std::uint8_t packet_bag,
    const std::uint8_t packet_slot) {
  if (packet_bag == InventorySlots::kMainBag) {
    return inventory.GetItemInSlot(packet_slot);
  }

  if (packet_bag >= InventorySlots::kBagSlotsStart &&
      packet_bag < InventorySlots::kBagSlotsEnd) {
    const auto bag_index = static_cast<std::uint8_t>(
        packet_bag - InventorySlots::kBagSlotsStart + 1);
    return inventory.GetBagSlot(bag_index, packet_slot);
  }

  if (packet_bag >= InventorySlots::kBankBagStart &&
      packet_bag < InventorySlots::kBankBagEnd) {
    const auto bag_index = static_cast<std::uint8_t>(
        packet_bag - InventorySlots::kBankBagStart);
    return inventory.GetBankBagSlot(bag_index, packet_slot);
  }

  return nullptr;
}

inline std::optional<PlayerItemPacketLocation> ResolvePlayerItemPacketLocation(
    const PlayerInventoryReplica& inventory,
    const std::uint8_t packet_bag,
    const std::uint8_t packet_slot) {
  const auto* item =
      GetPlayerItemAtPacketLocation(inventory, packet_bag, packet_slot);
  if (item == nullptr || item->IsEmpty()) {
    return std::nullopt;
  }

  return PlayerItemPacketLocation{
      .item = *item,
      .packet_bag = packet_bag,
      .packet_slot = packet_slot,
  };
}

inline std::optional<PlayerItemPacketLocation> ResolveDirectPlayerItemPacketLocation(
    const PlayerInventoryReplica& inventory,
    const std::uint8_t absolute_slot) {
  if (absolute_slot >= InventorySlots::kBagSlotsStart &&
      absolute_slot < InventorySlots::kBagSlotsEnd) {
    const auto bag_index = static_cast<std::uint8_t>(
        absolute_slot - InventorySlots::kBagSlotsStart + 1);
    const auto* bag = inventory.GetBag(bag_index);
    if (bag == nullptr || bag->guid == 0 ||
        inventory.GetSlotGuid(absolute_slot) != bag->guid) {
      return std::nullopt;
    }

    ItemInstance bag_item;
    bag_item.guid = bag->guid;
    bag_item.entry = bag->entry;
    bag_item.count = 1;
    return PlayerItemPacketLocation{
        .item = bag_item,
        .packet_bag = InventorySlots::kMainBag,
        .packet_slot = absolute_slot,
    };
  }

  const auto* item = inventory.GetItemInSlot(absolute_slot);
  if (item == nullptr || item->IsEmpty()) {
    return std::nullopt;
  }

  return PlayerItemPacketLocation{
      .item = *item,
      .packet_bag = InventorySlots::kMainBag,
      .packet_slot = absolute_slot,
  };
}

inline std::optional<PlayerItemPacketLocation> ResolvePlayerItemPacketLocation(
    const PlayerInventoryReplica& inventory,
    const std::uint64_t player_guid,
    const std::uint64_t container_guid,
    const std::uint32_t container_slot) {
  if (container_guid == 0) {
    return std::nullopt;
  }

  if (container_guid == player_guid) {
    if (container_slot > 0xFFu) {
      return std::nullopt;
    }
    return ResolveDirectPlayerItemPacketLocation(
        inventory, static_cast<std::uint8_t>(container_slot));
  }

  const auto bag_slot = inventory.FindSlotByGuid(container_guid);
  if (bag_slot < InventorySlots::kBagSlotsStart ||
      bag_slot >= InventorySlots::kBagSlotsEnd || container_slot > 0xFFu) {
    return std::nullopt;
  }

  const auto bag_index = static_cast<std::uint8_t>(
      bag_slot - InventorySlots::kBagSlotsStart + 1);
  const auto* bag = inventory.GetBag(bag_index);
  if (bag == nullptr || bag->guid != container_guid ||
      container_slot >= bag->num_slots) {
    return std::nullopt;
  }

  const auto* item =
      inventory.GetBagSlot(bag_index, static_cast<std::uint8_t>(container_slot));
  if (item == nullptr || item->IsEmpty()) {
    return std::nullopt;
  }

  return PlayerItemPacketLocation{
      .item = *item,
      .packet_bag = static_cast<std::uint8_t>(bag_slot),
      .packet_slot = static_cast<std::uint8_t>(container_slot),
  };
}

inline std::optional<PlayerItemPacketLocation> ResolvePlayerItemPacketLocationByGuid(
    const PlayerInventoryReplica& inventory,
    const std::uint64_t item_guid) {
  if (item_guid == 0) {
    return std::nullopt;
  }

  const auto direct_guids = inventory.CaptureSlotGuids();
  const auto resolve_direct_range = [&](const std::uint8_t first,
                                        const std::uint8_t last) {
    for (std::uint16_t slot = first; slot < last; ++slot) {
      if (direct_guids[slot] != item_guid) {
        continue;
      }

      return ResolveDirectPlayerItemPacketLocation(
          inventory, static_cast<std::uint8_t>(slot));
    }

    return std::optional<PlayerItemPacketLocation>{};
  };

  if (auto location = resolve_direct_range(InventorySlots::kEquipStart,
                                           InventorySlots::kBankEnd)) {
    return location;
  }
  if (auto location = resolve_direct_range(InventorySlots::kKeyringStart,
                                           InventorySlots::kKeyringEnd)) {
    return location;
  }

  for (std::uint8_t bag_index = 1; bag_index <= PlayerInventoryReplica::kMaxBags;
       ++bag_index) {
    const auto* bag = inventory.GetBag(bag_index);
    if (bag == nullptr || bag->guid == 0) {
      continue;
    }

    for (std::uint8_t slot = 0; slot < bag->num_slots; ++slot) {
      const auto* item = inventory.GetBagSlot(bag_index, slot);
      if (item == nullptr || item->guid != item_guid) {
        continue;
      }

      return PlayerItemPacketLocation{
          .item = *item,
          .packet_bag = static_cast<std::uint8_t>(
              InventorySlots::kBagSlotsStart + bag_index - 1),
          .packet_slot = slot,
      };
    }
  }

  for (std::uint8_t bag_index = 0;
       bag_index < PlayerInventoryReplica::kMaxBankBags; ++bag_index) {
    const auto* bag = inventory.GetBankBag(bag_index);
    if (bag == nullptr || bag->guid == 0) {
      continue;
    }

    for (std::uint8_t slot = 0; slot < bag->num_slots; ++slot) {
      const auto* item = inventory.GetBankBagSlot(bag_index, slot);
      if (item == nullptr || item->guid != item_guid) {
        continue;
      }

      return PlayerItemPacketLocation{
          .item = *item,
          .packet_bag = static_cast<std::uint8_t>(
              InventorySlots::kBankBagStart + bag_index),
          .packet_slot = slot,
      };
    }
  }

  return std::nullopt;
}

}

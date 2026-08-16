#pragma once

#include "openwow/game/inventory/player_inventory_replica.h"

#include <cstdint>

namespace openwow::ui::game::detail {

inline constexpr int kContainerFrameLuaBagSlotCount = 11;

inline const ::openwow::game::ItemInstance* ResolveLuaContainerItem(
    const ::openwow::game::PlayerInventoryReplica& inventory,
    const bool has_active_player, const bool bank_open, const int bag_id,
    const int zero_based_slot) {
  if (!has_active_player || zero_based_slot < 0) {
    return nullptr;
  }

  const auto slot = static_cast<std::uint8_t>(zero_based_slot);
  if (bag_id == -2) {
    return inventory.GetKeyringSlot(slot);
  }
  if (bag_id == -4) {
    return inventory.GetItemInSlot(static_cast<std::uint8_t>(
        ::openwow::game::InventorySlots::kCurrencyStart + slot));
  }
  if (bag_id == -1) {
    return bank_open ? inventory.GetBankSlot(slot) : nullptr;
  }
  if (bag_id == 0) {
    return inventory.GetBackpackSlot(slot);
  }
  if (bag_id >= 1 && bag_id <= ::openwow::game::PlayerInventoryReplica::kMaxBags) {
    return inventory.GetBagSlot(static_cast<std::uint8_t>(bag_id), slot);
  }
  if (bag_id >= 5 && bag_id <= kContainerFrameLuaBagSlotCount) {
    if (!bank_open) {
      return nullptr;
    }

    return inventory.GetBankBagSlot(static_cast<std::uint8_t>(bag_id - 5), slot);
  }

  return nullptr;
}

inline std::uint32_t CountFreeBackpackSlots(
    const ::openwow::game::PlayerInventoryReplica& inventory) {
  std::uint32_t free_slots = 0;
  for (std::uint8_t slot = 0;
       slot < ::openwow::game::PlayerInventoryReplica::kBackpackSize; ++slot) {
    if (inventory.GetBackpackSlot(slot) == nullptr) {
      ++free_slots;
    }
  }
  return free_slots;
}

inline std::uint32_t CountFreeBankSlots(
    const ::openwow::game::PlayerInventoryReplica& inventory) {
  std::uint32_t free_slots = 0;
  for (std::uint8_t slot = 0;
       slot < ::openwow::game::PlayerInventoryReplica::kBankSlots; ++slot) {
    if (inventory.GetBankSlot(slot) == nullptr) {
      ++free_slots;
    }
  }
  return free_slots;
}

inline std::uint64_t GetContainerGuidForLuaBagSlot(
    const ::openwow::game::PlayerInventoryReplica& inventory, const int bag_id,
    const bool bank_frame_open) {
  if (bag_id < 1 || bag_id > kContainerFrameLuaBagSlotCount) {
    return 0;
  }

  if (bag_id <= 4) {
    return inventory.GetSlotGuid(
        ::openwow::game::InventorySlots::kBagSlotsStart + (bag_id - 1));
  }

  if (!bank_frame_open) {
    return 0;
  }

  return inventory.GetSlotGuid(::openwow::game::InventorySlots::kBankBagStart +
                               (bag_id - 5));
}

}

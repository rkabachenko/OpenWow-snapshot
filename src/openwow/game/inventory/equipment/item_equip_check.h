#pragma once

#include "openwow/game/inventory/items/item_definitions.h"

#include <array>
#include <cstdint>

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::game {

inline constexpr std::array<std::uint32_t, 29> kInventoryTypeToSlotMask = {{
    0x00000000,
    0x00000001,
    0x00000002,
    0x00000004,
    0x00000008,
    0x00000010,
    0x00000020,
    0x00000040,
    0x00000080,
    0x00000100,
    0x00000200,
    0x00000C00,
    0x00003000,
    0x00018000,
    0x00010000,
    0x00020000,
    0x00004000,
    0x00018000,
    0x00780000,
    0x00040000,
    0x00000010,
    0x00018000,
    0x00018000,
    0x00010000,
    0x00000000,
    0x00020000,
    0x00020000,
    0x00780000,
    0x00020000,
}};

[[nodiscard]] inline bool CanInventoryTypeGoInSlot(std::uint32_t inv_type,
                                                    std::uint32_t target_slot,
                                                    bool has_relic_slot = false) {
  if (inv_type >= kInventoryTypeToSlotMask.size()) {
    return false;
  }
  if (target_slot >= 23) {
    return true;
  }
  if (target_slot == 17) {
    if (inv_type == 28) {
      return has_relic_slot;
    }
    if (inv_type == 15 || inv_type == 25 || inv_type == 26) {
      return !has_relic_slot;
    }
  }
  return (kInventoryTypeToSlotMask[inv_type] & (1u << target_slot)) != 0;
}

class CGPlayer_C;

[[nodiscard]] bool PlayerClassHasRelicSlot(std::uint8_t class_id);

[[nodiscard]] bool CanItemOccupyInventorySlot(
    const ItemTemplate& item,
    std::int32_t target_slot,
    const CGPlayer_C* player,
    const openwow::data::dbc::DbcLoader* dbc);

struct ItemEquipRequirementContext {
  std::uint32_t player_level = 0;
  std::uint8_t player_class = 0;
  std::uint8_t player_race = 0;
  std::uint32_t proficiency_mask = 0;
  std::int32_t faction_standing = 0;
  bool can_dual_wield = false;
  const openwow::data::dbc::DbcLoader* dbc = nullptr;
};

[[nodiscard]] bool PlayerMeetsItemEquipRequirements(
    const ItemTemplate& item,
    std::uint32_t target_slot,
    const ItemEquipRequirementContext& context);

}

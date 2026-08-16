
#pragma once

#include "openwow/game/object_guid.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace openwow::data::dbc {
struct AreaTriggerEntry;
}

namespace openwow::game {

struct ItemLootInteractionParams {
  std::uint64_t item_guid;
  bool          check_distance;
};

namespace ItemLootOpcode {

inline constexpr std::uint32_t kLoot = 349;
}

inline constexpr std::uint32_t kUnitFlagMaskBlocksLootInteraction = 0x00C010FF;

inline constexpr float kItemLootMaxDistanceSq = 25.0f;

inline constexpr std::uint32_t kItemLootEmoteId = 50;

inline constexpr std::uint32_t kActionTypeLoot = 6;

namespace GossipOpcode {
inline constexpr std::uint32_t kGossipHello = 540;
}

namespace BankerOpcode {
inline constexpr std::uint32_t kBankerActivate = 0x1B7;
}

struct TabardSaveParams {
  std::uint64_t vendor_guid;
  std::uint32_t emblem_style;
  std::uint32_t emblem_color;
  std::uint32_t border_style;
  std::uint32_t border_color;
  std::uint32_t background_color;
};

namespace ContainerOpcode {
inline constexpr std::uint32_t kSplitItem = 1001;

}

namespace EquipSwapOpcode {
inline constexpr std::uint32_t kAutoEquipItemDirect = 267;
}

struct ItemSkillCheck {
  std::uint16_t required_skill_id;
  std::uint16_t required_skill_rank;
  std::uint16_t player_skill_value;
};

[[nodiscard]] inline bool MeetsItemSkillRequirement(
    const ItemSkillCheck& check) {
  if (check.required_skill_id == 0) return true;
  return check.player_skill_value >= check.required_skill_rank;
}

namespace ItemOpenOpcode {
inline constexpr std::uint32_t kOpenItem = 172;
}

namespace AreaTriggerOpcode {
inline constexpr std::uint32_t kAreaTrigger = 180;
}

inline constexpr std::uint32_t kAreaTriggerCheckIntervalMs = 100;

struct AreaTriggerSystem {
  bool is_registered = false;
  std::uint32_t current_map_id = 0;
  std::size_t map_slice_begin = 0;
  std::size_t map_slice_end = 0;
  std::optional<std::size_t> active_trigger_index;

  std::uint32_t next_check_tick_ms = 0;

  [[nodiscard]] bool ConsumeCheckDue(std::uint32_t now_ms);

  void Cleanup();
  [[nodiscard]] std::optional<std::uint32_t> Update(
      std::span<const openwow::data::dbc::AreaTriggerEntry> entries,
      std::uint32_t map_id,
      const std::array<float, 3>& player_position);
  [[nodiscard]] static bool ContainsPoint(
      const openwow::data::dbc::AreaTriggerEntry& entry,
      const std::array<float, 3>& player_position);
};

}

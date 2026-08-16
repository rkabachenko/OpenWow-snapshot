
#pragma once

#include "openwow/game/object_guid.h"

#include <array>
#include <cstdint>

namespace openwow::game {

namespace UnitTooltipFlag {
constexpr std::uint16_t kAlive         = 0x0001;
constexpr std::uint16_t kPvP           = 0x0002;
constexpr std::uint16_t kDead          = 0x0004;
constexpr std::uint16_t kCorpse        = 0x0009;
constexpr std::uint16_t kTapped        = 0x0010;
constexpr std::uint16_t kItemType      = 0x0040;
constexpr std::uint16_t kContainerType = 0x0080;
constexpr std::uint16_t kAttackable    = 0x0100;
constexpr std::uint16_t kPlayerOrVehicleGuid = 0x0200;
}

struct UnitTooltipInfo {
  std::uint16_t flags = UnitTooltipFlag::kAlive;
  std::uint8_t power_type_display = 0;
  std::int32_t health = 0;
  std::int32_t max_health = 0;
  std::uint16_t power = 0;
  std::uint16_t max_power = 0;
  std::uint16_t level = 0;
  std::uint16_t map_id = 0;
  std::int16_t position_x = 0;
  std::int16_t position_y = 0;

  static constexpr std::size_t kMaxTooltipAuras = 64;
  std::array<std::uint32_t, kMaxTooltipAuras> aura_spell_ids{};
  std::array<std::uint8_t, kMaxTooltipAuras> aura_flags{};

  ObjectGuid target_guid;
  std::uint32_t vehicle_seat_spell_id = 0;

  [[nodiscard]] std::uint32_t GetAuraCount() const {
    std::uint32_t count = 0;
    for (auto id : aura_spell_ids) {
      if (id == 0) break;
      ++count;
    }
    return count;
  }
};

}

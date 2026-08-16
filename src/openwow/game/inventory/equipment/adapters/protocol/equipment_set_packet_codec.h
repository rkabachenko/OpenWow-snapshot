#pragma once

#include "openwow/game/inventory/equipment/equipment_sets.h"
#include "openwow/network/protocol/wotlk/world_packet.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace openwow::game::equipment_protocol {

[[nodiscard]] net::wotlk::WorldPacket encode_use(
    const EquipmentSetUse& request);
[[nodiscard]] net::wotlk::WorldPacket encode_save(
    const EquipmentSetSave& request);
[[nodiscard]] net::wotlk::WorldPacket encode_delete(ObjectGuid set);
[[nodiscard]] std::optional<std::vector<EquipmentSet>> decode_list(
    std::span<const std::uint8_t> payload);
[[nodiscard]] std::optional<std::pair<std::uint32_t, ObjectGuid>>
decode_saved(std::span<const std::uint8_t> payload);
[[nodiscard]] std::optional<std::uint8_t> decode_use_result(
    std::span<const std::uint8_t> payload);

}

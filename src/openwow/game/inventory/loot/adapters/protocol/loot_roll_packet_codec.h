#pragma once

#include "openwow/game/inventory/loot/loot_roll_messages.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace openwow::game::loot_protocol {

[[nodiscard]] std::optional<GroupLootStartRoll> DecodeStartRoll(
    const std::uint8_t* data, std::size_t size);
[[nodiscard]] std::optional<GroupLootRollResult> DecodeRoll(
    const std::uint8_t* data, std::size_t size);
[[nodiscard]] std::optional<GroupLootAllPassed> DecodeAllPassed(
    const std::uint8_t* data, std::size_t size);

}

#pragma once

#include "openwow/game/activities/dance/model/dance_studio_messages.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace openwow::game::dance::protocol {

[[nodiscard]] std::optional<StopDanceCommand>
DecodeStopDance(std::span<const std::uint8_t> payload);

[[nodiscard]] std::optional<PlayDanceCommand>
DecodePlayDance(std::span<const std::uint8_t> payload);

[[nodiscard]] std::optional<DanceQueryResult>
DecodeDanceQueryResult(std::span<const std::uint8_t> payload);

[[nodiscard]] std::optional<InvalidateDanceCommand>
DecodeInvalidateDance(std::span<const std::uint8_t> payload);

[[nodiscard]] std::optional<LearnedDanceMovesUpdate>
DecodeLearnedDanceMoves(std::span<const std::uint8_t> payload);

[[nodiscard]] std::optional<DanceManagementNotification>
DecodeDanceManagement(std::span<const std::uint8_t> payload);

[[nodiscard]] std::vector<std::uint8_t>
EncodeDanceCacheRecord(const DanceCacheRecord& dance);

}

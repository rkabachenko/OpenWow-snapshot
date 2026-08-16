#pragma once

#include "openwow/game/activities/dance/model/dance_move_catalog.h"
#include "openwow/game/activities/dance/model/dance_sequence.h"
#include "openwow/game/activities/dance/model/dance_studio_messages.h"

#include <array>
#include <cstdint>
#include <optional>

namespace openwow::game {

struct DancePlaybackRandomState final {
  std::array<std::uint32_t, 256> memory{};
  std::array<std::uint32_t, 256> results{};
  std::uint32_t accumulator = 0;
  std::uint32_t last_result = 0;
  std::uint32_t remaining_results = 0;
};

void InitializeDancePlaybackRandomState(DancePlaybackRandomState& state,
                                        std::uint32_t seed);

[[nodiscard]] std::uint32_t ComputeDancePlaybackIndex(
    std::uint32_t move_count, DancePlaybackRandomState& state);

[[nodiscard]] bool MeetsDanceMoveRequirements(
    const DanceMoveRecord& dance_move,
    std::optional<DancePlayerClass> player_class,
    LearnedDanceMoveMask learned_move_mask);

[[nodiscard]] DanceSequence BuildDancePlaybackSequence(
    const DanceCacheRecord& dance, DancePlaybackStep start_step,
    DancePlaybackSeed seed, const DanceMoveCatalog& catalog);

}

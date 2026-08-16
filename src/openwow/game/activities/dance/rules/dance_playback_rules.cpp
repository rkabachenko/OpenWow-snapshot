#include "openwow/game/activities/dance/rules/dance_playback_rules.h"

#include <cstddef>
#include <cstdint>
#include <ctime>

namespace openwow::game {
namespace {

constexpr double kIsaacUint32ToUnitFloat = 2.3283064e-10;
constexpr double kDancePlaybackIndexEpsilon = 0.000001;

[[nodiscard]] std::uint32_t DancePlaybackCrtRand(const std::uint32_t seed) {
  const std::uint32_t next_state = 214013u * seed + 2531011u;
  return (next_state >> 16) & 0x7FFFu;
}

void AdvanceDancePlaybackIsaac(DancePlaybackRandomState& state) {
  std::uint32_t accumulator = state.accumulator;
  std::uint32_t last_result = state.last_result;
  int table_bias = -127;

  for (std::size_t index = 0; index < state.memory.size(); index += 4) {
    const std::uint32_t x0 = state.memory[index];
    const std::uint32_t mix0 =
        state.memory[static_cast<std::uint8_t>(table_bias - 1)] +
        ((accumulator >> 13) ^ (accumulator << 19));
    const std::uint32_t y0 =
        mix0 + last_result + state.memory[static_cast<std::uint8_t>(x0)];
    state.memory[index] = y0;
    const std::uint32_t result0 =
        x0 + state.memory[static_cast<std::uint8_t>(y0 >> 8)];
    state.results[index] = result0;

    const std::uint32_t x1 = state.memory[index + 1];
    const std::uint32_t mix1 =
        state.memory[static_cast<std::uint8_t>(table_bias)] +
        ((mix0 >> 13) ^ (mix0 << 19));
    const std::uint32_t y1 =
        mix1 + result0 + state.memory[static_cast<std::uint8_t>(x1)];
    state.memory[index + 1] = y1;
    const std::uint32_t result1 =
        x1 + state.memory[static_cast<std::uint8_t>(y1 >> 8)];
    state.results[index + 1] = result1;

    const std::uint32_t x2 = state.memory[index + 2];
    const std::uint32_t mix2 =
        state.memory[static_cast<std::uint8_t>(table_bias + 1)] +
        ((mix1 >> 13) ^ (mix1 << 19));
    const std::uint32_t y2 =
        mix2 + result1 + state.memory[static_cast<std::uint8_t>(x2)];
    state.memory[index + 2] = y2;
    const std::uint32_t result2 =
        x2 + state.memory[static_cast<std::uint8_t>(y2 >> 8)];
    state.results[index + 2] = result2;

    const std::uint32_t x3 = state.memory[index + 3];
    accumulator =
        state.memory[static_cast<std::uint8_t>(table_bias + 2)] +
        ((mix2 >> 13) ^ (mix2 << 19));
    const std::uint32_t y3 =
        accumulator + result2 + state.memory[static_cast<std::uint8_t>(x3)];
    state.memory[index + 3] = y3;
    last_result = x3 + state.memory[static_cast<std::uint8_t>(y3 >> 8)];
    state.results[index + 3] = last_result;

    table_bias += 4;
  }

  state.accumulator = accumulator;
  state.last_result = last_result;
}

[[nodiscard]] double NextDancePlaybackRandomUnit(
    DancePlaybackRandomState& state) {
  const std::uint32_t remaining = state.remaining_results;
  state.remaining_results = remaining - 1;
  if (remaining != 0) {
    return static_cast<double>(state.results[remaining - 1]) *
           kIsaacUint32ToUnitFloat;
  }

  AdvanceDancePlaybackIsaac(state);
  state.remaining_results = 255;
  return static_cast<double>(state.results[255]) *
         kIsaacUint32ToUnitFloat;
}

}

void InitializeDancePlaybackRandomState(DancePlaybackRandomState& state,
                                        const std::uint32_t seed) {
  std::uint32_t effective_seed =
      seed == 0 ? static_cast<std::uint32_t>(std::time(nullptr)) : seed;
  std::uint32_t value = DancePlaybackCrtRand(effective_seed) | 1u;
  state.accumulator = 0;
  state.last_result = 0;

  for (std::uint32_t& entry : state.memory) {
    const std::uint32_t quotient = value / 127773u;
    std::int64_t next_value =
        16807ll * static_cast<std::int64_t>(value) -
        0x7FFFFFFFll * static_cast<std::int64_t>(quotient);
    if (next_value < 0) {
      next_value += 0x7FFFFFFFll;
    }
    value = static_cast<std::uint32_t>(next_value);
    entry = value;
  }

  AdvanceDancePlaybackIsaac(state);
  state.remaining_results = 256;
}

std::uint32_t ComputeDancePlaybackIndex(
    const std::uint32_t move_count, DancePlaybackRandomState& state) {
  if (move_count == 0) {
    return 0;
  }

  const double adjusted =
      NextDancePlaybackRandomUnit(state) * static_cast<double>(move_count) -
      kDancePlaybackIndexEpsilon;
  const std::uint32_t last_index = move_count - 1;
  if (static_cast<int>(adjusted) < static_cast<int>(last_index)) {
    return static_cast<std::uint32_t>(static_cast<int>(adjusted));
  }
  return last_index;
}

bool MeetsDanceMoveRequirements(
    const DanceMoveRecord& dance_move,
    const std::optional<DancePlayerClass> player_class,
    const LearnedDanceMoveMask learned_move_mask) {
  if (dance_move.required_classes.value != 0) {
    if (!player_class) {
      return false;
    }
    const auto class_index = static_cast<std::uint8_t>(*player_class) - 1u;
    const std::uint32_t class_bit = 1u << class_index;
    if ((dance_move.required_classes.value & class_bit) == 0) {
      return false;
    }
  }

  if (!dance_move.required_learned_move) {
    return true;
  }

  const std::uint32_t learned_move_index =
      dance_move.required_learned_move->value;
  if (learned_move_index >= 64) {
    return false;
  }
  return (learned_move_mask.value & (1ull << learned_move_index)) != 0;
}

DanceSequence BuildDancePlaybackSequence(
    const DanceCacheRecord& dance, const DancePlaybackStep start_step,
    const DancePlaybackSeed seed, const DanceMoveCatalog& catalog) {
  DanceSequence sequence;
  sequence.start_position = DanceSequencePosition{start_step.value};
  sequence.steps.reserve(dance.moves.size());

  DancePlaybackRandomState random_state;
  InitializeDancePlaybackRandomState(random_state, seed.value);

  for (const DanceCacheMove& move : dance.moves) {
    if (move.chance.value != 0 &&
        ComputeDancePlaybackIndex(100, random_state) >= move.chance.value) {
      continue;
    }

    DanceMoveId step_id = move.move_id;
    if (move.resolution_mode == DanceMoveResolutionMode::kCatalogFallback) {
      if (const DanceMoveRecord* move_record = catalog.Lookup(move.move_id);
          move_record != nullptr) {
        step_id = move_record->fallback_step_id;
      }
    }

    sequence.steps.push_back(step_id);
  }

  return sequence;
}

}

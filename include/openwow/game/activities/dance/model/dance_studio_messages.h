#pragma once

#include "openwow/game/activities/dance/model/dance_types.h"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace openwow::game {

enum class DanceManagementError : std::uint8_t {
  kNameTaken,
  kMaximumDancesReached,
  kUnknownDance,
};

struct DanceCacheMove final {
  DanceMoveId move_id;
  DanceMoveChancePercent chance;
  DanceMoveResolutionMode resolution_mode =
      DanceMoveResolutionMode::kDirect;
};

struct DanceCacheRecord final {
  DanceId id;
  DanceUnitGuid creator_guid;
  std::string name;
  std::vector<DanceCacheMove> moves;
  DanceChecksum checksum;
};

struct StopDanceCommand final {
  DanceUnitGuid unit_guid;
};

struct PlayDanceCommand final {
  DanceUnitGuid unit_guid;
  DanceId dance_id;
  DancePlaybackStep start_step;
  DancePlaybackSeed seed;
  DanceChecksum checksum;
};

struct DanceQueryFound final {
  DanceCacheRecord dance;
};

struct DanceQueryMissing final {
  DanceId dance_id;
};

using DanceQueryResult = std::variant<DanceQueryFound, DanceQueryMissing>;

struct InvalidateDanceCommand final {
  DanceId dance_id;
};

struct LearnedDanceMovesUpdate final {
  LearnedDanceMoveMask learned_move_mask;
};

struct DanceManagementChange final {
  DanceManagementOperations operations;
  DanceId dance_id;
  std::string name;
  DanceSequenceId sequence_id;
};

struct DanceManagementFailure final {
  std::optional<DanceManagementError> error;
};

struct DanceManagementNotification final {
  std::variant<DanceManagementChange, DanceManagementFailure> payload;
};

}

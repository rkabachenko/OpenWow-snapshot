#pragma once

#include "openwow/game/monster_move.h"
#include "openwow/game/object_types.h"

#include <cstdint>

namespace openwow::game {

enum class EmoteValidationFlag : std::uint16_t {
  kRequireStandStateZero = 0x0001,
  kSkipSleepDeadStandBlock = 0x0200,
  kRejectWhenCallerFlagSet = 0x0400,
  kRejectWhileSwimming = 0x0080,
  kSetMovementWarning = 0x4000,
  kRejectWhileSplineFlying = 0x8000,
};

inline constexpr std::uint8_t kStandStateSleep = 3;
inline constexpr std::uint8_t kStandStateDead = 7;
inline constexpr std::uint32_t kValidateEmoteMovementWarningMask = 0x00C010FFu;

struct EmoteValidationState {
  std::uint8_t stand_state = 0;
  std::uint32_t movement_flags = 0;
  std::uint32_t spline_flags = 0;
  bool caller_flag = false;
  bool has_active_spline = false;
};

struct EmoteValidationResult {
  bool allowed = true;
  bool needs_movement_warning = false;
};

[[nodiscard]] inline bool HasEmoteValidationFlag(const std::uint16_t flags,
                                                 const EmoteValidationFlag flag) {
  return (flags & static_cast<std::uint16_t>(flag)) != 0;
}

[[nodiscard]] inline EmoteValidationResult ValidateEmoteConditions(
    const std::uint16_t emote_flags,
    const EmoteValidationState& state) {
  EmoteValidationResult result{};

  if (state.caller_flag &&
      HasEmoteValidationFlag(emote_flags, EmoteValidationFlag::kRejectWhenCallerFlagSet)) {
    result.allowed = false;
    return result;
  }

  if (HasEmoteValidationFlag(emote_flags, EmoteValidationFlag::kRequireStandStateZero) &&
      state.stand_state != 0) {
    result.allowed = false;
    return result;
  }

  if (HasEmoteValidationFlag(emote_flags, EmoteValidationFlag::kRejectWhileSwimming) &&
      (state.movement_flags & kMoveFlagSwimming) != 0) {
    result.allowed = false;
    return result;
  }

  if (HasEmoteValidationFlag(emote_flags, EmoteValidationFlag::kRejectWhileSplineFlying)) {
    if (state.has_active_spline &&
        SplineFlag::HasNonExemptFlag(state.spline_flags, SplineFlag::kFlying)) {
      result.allowed = false;
      return result;
    }

    if ((state.movement_flags & kMoveFlagFlying) != 0) {
      result.allowed = false;
      return result;
    }
  }

  if (!HasEmoteValidationFlag(emote_flags, EmoteValidationFlag::kSkipSleepDeadStandBlock) &&
      (state.stand_state == kStandStateSleep || state.stand_state == kStandStateDead)) {
    result.allowed = false;
    return result;
  }

  if (HasEmoteValidationFlag(emote_flags, EmoteValidationFlag::kSetMovementWarning) &&
      (state.movement_flags & kValidateEmoteMovementWarningMask) != 0) {
    result.needs_movement_warning = true;
  }

  return result;
}

}

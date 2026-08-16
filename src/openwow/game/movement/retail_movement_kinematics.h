#pragma once

#include "openwow/game/movement_info.h"
#include "openwow/game/object_types.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace openwow::game::movement {

struct RetailMovementStep {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float orientation = 0.0f;
  float pitch = 0.0f;
};

[[nodiscard]] inline RetailMovementStep ComputeRetailMovementStep(
    const MovementInfo &info, const std::array<float, kMaxSpeeds> &speeds,
    const float elapsed_seconds, const float effective_speed = -1.0f) {
  constexpr float kInvSqrt2 = 0.70710677f;
  constexpr float kTwoPi = 6.2831855f;
  constexpr std::uint32_t kLinearMask = 0x00C0100Fu;

  RetailMovementStep result{
      .orientation = info.orientation,
      .pitch = info.pitch,
  };
  if (elapsed_seconds <= 0.0f) {
    return result;
  }

  const bool backward = (info.flags & kMoveFlagBackward) != 0u;
  float speed = effective_speed >= 0.0f ? effective_speed : 0.0f;
  if (effective_speed < 0.0f && (info.flags & 0x00C0000Fu) != 0u) {
    if ((info.flags & kMoveFlagFlying) != 0u) {
      speed = backward ? std::min(speeds[kSpeedFlight],
                                  speeds[kSpeedFlightBack])
                       : speeds[kSpeedFlight];
    } else if ((info.flags & kMoveFlagSwimming) != 0u) {
      speed = backward ? std::min(speeds[kSpeedSwim], speeds[kSpeedSwimBack])
                       : speeds[kSpeedSwim];
    } else if ((info.flags & kMoveFlagWalking) != 0u) {
      speed = std::min(speeds[kSpeedWalk], speeds[kSpeedRun]);
    } else {
      speed = backward ? std::min(speeds[kSpeedRun], speeds[kSpeedRunBack])
                       : speeds[kSpeedRun];
    }
  }

  float turn_rate = 0.0f;
  if ((info.flags & kMoveFlagTurnLeft) != 0u) {
    turn_rate = speeds[kSpeedTurnRate];
  } else if ((info.flags & kMoveFlagTurnRight) != 0u) {
    turn_rate = -speeds[kSpeedTurnRate];
  }
  if ((info.flags & kLinearMask) != 0u &&
      (info.flags2 & kMoveFlag2FullSpeedTurning) == 0u) {
    turn_rate *= 0.75f;
  }

  float pitch_rate = 0.0f;
  if ((info.flags & kMoveFlagPitchUp) != 0u) {
    pitch_rate = speeds[kSpeedPitchRate];
  } else if ((info.flags & kMoveFlagPitchDown) != 0u) {
    pitch_rate = -speeds[kSpeedPitchRate];
  }
  if ((info.flags & kLinearMask) != 0u &&
      (info.flags2 & kMoveFlag2FullSpeedPitching) == 0u) {
    pitch_rate *= 0.75f;
  }

  if ((info.flags & (kMoveFlagTurnLeft | kMoveFlagTurnRight)) != 0u) {
    result.orientation =
        std::fmod(info.orientation + turn_rate * elapsed_seconds, kTwoPi);
    if (result.orientation < 0.0f) {
      result.orientation += kTwoPi;
    }
  }
  if ((info.flags & (kMoveFlagPitchUp | kMoveFlagPitchDown)) != 0u) {
    result.pitch =
        std::fmod(info.pitch + pitch_rate * elapsed_seconds, kTwoPi);
  }

  const float facing_cos = std::cos(info.orientation);
  const float facing_sin = std::sin(info.orientation);
  const bool pitched_direction =
      (info.flags & (kMoveFlagSwimming | kMoveFlagFlying)) != 0u;
  const float pitch_cos = pitched_direction ? std::cos(info.pitch) : 1.0f;
  const float pitch_sin = pitched_direction ? std::sin(info.pitch) : 0.0f;

  float forward_axis = backward
                           ? -1.0f
                           : (info.flags & kMoveFlagForward) != 0u ? 1.0f
                                                                  : 0.0f;
  float strafe_axis = (info.flags & kMoveFlagStrafeLeft) != 0u
                          ? 1.0f
                          : (info.flags & kMoveFlagStrafeRight) != 0u ? -1.0f
                                                                     : 0.0f;
  const bool has_forward = forward_axis != 0.0f;
  const bool has_strafe = strafe_axis != 0.0f;
  if (has_forward && has_strafe) {
    forward_axis *= kInvSqrt2;
    strafe_axis *= kInvSqrt2;
  }

  const float direction_x =
      facing_cos * pitch_cos * forward_axis - facing_sin * strafe_axis;
  const float direction_y =
      facing_sin * pitch_cos * forward_axis + facing_cos * strafe_axis;
  const float direction_z = pitch_sin * forward_axis;

  const float direction_planar_x =
      facing_cos * forward_axis - facing_sin * strafe_axis;
  const float direction_planar_y =
      facing_sin * forward_axis + facing_cos * strafe_axis;

  unsigned state = 0u;
  if ((info.flags & (kMoveFlagTurnLeft | kMoveFlagTurnRight)) != 0u &&
      (info.flags & kMoveFlagFalling) == 0u) {
    state |= 2u;
  }
  const bool has_vertical =
      (info.flags & (kMoveFlagAscending | kMoveFlagDescending)) != 0u;
  if ((info.flags & (kMoveFlagPitchUp | kMoveFlagPitchDown)) != 0u &&
      !has_vertical) {
    state |= 8u;
  }
  if (has_vertical) state |= 16u;
  if (has_forward) state |= 1u;
  if (has_strafe) state |= 4u;

  const auto straight = [&] {
    result.x = direction_x * speed * elapsed_seconds;
    result.y = direction_y * speed * elapsed_seconds;
    result.z = direction_z * speed * elapsed_seconds;
  };
  const auto vertical_sign = [&] {
    return (info.flags & kMoveFlagAscending) != 0u ? 1.0f : -1.0f;
  };
  const auto turning_arc = [&] {
    const float radius = speed / turn_rate;
    const float sine_distance = std::sin(turn_rate * elapsed_seconds) * radius;
    const float cosine_distance =
        radius - std::cos(turn_rate * elapsed_seconds) * radius;
    float planar_scale = 1.0f;
    if (has_vertical) {
      planar_scale = kInvSqrt2;
      result.z = vertical_sign() * speed * elapsed_seconds * kInvSqrt2;
    } else if (has_forward) {
      planar_scale = pitch_cos;
      result.z = speed * elapsed_seconds * pitch_sin;
    }
    result.x = (sine_distance * direction_planar_x -
                cosine_distance * direction_planar_y) *
               planar_scale;
    result.y = (sine_distance * direction_planar_y +
                cosine_distance * direction_planar_x) *
               planar_scale;
  };
  const auto pitching_arc = [&] {
    const float radius = speed / pitch_rate;
    const float sine_distance = std::sin(pitch_rate * elapsed_seconds) * radius;
    const float cosine_distance =
        radius - std::cos(pitch_rate * elapsed_seconds) * radius;
    const float planar_distance =
        sine_distance * pitch_cos - cosine_distance * pitch_sin;
    result.x = planar_distance * direction_planar_x;
    result.y = planar_distance * direction_planar_y;
    result.z = sine_distance * pitch_sin + cosine_distance * pitch_cos;
  };
  const auto turning_and_pitching_arc = [&] {
    const float turn_radius = speed * kInvSqrt2 / turn_rate;
    const float pitch_radius = speed * kInvSqrt2 / pitch_rate;
    const float turn_sine =
        std::sin(turn_rate * elapsed_seconds) * turn_radius;
    const float turn_cosine =
        turn_radius - std::cos(turn_rate * elapsed_seconds) * turn_radius;
    const float pitch_sine_distance =
        std::sin(pitch_rate * elapsed_seconds) * pitch_radius;
    const float pitch_cosine_distance =
        pitch_radius - std::cos(pitch_rate * elapsed_seconds) * pitch_radius;
    result.x = turn_sine * direction_planar_x -
               turn_cosine * direction_planar_y;
    result.y = turn_sine * direction_planar_y +
               turn_cosine * direction_planar_x;
    result.z = pitch_sine_distance * pitch_sin +
               pitch_cosine_distance * pitch_cos;
  };

  switch (state) {
    case 1u:
    case 4u:
    case 5u:
    case 12u:
      straight();
      break;
    case 3u:
    case 6u:
    case 7u:
    case 14u:
    case 19u:
    case 22u:
    case 23u:
      turning_arc();
      break;
    case 9u:
    case 13u:
      pitching_arc();
      break;
    case 11u:
    case 15u:
      turning_and_pitching_arc();
      break;
    case 16u:
    case 18u:
      result.z = vertical_sign() * speed * elapsed_seconds;
      break;
    case 17u:
    case 20u:
    case 21u:

      result.x = facing_cos * speed * elapsed_seconds * kInvSqrt2;
      result.y = facing_sin * speed * elapsed_seconds * kInvSqrt2;
      result.z = vertical_sign() * speed * elapsed_seconds * kInvSqrt2;
      break;
    default:
      break;
  }
  return result;
}

}

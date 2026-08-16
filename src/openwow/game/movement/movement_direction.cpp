
#include "openwow/game/movement/player_move_event.h"
#include "openwow/game/movement_callbacks.h"
#include "openwow/game/movement/retail_fall_kinematics.h"
#include "openwow/game/object_types.h"
#include "openwow/foundation/math/row_major_mat4x4.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>

namespace openwow::game::movement {

namespace {

constexpr float kInvSqrt2 = 0.70710677f;

constexpr std::uint32_t kForwardBackMask = 0x03u;

constexpr std::uint32_t kStrafeMask = 0x0Cu;

constexpr std::uint32_t kTurnInputMask =
    openwow::game::kMoveFlagTurnLeft | openwow::game::kMoveFlagTurnRight;

constexpr std::uint32_t kPitchInputMask =
    openwow::game::kMoveFlagPitchUp | openwow::game::kMoveFlagPitchDown;

constexpr std::uint32_t kSpeedActiveMask = 0x00C0000Fu;
}

void CMovementData::EvaluateMemoisedTrig(TrigMemo& memo, const float angle,
                                         float& out_cos, float& out_sin) {
  const std::uint32_t angle_bits = std::bit_cast<std::uint32_t>(angle);
  if (!memo.valid || memo.angle_bits != angle_bits) {
    memo.valid = true;
    memo.angle_bits = angle_bits;
    memo.cos = std::cos(angle);
    memo.sin = std::sin(angle);
  }
  out_cos = memo.cos;
  out_sin = memo.sin;
}

void CMovementData::ComputeFacingVectors() {
  const float facing = scalar_facing_;
  EvaluateMemoisedTrig(facing_trig_memo_, facing, facing_cos_, facing_sin_);

  constexpr std::uint32_t kSwimOrFly =
      openwow::game::kMoveFlagSwimming | openwow::game::kMoveFlagFlying;
  constexpr float kPitchEpsilon = 0.0000009536743f;

  if ((runtime_flags_ & kSwimOrFly) != 0u &&
      (std::fabs(runtime_pitch_) >= kPitchEpsilon ||
       std::isnan(runtime_pitch_))) {
    EvaluateMemoisedTrig(pitch_trig_memo_, runtime_pitch_, pitch_cos_,
                         pitch_sin_);

    dir_forward_[0] = facing_cos_ * pitch_cos_;
    dir_forward_[1] = facing_sin_ * pitch_cos_;
    dir_forward_[2] = pitch_sin_;
  } else {
    dir_forward_[0] = facing_cos_;
    dir_forward_[1] = facing_sin_;
    dir_forward_[2] = 0.0f;
    pitch_cos_ = 1.0f;
    pitch_sin_ = 0.0f;
  }
}

float CMovementData::CalculateCurrentSpeed(bool use_walk_cap) const {
  if ((runtime_flags_ & kSpeedActiveMask) == 0u) {
    return 0.0f;
  }

  const bool backward = (runtime_flags_ & openwow::game::kMoveFlagBackward) != 0u;

  if ((runtime_flags_ & openwow::game::kMoveFlagFlying) != 0u) {
    const float flight = speed_table_[openwow::game::kSpeedFlight];
    const float flight_back = speed_table_[openwow::game::kSpeedFlightBack];
    if (backward && flight >= flight_back) {
      return flight_back;
    }
    return flight;
  }

  if ((runtime_flags_ & openwow::game::kMoveFlagSwimming) != 0u) {
    const float swim = speed_table_[openwow::game::kSpeedSwim];
    const float swim_back = speed_table_[openwow::game::kSpeedSwimBack];
    if (backward && swim >= swim_back) {
      return swim_back;
    }
    return swim;
  }

  const float walk = speed_table_[openwow::game::kSpeedWalk];
  const float run = speed_table_[openwow::game::kSpeedRun];
  const float run_back = speed_table_[openwow::game::kSpeedRunBack];

  const bool walking = use_walk_cap ||
      (runtime_flags_ & openwow::game::kMoveFlagWalking) != 0u;

  if (walking) {

    return (run > walk) ? walk : run;
  }

  if (backward && run >= run_back) {
    return run_back;
  }
  return run;
}

void CMovementData::ComputeDirectionVectors(bool force) {
  if ((runtime_flags_ & openwow::game::kMoveFlagFalling) != 0u && !force) {
    return;
  }

  ComputeFacingVectors();

  const std::uint32_t flags = runtime_flags_;
  const bool has_forward_back = (flags & kForwardBackMask) != 0u;
  const bool has_strafe = (flags & kStrafeMask) != 0u;

  if (has_forward_back) {
    if (has_strafe) {

      float saved_facing_cos = facing_cos_;
      float saved_facing_sin = facing_sin_;
      float fwd_x = dir_forward_[0];
      float fwd_y = dir_forward_[1];
      float fwd_z = dir_forward_[2];

      if ((flags & openwow::game::kMoveFlagBackward) != 0u) {
        saved_facing_cos = -saved_facing_cos;
        saved_facing_sin = -saved_facing_sin;
        fwd_x = -fwd_x;
        fwd_y = -fwd_y;
        fwd_z = -fwd_z;
      }

      float rotated_cos = facing_sin_;
      float rotated_sin = facing_cos_;

      if ((flags & openwow::game::kMoveFlagStrafeLeft) != 0u) {

        rotated_cos = -rotated_cos;
      } else {

        rotated_sin = -rotated_sin;
      }

      dir_forward_[0] = rotated_cos + fwd_x;
      dir_forward_[1] = rotated_sin + fwd_y;
      dir_forward_[2] = fwd_z;
      facing_cos_ = rotated_cos + saved_facing_cos;
      facing_sin_ = rotated_sin + saved_facing_sin;

      dir_forward_[0] *= kInvSqrt2;
      dir_forward_[1] *= kInvSqrt2;
      dir_forward_[2] *= kInvSqrt2;
      facing_cos_ *= kInvSqrt2;
      facing_sin_ *= kInvSqrt2;
      return;
    }

    if ((flags & openwow::game::kMoveFlagBackward) != 0u) {

      dir_forward_[0] = -dir_forward_[0];
      dir_forward_[1] = -dir_forward_[1];
      dir_forward_[2] = -dir_forward_[2];
      facing_cos_ = -facing_cos_;
      facing_sin_ = -facing_sin_;
    }
    return;
  }

  if (has_strafe) {

    float rotated_cos = facing_sin_;
    float rotated_sin = facing_cos_;

    if ((flags & openwow::game::kMoveFlagStrafeLeft) != 0u) {
      rotated_cos = -rotated_cos;
    } else {
      rotated_sin = -rotated_sin;
    }

    dir_forward_[0] = rotated_cos;
    dir_forward_[1] = rotated_sin;
    dir_forward_[2] = 0.0f;
    facing_cos_ = rotated_cos;
    facing_sin_ = rotated_sin;
    return;
  }

}

void CMovementData::ApplyAirborneCollisionKinematics(
    const float current_speed, const float horizontal_x,
    const float horizontal_y) {
  const float length_squared =
      horizontal_x * horizontal_x + horizontal_y * horizontal_y;
  if (length_squared >= 2.3841858e-7f) {
    const float inverse_length = 1.0f / std::sqrt(length_squared);
    const float normalized_x = horizontal_x * inverse_length;
    const float normalized_y = horizontal_y * inverse_length;

    dir_forward_[0] = normalized_x;
    dir_forward_[1] = normalized_y;
    dir_forward_[2] = 0.0f;
    facing_cos_ = normalized_x;
    facing_sin_ = normalized_y;
  } else {
    dir_forward_[0] = 0.0f;
    dir_forward_[1] = 0.0f;
    dir_forward_[2] = 0.0f;
    facing_cos_ = 0.0f;
    facing_sin_ = 0.0f;
  }
  current_speed_ = std::max(0.0f, current_speed);
}

void CMovementData::SetCurrentSpeedFromAirborneCollision(
    const float current_speed) {
  current_speed_ = std::max(0.0f, current_speed);
}

void CMovementData::ApplyDirectionStateTransition(
    const SpeedRefreshTiming speed_refresh) {
  const bool falling =
      (runtime_flags_ & openwow::game::kMoveFlagFalling) != 0u;
  if (!falling && speed_refresh == SpeedRefreshTiming::kBeforeDirection) {
    current_speed_ = CalculateCurrentSpeed(false);
  }

  SnapshotStateForDirectionRecompute();

  if (!falling && speed_refresh == SpeedRefreshTiming::kAfterDirection) {
    current_speed_ = CalculateCurrentSpeed(false);
  }
}

bool CMovementData::SetForwardDirection(const bool forward) {
  const std::uint32_t old_flags = runtime_flags_;
  constexpr std::uint32_t kPendingForwardClearMask =
      openwow::game::kMoveFlagPendingStop |
      openwow::game::kMoveFlagPendingForward |
      openwow::game::kMoveFlagPendingBackward;
  runtime_flags_ &= ~kPendingForwardClearMask;

  const bool falling =
      (old_flags & openwow::game::kMoveFlagFalling) != 0u;
  constexpr std::uint32_t kDirectionalMask = 0x0Fu;
  if (falling && (old_flags & kDirectionalMask) != 0u) {
    const std::uint32_t requested_flag =
        forward ? openwow::game::kMoveFlagForward
                : openwow::game::kMoveFlagBackward;
    if ((old_flags & requested_flag) == 0u) {
      runtime_flags_ |=
          forward ? openwow::game::kMoveFlagPendingForward
                  : openwow::game::kMoveFlagPendingBackward;
    }
    return false;
  }

  runtime_flags_ &= ~(openwow::game::kMoveFlagForward |
                       openwow::game::kMoveFlagBackward);
  runtime_flags_ |= forward ? openwow::game::kMoveFlagForward
                             : openwow::game::kMoveFlagBackward;
  prev_position_ = transform_position_;
  prev_orientation_ = scalar_facing_;
  prev_pitch_ = runtime_pitch_;
  interpolation_progress_ = 0.0f;
  ComputeDirectionVectors(falling);
  current_speed_ = CalculateCurrentSpeed(falling);
  return true;
}

void CMovementData::UpdateDirectionOnPositionChange() {
  if ((runtime_flags_ & openwow::game::kMoveFlagFalling) != 0u) {

    runtime_flags_ &= ~(openwow::game::kMoveFlagFalling |
                         openwow::game::kMoveFlagFallingFar);
  }

  if ((runtime_flags_ & openwow::game::kMoveFlagPendingRoot) != 0u) {

    constexpr std::uint32_t kPendingRootKeepMask = 0xFF203700u;
    runtime_flags_ = (runtime_flags_ & kPendingRootKeepMask) |
                     openwow::game::kMoveFlagRoot;
  }

  ApplyDirectionStateTransition(SpeedRefreshTiming::kAfterDirection);
}

void CMovementData::UpdateDirectionConditional() {
  const bool was_dirty =
      (runtime_flags_ & openwow::game::kMoveFlagFalling) != 0u;

  if (was_dirty) {
    runtime_flags_ &= ~(openwow::game::kMoveFlagFalling |
                         openwow::game::kMoveFlagFallingFar);
  }

  if ((runtime_flags_ & openwow::game::kMoveFlagPendingRoot) != 0u) {
    constexpr std::uint32_t kPendingRootKeepMask = 0xFF203700u;
    runtime_flags_ = (runtime_flags_ & kPendingRootKeepMask) |
                     openwow::game::kMoveFlagRoot;
  } else if (!was_dirty) {

    return;
  }

  ApplyDirectionStateTransition(SpeedRefreshTiming::kAfterDirection);
}

bool CMovementData::SetSpeed(openwow::game::SpeedType type, float value) {
  const auto index = static_cast<std::size_t>(type);
  if (index >= speed_table_.size()) {
    return false;
  }

  constexpr float kSpeedChangeEpsilon = 0.00000023841858f;
  if (!(std::fabs(value - speed_table_[index]) >= kSpeedChangeEpsilon)) {
    return false;
  }

  speed_table_[index] = value;

  const bool refresh_linear_speed =
      type != openwow::game::kSpeedTurnRate &&
      type != openwow::game::kSpeedPitchRate;
  ApplyDirectionStateTransition(
      refresh_linear_speed ? SpeedRefreshTiming::kAfterDirection
                           : SpeedRefreshTiming::kNone);

  return true;
}

float CMovementData::GetSpeed(openwow::game::SpeedType type) const {
  const auto index = static_cast<std::size_t>(type);
  if (index < speed_table_.size()) {
    return speed_table_[index];
  }
  return 0.0f;
}

void CMovementData::ApplySplineMovementMode(const SplineMovementMode mode) {
  switch (mode) {
    case SplineMovementMode::kUnroot:
      ForceRemoveRoot(true);
      return;
    case SplineMovementMode::kFeatherFall:
      SetFallingSlowState(true);
      return;
    case SplineMovementMode::kNormalFall:
      SetFallingSlowState(false);
      return;
    case SplineMovementMode::kHover:
      SetHoverState(true);
      return;
    case SplineMovementMode::kNoHover:
      SetHoverState(false);
      return;
    case SplineMovementMode::kWaterWalk:
      runtime_flags_ |= openwow::game::kMoveFlagWaterwalking;
      return;
    case SplineMovementMode::kLandWalk:
      runtime_flags_ &= ~openwow::game::kMoveFlagWaterwalking;
      return;
    case SplineMovementMode::kStartSwim:
      ResetMovementBaseState();
      (void)RecalculateStateFlags();
      return;
    case SplineMovementMode::kStopSwim: {
      ApplyStopSwimState();
      return;
    }
    case SplineMovementMode::kRun:
      runtime_flags_ &= ~openwow::game::kMoveFlagWalking;
      ApplyDirectionStateTransition(SpeedRefreshTiming::kAfterDirection);
      return;
    case SplineMovementMode::kWalk:
      runtime_flags_ |= openwow::game::kMoveFlagWalking;
      ApplyDirectionStateTransition(SpeedRefreshTiming::kAfterDirection);
      return;
    case SplineMovementMode::kRoot:
      ForceApplyRoot();
      return;
    case SplineMovementMode::kSetFlying:

      if (!EnableFlyMode()) {
        return;
      }
      return;
    case SplineMovementMode::kUnsetFlying:
      DisableFlyMode();
      return;
    case SplineMovementMode::kGravityDisable:
      (void)SetGravityEnabledAndRefresh(false);
      return;
    case SplineMovementMode::kGravityEnable:
      (void)SetGravityEnabledAndRefresh(true);
      return;
  }
}

void CMovementData::ClearParentMovement() {
  vehicle_seat_transfer_runtime_flags_ &= ~kHasParentMovementUpdateBitMask;
  if (has_parent_movement_) {
    has_parent_movement_ = false;
    parent_movement_flags_ = 0u;
    if ((runtime_flags_ & openwow::game::kMoveFlagFalling) == 0u) {
      current_speed_ = CalculateCurrentSpeed(false);
    }
  }
}

bool CMovementData::ProcessPendingTurnStop() {
  if ((runtime_flags_ & kTurnInputMask) == 0u) {
    return false;
  }

  runtime_flags_ &= ~kTurnInputMask;
  ApplyDirectionStateTransition(SpeedRefreshTiming::kNone);

  return true;
}

bool CMovementData::ProcessPendingPitchStop() {
  if ((runtime_flags_ & kPitchInputMask) == 0u) {
    return false;
  }

  runtime_flags_ &= ~kPitchInputMask;
  ApplyDirectionStateTransition(SpeedRefreshTiming::kNone);

  return true;
}

void CMovementData::SetPitchDirection(bool pitchUp) {
  constexpr std::uint32_t kSwimOrFly =
      openwow::game::kMoveFlagSwimming | openwow::game::kMoveFlagFlying;
  if ((runtime_flags_ & kSwimOrFly) == 0u &&
      (runtime_flags2_ & openwow::game::kMoveFlag2AlwaysAllowPitching) == 0u) {
    return;
  }

  runtime_flags_ &= ~kPitchInputMask;
  runtime_flags_ |=
      pitchUp ? openwow::game::kMoveFlagPitchUp
              : openwow::game::kMoveFlagPitchDown;

  runtime_flags2_ &= ~openwow::game::kMoveFlag2InterpolatedPitching;

  SnapshotStateForDirectionRecompute();
}

bool CMovementData::SetPitchAndTestSteepFall(float new_pitch) {
  constexpr float kPitchEpsilon = 9.5367431640625e-7f;
  constexpr float kSteepPitchThreshold = -0.6457718014717102f;
  constexpr std::uint32_t kSteepFallBlockingMotion =
      kActiveMoverMotionMask | openwow::game::kMoveFlagHover;

  constexpr std::uint32_t kSwimOrFly =
      openwow::game::kMoveFlagSwimming | openwow::game::kMoveFlagFlying;

  if (std::fabs(new_pitch - runtime_pitch_) < kPitchEpsilon) {
    runtime_flags_ &= ~kPitchInputMask;
    return false;
  }

  const std::uint32_t flags = runtime_flags_;
  runtime_pitch_ = new_pitch;

  if ((flags & kSwimOrFly) != 0u) {
    SnapshotStateForDirectionRecompute();
    runtime_flags_ &= ~kPitchInputMask;
    return false;
  }

  if ((flags & openwow::game::kMoveFlagWaterwalking) != 0u &&
      (flags & kSteepFallBlockingMotion) == 0u &&
      (runtime_flags2_ &
       openwow::game::kMoveFlag2SuppressSteepPitchFall) == 0u &&
      new_pitch <= kSteepPitchThreshold) {
    runtime_flags_ &= ~kPitchInputMask;
    return true;
  }

  runtime_flags_ &= ~kPitchInputMask;
  return false;
}

void CMovementData::ApplySetFacingEvent(const float new_facing) {
  constexpr float kFacingEpsilon = 0.00000095367432f;

  if (std::fabs(new_facing - scalar_facing_) >= kFacingEpsilon) {
    scalar_facing_ = new_facing;

    if ((runtime_flags_ & openwow::game::kMoveFlagFalling) == 0u) {
      SnapshotStateForDirectionRecompute();
    }
  }

  runtime_flags_ &= ~kTurnInputMask;
}

bool CMovementData::SetAscendDescend(bool ascending) {
  constexpr std::uint32_t kSwimOrFly =
      openwow::game::kMoveFlagSwimming | openwow::game::kMoveFlagFlying;

  if ((runtime_flags_ & kSwimOrFly) == 0u) {
    return false;
  }

  if ((runtime_flags2_ & openwow::game::kMoveFlag2NoStrafe) != 0u) {
    return false;
  }

  if (ascending) {
    runtime_flags_ |= openwow::game::kMoveFlagAscending;
  } else {
    runtime_flags_ |= openwow::game::kMoveFlagDescending;
  }

  ApplyDirectionStateTransition(SpeedRefreshTiming::kAfterDirection);

  return true;
}

bool CMovementData::ClearAscendDescend() {
  constexpr std::uint32_t kSwimOrFly =
      openwow::game::kMoveFlagSwimming | openwow::game::kMoveFlagFlying;

  if ((runtime_flags_ & kSwimOrFly) == 0u) {
    return false;
  }

  constexpr std::uint32_t kAscendDescendMask =
      openwow::game::kMoveFlagAscending | openwow::game::kMoveFlagDescending;
  runtime_flags_ &= ~kAscendDescendMask;
  ApplyDirectionStateTransition(SpeedRefreshTiming::kBeforeDirection);

  return true;
}

bool CMovementData::TryStopForwardBackwardIfPreviouslyActive(
    std::uint32_t old_flags) {
  if ((old_flags & kForwardBackMask) == 0u) {
    return false;
  }
  if ((runtime_flags_ & openwow::game::kMoveFlagFalling) != 0u) {
    return false;
  }

  StopForwardBackwardAndRecalculate(true);
  return true;
}

bool CMovementData::TryClearStrafeIfPreviouslyActive(std::uint32_t old_flags) {
  if ((old_flags & kStrafeMask) == 0u) {
    return false;
  }
  if ((runtime_flags_ & openwow::game::kMoveFlagFalling) != 0u) {
    return false;
  }

  ClearStrafeAndRecalculate();
  return true;
}

void CMovementData::ClearStrafeAndRecalculate() {
  constexpr std::uint32_t kStrafeClearMask =
      openwow::game::kMoveFlagStrafeLeft |
      openwow::game::kMoveFlagStrafeRight |
      openwow::game::kMoveFlagPendingStrafeStop;
  runtime_flags_ &= ~kStrafeClearMask;
  ApplyDirectionStateTransition(SpeedRefreshTiming::kBeforeDirection);
}

int CMovementData::ExecStopForwardBackward() {
  std::uint32_t flags = runtime_flags_;

  if ((flags & kForwardBackMask) == 0u) {

    constexpr std::uint32_t kPendingDirMask =
        openwow::game::kMoveFlagPendingForward |

        openwow::game::kMoveFlagPendingBackward;

    if ((flags & kPendingDirMask) != 0u) {
      runtime_flags_ = flags & ~kPendingDirMask;
    }
    return 0;
  }

  if ((flags & openwow::game::kMoveFlagSplineElevation) != 0u) {
    runtime_flags_ = flags & ~openwow::game::kMoveFlagSplineElevation;
    (void)TryResetMovementState();
  }

  flags = runtime_flags_;

  if ((flags & openwow::game::kMoveFlagFalling) != 0u) {
    constexpr std::uint32_t kDeferClearMask =
        openwow::game::kMoveFlagPendingStop |
        openwow::game::kMoveFlagPendingForward |

        openwow::game::kMoveFlagPendingBackward;

    runtime_flags_ = (flags & ~kDeferClearMask) |
                     openwow::game::kMoveFlagPendingStop;
    return 0;
  }

  StopForwardBackwardAndRecalculate(true);
  return 1;
}

void CMovementData::StopForwardBackwardAndRecalculate(bool check_parent_init) {
  constexpr std::uint32_t kStopClearMask =
      openwow::game::kMoveFlagForward |
      openwow::game::kMoveFlagBackward |
      openwow::game::kMoveFlagPendingStop;
  runtime_flags_ &= ~kStopClearMask;
  ApplyDirectionStateTransition(SpeedRefreshTiming::kBeforeDirection);

  if (has_parent_movement_ &&
      (parent_movement_flags_ & kParentAllowStopFlag) == 0u) {
    parent_movement_flags_ |= kParentAllowStopFlag;
    if (check_parent_init &&
        (parent_movement_flags_ & kParentFlyingSplineFlag) != 0u) {
      (void)TryResetMovementState();
    }
  }
}

void CMovementData::ProcessPendingMovementStops() {
  UpdateDirectionConditional();

  constexpr std::uint32_t kPendingClearMask =
      openwow::game::kMoveFlagPendingStop |
      openwow::game::kMoveFlagPendingStrafeStop |
      openwow::game::kMoveFlagPendingForward |
      openwow::game::kMoveFlagPendingBackward |
      openwow::game::kMoveFlagPendingStrafeLeft |
      openwow::game::kMoveFlagPendingStrafeRight |
      openwow::game::kMoveFlagAscending |
      openwow::game::kMoveFlagDescending |
      openwow::game::kMoveFlagSplineElevation;
  runtime_flags_ &= ~kPendingClearMask;

  if (ProcessPendingTurnStop()) {
    if (dispatch_stop_opcode_callback_) {
      dispatch_stop_opcode_callback_(*this, 190u);
    }
  }

  if (ProcessPendingPitchStop()) {
    if (dispatch_stop_opcode_callback_) {
      dispatch_stop_opcode_callback_(*this, 193u);
    }
  }

  constexpr std::uint32_t kActiveStrafeFlags =
      openwow::game::kMoveFlagStrafeLeft |
      openwow::game::kMoveFlagStrafeRight;
  if ((runtime_flags_ & kActiveStrafeFlags) != 0u) {
    ClearStrafeAndRecalculate();

    if (dispatch_stop_opcode_callback_) {
      dispatch_stop_opcode_callback_(*this, 186u);
    }
  }

  StopForwardBackwardAndRecalculate(false);

}

bool CMovementData::EnableFlyMode() {
  const std::uint32_t flags = runtime_flags_;

  if ((flags & openwow::game::kMoveFlagCanFly) == 0u ||
      (flags & openwow::game::kMoveFlagPendingRoot) != 0u) {
    return false;
  }

  constexpr std::uint32_t kEnableFlyFlagsClear =
      openwow::game::kMoveFlagSwimming | openwow::game::kMoveFlagFalling |
      openwow::game::kMoveFlagFallingFar |
      openwow::game::kMoveFlagSplineElevation |
      openwow::game::kMoveFlagFlying;
  runtime_flags_ = (runtime_flags_ & ~kEnableFlyFlagsClear) |
                   openwow::game::kMoveFlagFlying;

  ApplyDirectionStateTransition(SpeedRefreshTiming::kAfterDirection);

  return true;
}

void CMovementData::ApplyStopSwimState() {
  runtime_flags_ &=
      (runtime_flags2_ & kFlags2AllowPitchBit) != 0u ? 0xFF1FFFFFu
                                                     : 0xFF1FFF3Fu;
  if ((runtime_flags2_ & kFlags2AllowPitchBit) == 0u) {
    runtime_pitch_ = 0.0f;
  }

  (void)TryInitRemoteMovement();
  if ((runtime_flags_ & openwow::game::kMoveFlagFalling) == 0u) {
    current_speed_ = CalculateCurrentSpeed(false);
  }
  SnapshotStateForDirectionRecompute();
}

void CMovementData::DisableFlyMode() {
  runtime_flags_ &= 0xFD3FFF3Fu;
  runtime_pitch_ = 0.0f;
  (void)TryInitRemoteMovement();
  if ((runtime_flags_ & openwow::game::kMoveFlagFalling) == 0u) {
    current_speed_ = CalculateCurrentSpeed(false);
  }
  SnapshotStateForDirectionRecompute();
}

void CMovementData::SetFallingSlowState(const bool enable) {
  if (enable) {
    runtime_flags_ |= openwow::game::kMoveFlagFallingSlow;
  } else {
    runtime_flags_ &= ~openwow::game::kMoveFlagFallingSlow;
  }
}

void CMovementData::SetHoverStateAndRefresh(
    const bool enable, const bool allow_fall_transition) {
  if (!enable && allow_fall_transition) {
    (void)TryInitRemoteMovement();
    if ((runtime_flags_ & openwow::game::kMoveFlagFalling) != 0u &&
        (runtime_flags_ & openwow::game::kMoveFlagFallingFar) == 0u) {
      runtime_flags_ |= openwow::game::kMoveFlagFallingFar;
    }
  }
  SetHoverState(enable);
}

bool CMovementData::TryStartJump(const bool allow_hover) {
  const std::uint32_t flags = runtime_flags_;
  if (!allow_hover && (flags & openwow::game::kMoveFlagHover) != 0u &&
      (flags & openwow::game::kMoveFlagSwimming) == 0u) {
    return false;
  }

  const bool non_exempt_parent =
      has_parent_movement_ &&
      (parent_movement_flags_ & kParentAllowStopFlag) == 0u;
  const bool falling_parent =
      non_exempt_parent &&
      (parent_movement_flags_ & kParentFallingSplineFlag) != 0u;
  if (non_exempt_parent &&
      (parent_movement_flags_ & kParentFlyingSplineFlag) != 0u) {
    return false;
  }

  constexpr std::uint32_t kJumpBlockingFlags =
      openwow::game::kMoveFlagFlying |
      openwow::game::kMoveFlagFalling |
      openwow::game::kMoveFlagRoot;
  if ((flags & kJumpBlockingFlags) != 0u) {
    return false;
  }

  if ((runtime_flags2_ & kFlags2SuppressBit) == 0u && !falling_parent &&
      (flags & openwow::game::kMoveFlagDisableGravity) != 0u) {
    return false;
  }
  if ((runtime_flags2_ & openwow::game::kMoveFlag2NoJumping) != 0u) {
    return false;
  }

  if ((flags & kInitBlockedByRootOrTransport) == 0u &&
      (!has_parent_movement_ ||
       (parent_movement_flags_ & kInitBlockedByRootOrTransport) == 0u)) {
    SnapshotStateForDirectionRecompute();
    runtime_flags_ = (flags & ~kInitResetClearMask) |
                     openwow::game::kMoveFlagFalling;
    if ((runtime_flags2_ & kFlags2AllowPitchBit) == 0u) {
      runtime_flags_ &= ~(openwow::game::kMoveFlagPitchUp |
                           openwow::game::kMoveFlagPitchDown);
    }

    runtime_fall_time_ = 0u;
    runtime_fall_start_z_ = transform_position_[2];
    runtime_jump_z_speed_ =
        (flags & openwow::game::kMoveFlagSwimming) != 0u
            ? openwow::game::PhysicsConstants::SwimJumpInitialVelocity
            : openwow::game::PhysicsConstants::JumpInitialVelocity;

    runtime_jump_cos_angle_ = facing_cos_;
    runtime_jump_sin_angle_ = facing_sin_;
    runtime_jump_xy_speed_ = current_speed_;
  }

  return true;
}

void CMovementData::ApplyKnockBackState(
    const openwow::game::ObjectManager &objects, float direction_x,
    float direction_y, const float horizontal_speed,
    const float vertical_speed) {
  if ((runtime_flags_ & openwow::game::kMoveFlagSwimming) != 0u) {
    ApplyStopSwimState();
  } else if ((runtime_flags_ & openwow::game::kMoveFlagFlying) != 0u) {
    DisableFlyMode();
  }

  if ((runtime_flags_ & openwow::game::kMoveFlagRoot) != 0u) {
    runtime_flags_ =
        (runtime_flags_ & ~openwow::game::kMoveFlagRoot) |
        openwow::game::kMoveFlagPendingRoot;
  }
  runtime_flags_ &= ~openwow::game::kMoveFlagCanFly;

  if ((runtime_flags_ & kInitBlockedByRootOrTransport) == 0u &&
      (!has_parent_movement_ ||
       (parent_movement_flags_ & kInitBlockedByRootOrTransport) == 0u)) {
    SnapshotStateForDirectionRecompute();
    runtime_flags_ = (runtime_flags_ & ~0x06E00000u) |
                     openwow::game::kMoveFlagFalling;
    if ((runtime_flags2_ & kFlags2AllowPitchBit) == 0u) {
      runtime_flags_ &= ~(openwow::game::kMoveFlagPitchUp |
                           openwow::game::kMoveFlagPitchDown);
    }
    runtime_fall_time_ = 0u;
    runtime_fall_start_z_ = transform_position_[2];
    runtime_jump_z_speed_ = vertical_speed;
  }

  if (transport_guid_ != 0u) {
    float transport_transform[16]{};
    if (Movement_GetObjectTransform(objects, transport_guid_,
                                    transport_transform) != 0) {
      float inverse[16]{};
      openwow::math::row_major_mat4x4::BuildInverseRigidTransform4x4(
          inverse, transport_transform);
      const float world_direction[3]{direction_x, direction_y, 0.0f};
      float local_direction[3]{};
      openwow::math::row_major_mat4x4::TransformVec3ByUpper3x3(
          local_direction, world_direction, inverse);
      direction_x = local_direction[0];
      direction_y = local_direction[1];
    }
  }

  const float length_squared =
      direction_x * direction_x + direction_y * direction_y;
  if (length_squared > 2.3841858e-7f) {
    const float inverse_length = 1.0f / std::sqrt(length_squared);
    direction_x *= inverse_length;
    direction_y *= inverse_length;
  }
  runtime_jump_cos_angle_ = direction_x;
  runtime_jump_sin_angle_ = direction_y;
  runtime_jump_xy_speed_ = horizontal_speed;
  current_speed_ = horizontal_speed;
  runtime_flags_ =
      (runtime_flags_ & ~openwow::game::kMoveFlagBackward) |
      openwow::game::kMoveFlagForward |
      openwow::game::kMoveFlagPendingStop;
}

bool CMovementData::SetGravityEnabledState(bool gravity_enabled) {
  const std::uint32_t old_flags = runtime_flags_;

  const std::uint32_t new_flags = gravity_enabled
      ? (old_flags & ~openwow::game::kMoveFlagDisableGravity)
      : (old_flags | openwow::game::kMoveFlagDisableGravity);

  runtime_flags_ = new_flags;

  if (old_flags == new_flags) {
    return false;
  }

  SnapshotStateForDirectionRecompute();

  return true;
}

bool CMovementData::SetGravityEnabledAndRefresh(const bool gravity_enabled) {
  if (!SetGravityEnabledState(gravity_enabled)) {
    return false;
  }

  RefreshGravityState();
  return true;
}

void CMovementData::RefreshGravityState() {
  const bool non_exempt_parent =
      has_parent_movement_ &&
      (parent_movement_flags_ & kParentAllowStopFlag) == 0u;
  const bool falling_parent =
      non_exempt_parent &&
      (parent_movement_flags_ & kParentFallingSplineFlag) != 0u;
  const bool standard_reinitialization =
      (!non_exempt_parent ||
       (parent_movement_flags_ & kParentFlyingSplineFlag) == 0u) &&
      (runtime_flags2_ & kFlags2SuppressBit) == 0u &&
      (runtime_flags_ & openwow::game::kMoveFlagDisableGravity) == 0u;

  if (falling_parent || standard_reinitialization) {
    (void)TryInitRemoteMovement();
    return;
  }

  const std::uint32_t old_flags = runtime_flags_;
  const bool was_falling =
      (old_flags & openwow::game::kMoveFlagFalling) != 0u;
  const bool had_pending_root =
      (old_flags & openwow::game::kMoveFlagPendingRoot) != 0u;

  if (was_falling || had_pending_root) {
    if (was_falling) {
      runtime_flags_ &= ~(openwow::game::kMoveFlagFalling |
                          openwow::game::kMoveFlagFallingFar);
    }
    if (had_pending_root) {
      runtime_flags_ =
          (runtime_flags_ & 0xFF203F00u) | openwow::game::kMoveFlagRoot;
    }
    SnapshotStateForDirectionRecompute();
    if ((runtime_flags_ & openwow::game::kMoveFlagFalling) == 0u) {
      current_speed_ = CalculateCurrentSpeed(false);
    }
  }

  ProcessPendingMovementStops();
  if (was_falling &&
      (runtime_flags2_ & openwow::game::kMoveFlag2FullSpeedPitching) == 0u) {
    HandleRemotePoseSnapshot();
  }
}

void CMovementData::SetRootState(bool rooted) {

  if (rooted) {
    runtime_flags_ &= ~openwow::game::kMoveFlagWalking;
  } else {
    runtime_flags_ |= openwow::game::kMoveFlagWalking;
  }

  ApplyDirectionStateTransition(SpeedRefreshTiming::kAfterDirection);
}

void CMovementData::SetHoverState(bool enable) {
  if (enable) {
    runtime_flags_ = (runtime_flags_ &
                      ~(openwow::game::kMoveFlagHover |
                        openwow::game::kMoveFlagSplineElevation)) |
                     openwow::game::kMoveFlagHover;
  } else {
    runtime_flags_ &= ~openwow::game::kMoveFlagHover;
  }
}

void CMovementData::ForceApplyRoot() {
  if ((runtime_flags_ & openwow::game::kMoveFlagRoot) != 0u) {
    return;
  }

  if ((runtime_flags_ & openwow::game::kMoveFlagFalling) != 0u &&
      !has_parent_movement_) {
    runtime_flags_ |= openwow::game::kMoveFlagPendingRoot;
    return;
  }

  runtime_flags_ |= openwow::game::kMoveFlagRoot;

  UpdateDirectionConditional();

  constexpr std::uint32_t kForceRootKeepMask = 0xFF203F00u;
  runtime_flags_ &= kForceRootKeepMask;

  if ((runtime_flags_ & openwow::game::kMoveFlagFalling) == 0u) {
    current_speed_ = CalculateCurrentSpeed(false);
  }
}

void CMovementData::ForceRemoveRoot(bool try_reinit) {
  runtime_flags_ &= ~(openwow::game::kMoveFlagRoot |
                       openwow::game::kMoveFlagPendingRoot);

  if (try_reinit) {
    (void)TryResetMovementState();
  }
}

}

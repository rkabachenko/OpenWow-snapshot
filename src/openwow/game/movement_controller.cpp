#include "openwow/game/movement_controller.h"

#include "openwow/net/wotlk/movement.h"

#include <cstddef>

namespace openwow::game {

MovementController::MovementController() {
  const auto base = net::wotlk::movement_speeds::BaseSpeedTable();
  for (std::size_t index = 0; index < speeds_.size(); ++index) {
    speeds_[index] = base[index];
  }
}

void MovementController::SetPosition(const float x, const float y,
                                     const float z,
                                     const float orientation) {
  info_.x = x;
  info_.y = y;
  info_.z = z;
  info_.orientation = orientation;
}

void MovementController::SetSpeed(const SpeedType type, const float value) {
  const auto index = static_cast<std::size_t>(type);
  if (index < speeds_.size()) {
    speeds_[index] = value;
  }
}

float MovementController::GetSpeed(const SpeedType type) const {
  const auto index = static_cast<std::size_t>(type);
  return index < speeds_.size() ? speeds_[index] : 0.0f;
}

bool MovementController::IsMoving() const {
  constexpr std::uint32_t kMovingMask =
      kMoveFlagForward | kMoveFlagBackward | kMoveFlagStrafeLeft |
      kMoveFlagStrafeRight | kMoveFlagAscending | kMoveFlagDescending;
  return (info_.flags & kMovingMask) != 0u;
}

bool MovementController::IsTurning() const {
  return (info_.flags & (kMoveFlagTurnLeft | kMoveFlagTurnRight)) != 0u;
}

bool MovementController::IsFlying() const {
  return info_.HasFlag(kMoveFlagFlying);
}

bool MovementController::IsSwimming() const {
  return info_.HasFlag(kMoveFlagSwimming);
}

bool MovementController::IsFalling() const {
  return info_.HasFlag(kMoveFlagFalling);
}

bool MovementController::IsWalking() const {
  return info_.HasFlag(kMoveFlagWalking);
}

bool MovementController::IsOnGround() const {
  return !IsFalling() && !IsFlying() && !IsSwimming();
}

void MovementController::ResetTransientState() {
  info_ = {};
}

void MovementController::ApplyTeleport(const float x, const float y,
                                       const float z,
                                       const float orientation) {
  info_.x = x;
  info_.y = y;
  info_.z = z;
  info_.orientation = orientation;
  info_.flags &= ~kMoveFlagFalling;
  info_.fall_time = 0u;
  info_.jump = {};
}

void MovementController::ApplyAuthoritativeMovementInfo(
    const MovementInfo& info) {
  info_ = info;
}

}

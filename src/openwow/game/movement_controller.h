#pragma once

#include "openwow/game/movement_info.h"
#include "openwow/game/object_types.h"

#include <array>
#include <cstdint>

namespace openwow::game {

class MovementController {
 public:
  MovementController();

  void SetPosition(float x, float y, float z, float orientation);
  void SetSpeed(SpeedType type, float value);
  [[nodiscard]] float GetSpeed(SpeedType type) const;

  [[nodiscard]] const MovementInfo& GetMovementInfo() const { return info_; }
  [[nodiscard]] float x() const { return info_.x; }
  [[nodiscard]] float y() const { return info_.y; }
  [[nodiscard]] float z() const { return info_.z; }
  [[nodiscard]] float orientation() const { return info_.orientation; }
  [[nodiscard]] bool IsMoving() const;
  [[nodiscard]] bool IsTurning() const;
  [[nodiscard]] bool IsFlying() const;
  [[nodiscard]] bool IsSwimming() const;
  [[nodiscard]] bool IsFalling() const;
  [[nodiscard]] bool IsWalking() const;
  [[nodiscard]] bool IsOnGround() const;

  void ResetTransientState();
  void ApplyTeleport(float x, float y, float z, float orientation);
  void ApplyAuthoritativeMovementInfo(const MovementInfo& info);

 private:
  MovementInfo info_;
  std::array<float, kMaxSpeeds> speeds_{};
};

}

#include "openwow/game/movement/retail_fall_kinematics.h"

#include "openwow/game/movement_info.h"

namespace openwow::game {

float IntegrateFallDistance(const float time_sec, const bool has_safe_fall,
                            const float initial_speed) {

  const double terminal_velocity =
      has_safe_fall
          ? static_cast<double>(PhysicsConstants::SafeFallTerminalVelocity)
          : static_cast<double>(PhysicsConstants::TerminalVelocity);
  double initial_velocity = static_cast<double>(initial_speed);
  if (initial_velocity > terminal_velocity) {
    initial_velocity = terminal_velocity;
  }

  const double time = static_cast<double>(time_sec);
  const double gravity = static_cast<double>(PhysicsConstants::Gravity);
  const double half_gravity =
      static_cast<double>(PhysicsConstants::HalfGravity);
  const double inverse_gravity =
      static_cast<double>(PhysicsConstants::InverseGravity);

  if (gravity * time + initial_velocity > terminal_velocity) {
    const double acceleration_time =
        (terminal_velocity - initial_velocity) * inverse_gravity;
    const double acceleration_distance =
        (initial_velocity + half_gravity * acceleration_time) *
        acceleration_time;
    const double terminal_distance =
        terminal_velocity * (time - acceleration_time);
    return static_cast<float>(acceleration_distance + terminal_distance);
  }

  return static_cast<float>(
      time * (initial_velocity + half_gravity * time));
}

float PredictFallZ(const float base_z, const bool has_safe_fall,
                   const float fall_speed, const std::uint32_t time_ms) {
  return base_z + IntegrateFallDistance(
                      static_cast<float>(time_ms) * 0.001f, has_safe_fall,
                      fall_speed);
}

float ComputeCollisionFallDisplacement(
    const std::uint32_t movement_flags, const float fall_speed,
    const float base_z, const float fall_start_z,
    const std::uint32_t time_ms) {
  const bool has_safe_fall =
      (movement_flags & kMoveFlagFallingSlow) != 0u;
  const float distance = IntegrateFallDistance(
      static_cast<float>(time_ms) * 0.001f, has_safe_fall, fall_speed);
  return (movement_flags & kMoveFlagFalling) != 0u
             ? distance + base_z - fall_start_z
             : distance;
}

float ComputeOrdinaryFallDistance(const float fall_start_z,
                                  const float current_z,
                                  const float current_fall_speed) {
  double result =
      static_cast<double>(fall_start_z) - static_cast<double>(current_z);
  constexpr float kDownwardEpsilon = -0.00000023841858f;
  if (current_fall_speed < kDownwardEpsilon) {
    const double velocity = static_cast<double>(current_fall_speed);
    result += velocity * velocity *
              static_cast<double>(PhysicsConstants::InverseGravity) * 0.5;
  }
  return result < 0.0 ? 0.0f : static_cast<float>(result);
}

}

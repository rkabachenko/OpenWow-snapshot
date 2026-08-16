#pragma once

#include <cstdint>

namespace openwow::game {

namespace PhysicsConstants {

inline constexpr float Gravity = 19.291103f;
inline constexpr float HalfGravity = Gravity * 0.5f;
inline constexpr float InverseGravity = 1.0f / Gravity;

  inline constexpr float TerminalVelocity = 60.148003f;
  inline constexpr float SafeFallTerminalVelocity = 7.0f;
  inline constexpr float JumpInitialVelocity = -7.9555473f;
  inline constexpr float SwimJumpInitialVelocity = -9.096748f;
}

[[nodiscard]] float IntegrateFallDistance(float time_sec, bool has_safe_fall,
                                          float initial_speed);
[[nodiscard]] float PredictFallZ(float base_z, bool has_safe_fall,
                                 float fall_speed, std::uint32_t time_ms);
[[nodiscard]] float ComputeCollisionFallDisplacement(
    std::uint32_t movement_flags, float fall_speed, float base_z,
    float fall_start_z, std::uint32_t time_ms);
[[nodiscard]] inline float ComputeCollisionFallZ(
    const std::uint32_t movement_flags, const float fall_speed,
    const float position_z, const float fall_start_z,
    const std::uint32_t time_ms) {
  return ComputeCollisionFallDisplacement(
      movement_flags, fall_speed, position_z, fall_start_z, time_ms);
}
[[nodiscard]] float ComputeOrdinaryFallDistance(float fall_start_z,
                                                float current_z,
                                                float current_fall_speed);

}

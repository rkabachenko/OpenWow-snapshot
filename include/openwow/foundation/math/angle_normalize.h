
#pragma once

#include <cmath>

namespace openwow::math {

inline constexpr float kAnglePi = 3.1415927f;
inline constexpr float kAngleTwoPi = 6.2831855f;

[[nodiscard]] inline float NormalizeSignedAngle(float angle) noexcept {
  float result = std::fmod(angle, kAngleTwoPi);
  if (result < -kAnglePi) {
    result += kAngleTwoPi;
  } else if (result > kAnglePi) {
    result -= kAngleTwoPi;
  }
  return result;
}

[[nodiscard]] inline float NormalizePositiveAngle(float angle) noexcept {
  float result = std::fmod(angle, kAngleTwoPi);
  if (result < 0.0f) {
    result += kAngleTwoPi;
  }
  return result;
}

}

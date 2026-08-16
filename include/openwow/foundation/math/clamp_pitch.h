
#pragma once

namespace openwow::math {

inline constexpr float kPitchHalfPi = 1.5707964f;

[[nodiscard]] inline float ClampPitch(float pitch) noexcept {
  if (pitch > kPitchHalfPi) {
    return kPitchHalfPi;
  }
  if (pitch < -kPitchHalfPi) {
    return -kPitchHalfPi;
  }
  return pitch;
}

}

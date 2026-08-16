
#pragma once

#include <cmath>

namespace openwow::math::vec3 {

inline constexpr float kNearZeroEpsilon = 1e-8f;

inline void SnapToZero(float* xyz) {
  if (0.0f != xyz[0] && std::fabs(xyz[0]) < kNearZeroEpsilon) {
    xyz[0] = 0.0f;
  }
  if (0.0f != xyz[1] && std::fabs(xyz[1]) < kNearZeroEpsilon) {
    xyz[1] = 0.0f;
  }
  if (0.0f != xyz[2] && std::fabs(xyz[2]) < kNearZeroEpsilon) {
    xyz[2] = 0.0f;
  }
}

}

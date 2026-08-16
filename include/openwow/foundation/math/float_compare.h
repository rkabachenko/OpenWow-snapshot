
#pragma once

#include <cmath>

namespace openwow::math::float_compare {

inline constexpr float kClientFloatEpsilon = 0.00000023841858f;
inline constexpr float kClientFloatWideEpsilon = 0.00000095367432f;

inline bool WithinTolerance(float lhs, float rhs, float tolerance) {
  return std::fabs(lhs - rhs) < tolerance;
}

inline bool OutsideTolerance(float lhs, float rhs, float tolerance) {
  return std::fabs(lhs - rhs) >= tolerance;
}

inline bool WithinClientEpsilon(float lhs, float rhs) {
  return WithinTolerance(lhs, rhs, kClientFloatEpsilon);
}

inline bool OutsideClientEpsilon(float lhs, float rhs) {
  return OutsideTolerance(lhs, rhs, kClientFloatEpsilon);
}

inline bool WithinWideClientEpsilon(float lhs, float rhs) {
  return WithinTolerance(lhs, rhs, kClientFloatWideEpsilon);
}

}


#pragma once

#include "openwow/foundation/math/float_compare.h"

#include <cmath>

namespace openwow::math {

inline constexpr float kLegacyFacingPi = 3.1415927f;

struct PlanarPoint {
  float x = 0.0f;
  float y = 0.0f;
};

[[nodiscard]] inline float ComputeRetailPlanarFacingAngle(const PlanarPoint from,
                                                          const PlanarPoint to) noexcept {
  const double dx = static_cast<double>(to.x) - static_cast<double>(from.x);
  const double dy = static_cast<double>(to.y) - static_cast<double>(from.y);
  if (std::fabs(dx) >= float_compare::kClientFloatEpsilon) {
    if (std::fabs(dy) >= float_compare::kClientFloatEpsilon) {
      return static_cast<float>(std::atan2(dy, dx));
    }
    return from.x <= static_cast<double>(to.x) ? 0.0f : kLegacyFacingPi;
  }
  return dy >= 0.0 ? 0.5f * kLegacyFacingPi : 1.5f * kLegacyFacingPi;
}

}

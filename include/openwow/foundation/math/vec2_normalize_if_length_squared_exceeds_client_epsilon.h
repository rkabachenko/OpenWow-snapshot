
#pragma once

#include <cmath>

#include "openwow/foundation/math/float_compare.h"

namespace openwow::math::vec2 {

inline void NormalizeInPlaceIfLengthSquaredExceedsClientEpsilon(
    float* xy) noexcept {
  const float length_squared = xy[0] * xy[0] + xy[1] * xy[1];
  if (length_squared <= float_compare::kClientFloatEpsilon) {
    return;
  }

  const float inverse_length = 1.0f / std::sqrt(length_squared);
  xy[0] *= inverse_length;
  xy[1] *= inverse_length;
}

}

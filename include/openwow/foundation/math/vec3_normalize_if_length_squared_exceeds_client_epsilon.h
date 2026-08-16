
#pragma once

#include <cmath>

#include "openwow/foundation/math/float_compare.h"

namespace openwow::math::vec3 {

inline void NormalizeInPlaceIfLengthSquaredExceedsClientEpsilon(float* xyz) {
  const float length_squared =
      xyz[0] * xyz[0] + xyz[1] * xyz[1] + xyz[2] * xyz[2];
  if (length_squared <= float_compare::kClientFloatEpsilon) {
    return;
  }

  const float inverse_length = 1.0f / std::sqrt(length_squared);
  xyz[0] *= inverse_length;
  xyz[1] *= inverse_length;
  xyz[2] *= inverse_length;
}

}

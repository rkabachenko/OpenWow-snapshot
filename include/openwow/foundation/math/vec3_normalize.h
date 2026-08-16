
#pragma once

#include <cmath>

#include "openwow/foundation/math/vec3_divide.h"

namespace openwow::math::vec3 {

inline void NormalizeInPlace(float* xyz) {
  const float length =
      std::sqrt(xyz[0] * xyz[0] + xyz[1] * xyz[1] + xyz[2] * xyz[2]);
  DivideInPlace(xyz, length);
}

}

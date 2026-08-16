
#pragma once

namespace openwow::math::vec4 {

inline float* ComponentwiseDivide(float* dest, const float* lhs,
                                  const float* rhs) {
  dest[0] = lhs[0] / rhs[0];
  dest[1] = lhs[1] / rhs[1];
  dest[2] = lhs[2] / rhs[2];
  dest[3] = lhs[3] / rhs[3];
  return dest;
}

}

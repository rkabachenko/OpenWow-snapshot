
#pragma once

namespace openwow::math::vec3 {

inline float* DivideInPlace(float* xyz, const float scalar) {
  const float inverse_scalar = 1.0f / scalar;
  xyz[0] *= inverse_scalar;
  xyz[1] *= inverse_scalar;
  xyz[2] *= inverse_scalar;
  return xyz;
}

}


#pragma once

namespace openwow::math::vec3 {

inline float* ScaleInPlace(float* xyz, const float scalar) {
  xyz[0] *= scalar;
  xyz[1] *= scalar;
  xyz[2] *= scalar;
  return xyz;
}

}

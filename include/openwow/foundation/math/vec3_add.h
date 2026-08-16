
#pragma once

namespace openwow::math::vec3 {

inline float* AddInPlace(float* xyz, const float* rhs_xyz) {
  xyz[0] += rhs_xyz[0];
  xyz[1] += rhs_xyz[1];
  xyz[2] += rhs_xyz[2];
  return xyz;
}

}

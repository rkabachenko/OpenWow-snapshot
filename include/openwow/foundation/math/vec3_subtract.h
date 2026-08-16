
#pragma once

namespace openwow::math::vec3 {

inline float* Subtract(float* out_xyz, const float* lhs_xyz,
                       const float* rhs_xyz) {
  out_xyz[0] = lhs_xyz[0] - rhs_xyz[0];
  out_xyz[1] = lhs_xyz[1] - rhs_xyz[1];
  out_xyz[2] = lhs_xyz[2] - rhs_xyz[2];
  return out_xyz;
}

inline float* SubtractInPlace(float* xyz, const float* rhs_xyz) {
  xyz[0] -= rhs_xyz[0];
  xyz[1] -= rhs_xyz[1];
  xyz[2] -= rhs_xyz[2];
  return xyz;
}

}

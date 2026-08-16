
#pragma once

namespace openwow::math::vec3 {

inline float* Cross(float* out_xyz, const float* lhs_xyz,
                    const float* rhs_xyz) {
  out_xyz[0] = lhs_xyz[1] * rhs_xyz[2] - lhs_xyz[2] * rhs_xyz[1];
  out_xyz[1] = lhs_xyz[2] * rhs_xyz[0] - lhs_xyz[0] * rhs_xyz[2];
  out_xyz[2] = lhs_xyz[0] * rhs_xyz[1] - lhs_xyz[1] * rhs_xyz[0];
  return out_xyz;
}

}

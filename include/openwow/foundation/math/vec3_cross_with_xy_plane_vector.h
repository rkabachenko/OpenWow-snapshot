
#pragma once

namespace openwow::math::vec3 {

inline float* CrossWithXYPlaneVector(float* out_xyz,
                                     const float* lhs_xyz,
                                     const float* rhs_xy) {
  out_xyz[0] = -(rhs_xy[1] * lhs_xyz[2]);
  out_xyz[1] = rhs_xy[0] * lhs_xyz[2];
  out_xyz[2] = rhs_xy[1] * lhs_xyz[0] - lhs_xyz[1] * rhs_xy[0];
  return out_xyz;
}

}

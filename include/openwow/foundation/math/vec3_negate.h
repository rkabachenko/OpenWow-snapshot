
#pragma once

namespace openwow::math::vec3 {

inline float* CopyNegated(float* out_xyz, const float* src_xyz) {
  out_xyz[0] = -src_xyz[0];
  out_xyz[1] = -src_xyz[1];
  out_xyz[2] = -src_xyz[2];
  return out_xyz;
}

}

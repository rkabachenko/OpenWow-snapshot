
#pragma once

namespace openwow::math::vec3 {

inline bool AnyComponentDiffers(const float* lhs_xyz, const float* rhs_xyz) {
  return rhs_xyz[0] != lhs_xyz[0] || rhs_xyz[1] != lhs_xyz[1] || rhs_xyz[2] != lhs_xyz[2];
}

inline bool AllComponentsEqual(const float* lhs_xyz, const float* rhs_xyz) {
  return !AnyComponentDiffers(lhs_xyz, rhs_xyz);
}

}

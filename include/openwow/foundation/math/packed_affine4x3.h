
#pragma once

#include "openwow/foundation/math/packed_mat3x3.h"

namespace openwow::math::packed_affine4x3 {

inline float* Multiply(float* out_affine4x3,
                       const float* left_affine4x3,
                       const float* right_affine4x3) {

  out_affine4x3[0] = left_affine4x3[1] * right_affine4x3[3]
                    + right_affine4x3[6] * left_affine4x3[2]
                    + left_affine4x3[0] * right_affine4x3[0];
  out_affine4x3[1] = right_affine4x3[7] * left_affine4x3[2]
                    + left_affine4x3[0] * right_affine4x3[1]
                    + left_affine4x3[1] * right_affine4x3[4];
  out_affine4x3[2] = left_affine4x3[1] * right_affine4x3[5]
                    + right_affine4x3[2] * left_affine4x3[0]
                    + right_affine4x3[8] * left_affine4x3[2];
  out_affine4x3[3] = left_affine4x3[3] * right_affine4x3[0]
                    + right_affine4x3[6] * left_affine4x3[5]
                    + right_affine4x3[3] * left_affine4x3[4];
  out_affine4x3[4] = left_affine4x3[3] * right_affine4x3[1]
                    + right_affine4x3[7] * left_affine4x3[5]
                    + right_affine4x3[4] * left_affine4x3[4];
  out_affine4x3[5] = left_affine4x3[5] * right_affine4x3[8]
                    + left_affine4x3[4] * right_affine4x3[5]
                    + left_affine4x3[3] * right_affine4x3[2];
  out_affine4x3[6] = right_affine4x3[6] * left_affine4x3[8]
                    + left_affine4x3[7] * right_affine4x3[3]
                    + left_affine4x3[6] * right_affine4x3[0];
  out_affine4x3[7] = left_affine4x3[6] * right_affine4x3[1]
                    + right_affine4x3[7] * left_affine4x3[8]
                    + right_affine4x3[4] * left_affine4x3[7];
  out_affine4x3[8] = left_affine4x3[8] * right_affine4x3[8]
                    + left_affine4x3[7] * right_affine4x3[5]
                    + left_affine4x3[6] * right_affine4x3[2];

  out_affine4x3[9]  = left_affine4x3[10] * right_affine4x3[3]
                     + left_affine4x3[11] * right_affine4x3[6]
                     + right_affine4x3[0] * left_affine4x3[9]
                     + right_affine4x3[9];
  out_affine4x3[10] = left_affine4x3[11] * right_affine4x3[7]
                     + left_affine4x3[10] * right_affine4x3[4]
                     + left_affine4x3[9] * right_affine4x3[1]
                     + right_affine4x3[10];
  out_affine4x3[11] = left_affine4x3[11] * right_affine4x3[8]
                     + left_affine4x3[10] * right_affine4x3[5]
                     + right_affine4x3[2] * left_affine4x3[9]
                     + right_affine4x3[11];
  return out_affine4x3;
}

}

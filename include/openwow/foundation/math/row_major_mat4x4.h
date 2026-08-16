
#pragma once

namespace openwow::math::row_major_mat4x4 {

inline void SetIdentity(float* out_matrix4x4) noexcept {
  out_matrix4x4[15] = 1.0f;
  out_matrix4x4[14] = 0.0f;
  out_matrix4x4[13] = 0.0f;
  out_matrix4x4[12] = 0.0f;
  out_matrix4x4[11] = 0.0f;
  out_matrix4x4[10] = 1.0f;
  out_matrix4x4[9] = 0.0f;
  out_matrix4x4[8] = 0.0f;
  out_matrix4x4[7] = 0.0f;
  out_matrix4x4[6] = 0.0f;
  out_matrix4x4[5] = 1.0f;
  out_matrix4x4[4] = 0.0f;
  out_matrix4x4[3] = 0.0f;
  out_matrix4x4[2] = 0.0f;
  out_matrix4x4[1] = 0.0f;
  out_matrix4x4[0] = 1.0f;
}

inline float* BuildRowMajorAffine4x4FromPackedMat3x3(
    float* out_matrix4x4, const float* matrix3x3) noexcept {
  out_matrix4x4[0] = matrix3x3[0];
  out_matrix4x4[1] = matrix3x3[1];
  out_matrix4x4[2] = matrix3x3[2];
  out_matrix4x4[3] = 0.0f;
  out_matrix4x4[4] = matrix3x3[3];
  out_matrix4x4[5] = matrix3x3[4];
  out_matrix4x4[6] = matrix3x3[5];
  out_matrix4x4[7] = 0.0f;
  out_matrix4x4[8] = matrix3x3[6];
  out_matrix4x4[9] = matrix3x3[7];
  out_matrix4x4[10] = matrix3x3[8];
  out_matrix4x4[11] = 0.0f;
  out_matrix4x4[12] = 0.0f;
  out_matrix4x4[13] = 0.0f;
  out_matrix4x4[14] = 0.0f;
  out_matrix4x4[15] = 1.0f;
  return out_matrix4x4;
}

inline float* CopyRowMajorBasis3x3FromAffine4x4(float* out_matrix3x3,
                                                const float* matrix4x4) noexcept {
  out_matrix3x3[0] = matrix4x4[0];
  out_matrix3x3[1] = matrix4x4[1];
  out_matrix3x3[2] = matrix4x4[2];
  out_matrix3x3[3] = matrix4x4[4];
  out_matrix3x3[4] = matrix4x4[5];
  out_matrix3x3[5] = matrix4x4[6];
  out_matrix3x3[6] = matrix4x4[8];
  out_matrix3x3[7] = matrix4x4[9];
  out_matrix3x3[8] = matrix4x4[10];
  return out_matrix3x3;
}

inline float* BuildRowMajorAffine4x4FromPackedAffine4x3(
    float* out_matrix4x4, const float* affine4x3) noexcept {
  out_matrix4x4[0] = affine4x3[0];
  out_matrix4x4[1] = affine4x3[1];
  out_matrix4x4[2] = affine4x3[2];
  out_matrix4x4[3] = 0.0f;
  out_matrix4x4[4] = affine4x3[3];
  out_matrix4x4[5] = affine4x3[4];
  out_matrix4x4[6] = affine4x3[5];
  out_matrix4x4[7] = 0.0f;
  out_matrix4x4[8] = affine4x3[6];
  out_matrix4x4[9] = affine4x3[7];
  out_matrix4x4[10] = affine4x3[8];
  out_matrix4x4[11] = 0.0f;
  out_matrix4x4[12] = affine4x3[9];
  out_matrix4x4[13] = affine4x3[10];
  out_matrix4x4[14] = affine4x3[11];
  out_matrix4x4[15] = 1.0f;
  return out_matrix4x4;
}

inline bool EqualExact(const float* lhs, const float* rhs) noexcept {
  return lhs[0] == rhs[0] && lhs[1] == rhs[1] && lhs[2] == rhs[2] &&
         lhs[3] == rhs[3] && lhs[4] == rhs[4] && lhs[5] == rhs[5] &&
         lhs[6] == rhs[6] && lhs[7] == rhs[7] && lhs[8] == rhs[8] &&
         lhs[9] == rhs[9] && lhs[10] == rhs[10] && lhs[11] == rhs[11] &&
         lhs[12] == rhs[12] && lhs[13] == rhs[13] &&
         lhs[14] == rhs[14] && lhs[15] == rhs[15];
}

inline bool NotEqualExact(const float* lhs, const float* rhs) noexcept {
  return !EqualExact(lhs, rhs);
}

inline float* TransformPointByRowMajorAffine4x4Unbuffered(
    float* out_xyz, const float* point_xyz, const float* matrix4x4) noexcept {
  out_xyz[0] = matrix4x4[0] * point_xyz[0] + matrix4x4[4] * point_xyz[1] +
               matrix4x4[8] * point_xyz[2] + matrix4x4[12];
  out_xyz[1] = matrix4x4[1] * point_xyz[0] + matrix4x4[5] * point_xyz[1] +
               matrix4x4[9] * point_xyz[2] + matrix4x4[13];
  out_xyz[2] = matrix4x4[2] * point_xyz[0] + matrix4x4[6] * point_xyz[1] +
               matrix4x4[10] * point_xyz[2] + matrix4x4[14];
  return out_xyz;
}

inline float* TransformPointByRowMajorAffine4x4LastColumn(
    float* out_xyz, const float* matrix4x4, const float* point_xyz) noexcept {
  const double input_x = point_xyz[0];
  const double input_y = point_xyz[1];
  const double input_z = point_xyz[2];
  const double output_y =
      matrix4x4[4] * input_x + matrix4x4[5] * input_y +
      matrix4x4[6] * input_z + matrix4x4[7];
  const double output_z =
      matrix4x4[8] * input_x + matrix4x4[9] * input_y +
      matrix4x4[10] * input_z + matrix4x4[11];

  out_xyz[0] = static_cast<float>(matrix4x4[0] * input_x +
                                  matrix4x4[1] * input_y +
                                  matrix4x4[2] * input_z + matrix4x4[3]);
  out_xyz[1] = static_cast<float>(output_y);
  out_xyz[2] = static_cast<float>(output_z);
  return out_xyz;
}

inline float* TransformPointByRowMajorAffine4x4(float* out_xyz,
                                                const float* point_xyz,
                                                const float* matrix4x4) noexcept {

  const float input_x = point_xyz[0];
  const float input_y = point_xyz[1];
  const float input_z = point_xyz[2];

  float output_z = input_x * matrix4x4[2];
  output_z += input_y * matrix4x4[6];
  output_z += input_z * matrix4x4[10];
  output_z += matrix4x4[14];

  float output_y = input_x * matrix4x4[1];
  output_y += input_y * matrix4x4[5];
  output_y += input_z * matrix4x4[9];
  output_y += matrix4x4[13];

  float output_x = input_x * matrix4x4[0];
  output_x += input_y * matrix4x4[4];
  output_x += input_z * matrix4x4[8];
  output_x += matrix4x4[12];

  out_xyz[2] = output_z;
  out_xyz[1] = output_y;
  out_xyz[0] = output_x;
  return out_xyz;
}

inline float* TransformVec3ByUpper3x3(float* out_xyz,
                                      const float* vector_xyz,
                                      const float* matrix4x4) noexcept {
  out_xyz[0] = matrix4x4[0] * vector_xyz[0] + matrix4x4[4] * vector_xyz[1] +
               matrix4x4[8] * vector_xyz[2];
  out_xyz[1] = matrix4x4[1] * vector_xyz[0] + matrix4x4[5] * vector_xyz[1] +
               matrix4x4[9] * vector_xyz[2];
  out_xyz[2] = matrix4x4[2] * vector_xyz[0] + matrix4x4[6] * vector_xyz[1] +
               matrix4x4[10] * vector_xyz[2];
  return out_xyz;
}

inline float* AccumulateAABBByPackedRowMajorBasis3x3(
    float* out_bounds_min_max, const float* bounds_min_max,
    const float* matrix3x3) noexcept {
  for (int axis = 0; axis < 3; ++axis) {
    const double x_from_min =
        static_cast<double>(bounds_min_max[0]) * matrix3x3[axis];
    const double x_from_max =
        static_cast<double>(bounds_min_max[3]) * matrix3x3[axis];
    if (!(x_from_max > x_from_min)) {
      out_bounds_min_max[axis] =
          static_cast<float>(x_from_max + out_bounds_min_max[axis]);
      out_bounds_min_max[axis + 3] =
          static_cast<float>(x_from_min + out_bounds_min_max[axis + 3]);
    } else {
      out_bounds_min_max[axis] =
          static_cast<float>(x_from_min + out_bounds_min_max[axis]);
      out_bounds_min_max[axis + 3] =
          static_cast<float>(x_from_max + out_bounds_min_max[axis + 3]);
    }

    const double y_from_min =
        static_cast<double>(bounds_min_max[1]) * matrix3x3[axis + 3];
    const double y_from_max =
        static_cast<double>(bounds_min_max[4]) * matrix3x3[axis + 3];
    if (!(y_from_max > y_from_min)) {
      out_bounds_min_max[axis] =
          static_cast<float>(y_from_max + out_bounds_min_max[axis]);
      out_bounds_min_max[axis + 3] =
          static_cast<float>(y_from_min + out_bounds_min_max[axis + 3]);
    } else {
      out_bounds_min_max[axis] =
          static_cast<float>(y_from_min + out_bounds_min_max[axis]);
      out_bounds_min_max[axis + 3] =
          static_cast<float>(y_from_max + out_bounds_min_max[axis + 3]);
    }

    const double z_from_min =
        static_cast<double>(bounds_min_max[2]) * matrix3x3[axis + 6];
    const double z_from_max =
        static_cast<double>(bounds_min_max[5]) * matrix3x3[axis + 6];
    if (!(z_from_max > z_from_min)) {
      out_bounds_min_max[axis] =
          static_cast<float>(z_from_max + out_bounds_min_max[axis]);
      out_bounds_min_max[axis + 3] =
          static_cast<float>(z_from_min + out_bounds_min_max[axis + 3]);
    } else {
      out_bounds_min_max[axis] =
          static_cast<float>(z_from_min + out_bounds_min_max[axis]);
      out_bounds_min_max[axis + 3] =
          static_cast<float>(z_from_max + out_bounds_min_max[axis + 3]);
    }
  }

  return out_bounds_min_max;
}

inline float* TransformAABBByPackedRowMajorBasis3x3(
    float* out_bounds_min_max, const float* bounds_min_max,
    const float* matrix3x3) noexcept {
  out_bounds_min_max[0] = 0.0f;
  out_bounds_min_max[1] = 0.0f;
  out_bounds_min_max[2] = 0.0f;
  out_bounds_min_max[3] = 0.0f;
  out_bounds_min_max[4] = 0.0f;
  out_bounds_min_max[5] = 0.0f;
  return AccumulateAABBByPackedRowMajorBasis3x3(
      out_bounds_min_max, bounds_min_max, matrix3x3);
}

inline float* TransformAABBByPackedRowMajorAffine4x3(
    float* out_bounds_min_max, const float* bounds_min_max,
    const float* affine4x3) noexcept {
  out_bounds_min_max[0] = affine4x3[9];
  out_bounds_min_max[1] = affine4x3[10];
  out_bounds_min_max[2] = affine4x3[11];
  out_bounds_min_max[3] = affine4x3[9];
  out_bounds_min_max[4] = affine4x3[10];
  out_bounds_min_max[5] = affine4x3[11];
  return AccumulateAABBByPackedRowMajorBasis3x3(
      out_bounds_min_max, bounds_min_max, affine4x3);
}

inline float* TransformAABBByRowMajorAffine4x4(
    float* out_bounds_min_max, const float* bounds_min_max,
    const float* matrix4x4) noexcept {
  out_bounds_min_max[0] = matrix4x4[12];
  out_bounds_min_max[1] = matrix4x4[13];
  out_bounds_min_max[2] = matrix4x4[14];
  out_bounds_min_max[3] = matrix4x4[12];
  out_bounds_min_max[4] = matrix4x4[13];
  out_bounds_min_max[5] = matrix4x4[14];

  for (int axis = 0; axis < 3; ++axis) {

    const double x_from_min =
        static_cast<double>(bounds_min_max[0]) * matrix4x4[axis];
    const double x_from_max =
        static_cast<double>(bounds_min_max[3]) * matrix4x4[axis];
    if (!(x_from_max > x_from_min)) {
      out_bounds_min_max[axis] =
          static_cast<float>(x_from_max + out_bounds_min_max[axis]);
      out_bounds_min_max[axis + 3] =
          static_cast<float>(x_from_min + out_bounds_min_max[axis + 3]);
    } else {
      out_bounds_min_max[axis] =
          static_cast<float>(x_from_min + out_bounds_min_max[axis]);
      out_bounds_min_max[axis + 3] =
          static_cast<float>(x_from_max + out_bounds_min_max[axis + 3]);
    }

    const double y_from_min =
        static_cast<double>(bounds_min_max[1]) * matrix4x4[4 + axis];
    const double y_from_max =
        static_cast<double>(bounds_min_max[4]) * matrix4x4[4 + axis];
    if (!(y_from_max > y_from_min)) {
      out_bounds_min_max[axis] =
          static_cast<float>(y_from_max + out_bounds_min_max[axis]);
      out_bounds_min_max[axis + 3] =
          static_cast<float>(y_from_min + out_bounds_min_max[axis + 3]);
    } else {
      out_bounds_min_max[axis] =
          static_cast<float>(y_from_min + out_bounds_min_max[axis]);
      out_bounds_min_max[axis + 3] =
          static_cast<float>(y_from_max + out_bounds_min_max[axis + 3]);
    }

    const double z_from_min =
        static_cast<double>(bounds_min_max[2]) * matrix4x4[8 + axis];
    const double z_from_max =
        static_cast<double>(bounds_min_max[5]) * matrix4x4[8 + axis];
    if (!(z_from_max > z_from_min)) {
      out_bounds_min_max[axis] =
          static_cast<float>(z_from_max + out_bounds_min_max[axis]);
      out_bounds_min_max[axis + 3] =
          static_cast<float>(z_from_min + out_bounds_min_max[axis + 3]);
    } else {
      out_bounds_min_max[axis] =
          static_cast<float>(z_from_min + out_bounds_min_max[axis]);
      out_bounds_min_max[axis + 3] =
          static_cast<float>(z_from_max + out_bounds_min_max[axis + 3]);
    }
  }

  return out_bounds_min_max;
}

inline float* Copy4x4(float* dst, const float* src) noexcept {
  dst[0] = src[0];   dst[1] = src[1];   dst[2] = src[2];   dst[3] = src[3];
  dst[4] = src[4];   dst[5] = src[5];   dst[6] = src[6];   dst[7] = src[7];
  dst[8] = src[8];   dst[9] = src[9];   dst[10] = src[10]; dst[11] = src[11];
  dst[12] = src[12]; dst[13] = src[13]; dst[14] = src[14]; dst[15] = src[15];
  return dst;
}

inline float* Multiply4x4(float* dst, const float* lhs, const float* rhs) noexcept {
  dst[0]  = rhs[8]  * lhs[2]  + lhs[1] * rhs[4]  + lhs[3]  * rhs[12] + lhs[0]  * rhs[0];
  dst[1]  = rhs[9]  * lhs[2]  + lhs[1] * rhs[5]  + rhs[1]  * lhs[0]  + rhs[13] * lhs[3];
  dst[2]  = lhs[1]  * rhs[6]  + lhs[3] * rhs[14] + rhs[2]  * lhs[0]  + lhs[2]  * rhs[10];
  dst[3]  = rhs[11] * lhs[2]  + rhs[3] * lhs[0]  + lhs[1]  * rhs[7]  + rhs[15] * lhs[3];
  dst[4]  = lhs[5]  * rhs[4]  + rhs[8] * lhs[6]  + lhs[7]  * rhs[12] + lhs[4]  * rhs[0];
  dst[5]  = lhs[5]  * rhs[5]  + rhs[9] * lhs[6]  + rhs[13] * lhs[7]  + lhs[4]  * rhs[1];
  dst[6]  = rhs[14] * lhs[7]  + lhs[6] * rhs[10] + lhs[5]  * rhs[6]  + lhs[4]  * rhs[2];
  dst[7]  = lhs[5]  * rhs[7]  + rhs[15]* lhs[7]  + rhs[11] * lhs[6]  + lhs[4]  * rhs[3];
  dst[8]  = lhs[9]  * rhs[4]  + rhs[8] * lhs[10] + lhs[11] * rhs[12] + lhs[8]  * rhs[0];
  dst[9]  = lhs[9]  * rhs[5]  + rhs[13]* lhs[11] + rhs[9]  * lhs[10] + lhs[8]  * rhs[1];
  dst[10] = lhs[11] * rhs[14] + lhs[10]* rhs[10] + lhs[9]  * rhs[6]  + lhs[8]  * rhs[2];
  dst[11] = lhs[9]  * rhs[7]  + rhs[15]* lhs[11] + rhs[11] * lhs[10] + lhs[8]  * rhs[3];
  dst[12] = lhs[13] * rhs[4]  + rhs[8] * lhs[14] + lhs[15] * rhs[12] + lhs[12] * rhs[0];
  dst[13] = lhs[13] * rhs[5]  + rhs[13]* lhs[15] + rhs[9]  * lhs[14] + lhs[12] * rhs[1];
  dst[14] = lhs[15] * rhs[14] + lhs[14]* rhs[10] + lhs[13] * rhs[6]  + lhs[12] * rhs[2];
  dst[15] = lhs[13] * rhs[7]  + rhs[15]* lhs[15] + rhs[11] * lhs[14] + lhs[12] * rhs[3];
  return dst;
}

inline float* BuildInverseRigidTransform4x4(float* dst, const float* src) noexcept {

  dst[0]  = src[0];  dst[1]  = src[4];  dst[2]  = src[8];   dst[3]  = 0.0f;
  dst[4]  = src[1];  dst[5]  = src[5];  dst[6]  = src[9];   dst[7]  = 0.0f;
  dst[8]  = src[2];  dst[9]  = src[6];  dst[10] = src[10];  dst[11] = 0.0f;
  dst[15] = 1.0f;

  const float tx = -src[12];
  const float ty = -src[13];
  const float tz = -src[14];
  dst[12] = dst[0] * tx + dst[4] * ty + dst[8]  * tz;
  dst[13] = dst[1] * tx + dst[5] * ty + dst[9]  * tz;
  dst[14] = dst[2] * tx + dst[6] * ty + dst[10] * tz;
  return dst;
}

inline float* BuildRowMajorAffine4x4FromAxesAndPosition(
    float* out_matrix4x4,
    const float* axis0_xyz,
    const float* axis1_xyz,
    const float* axis2_xyz,
    const float* position_xyz) noexcept {
  out_matrix4x4[0]  = axis0_xyz[0];
  out_matrix4x4[1]  = axis0_xyz[1];
  out_matrix4x4[2]  = axis0_xyz[2];
  out_matrix4x4[3]  = 0.0f;
  out_matrix4x4[4]  = axis1_xyz[0];
  out_matrix4x4[5]  = axis1_xyz[1];
  out_matrix4x4[6]  = axis1_xyz[2];
  out_matrix4x4[7]  = 0.0f;
  out_matrix4x4[8]  = axis2_xyz[0];
  out_matrix4x4[9]  = axis2_xyz[1];
  out_matrix4x4[10] = axis2_xyz[2];
  out_matrix4x4[11] = 0.0f;
  out_matrix4x4[12] = position_xyz[0];
  out_matrix4x4[13] = position_xyz[1];
  out_matrix4x4[14] = position_xyz[2];
  out_matrix4x4[15] = 1.0f;
  return out_matrix4x4;
}

}

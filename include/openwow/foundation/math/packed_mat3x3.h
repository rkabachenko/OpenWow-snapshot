
#pragma once

#include <cmath>
#include <cstring>

namespace openwow::math::packed_mat3x3 {

inline double Determinant(const float* matrix3x3) {
  return static_cast<double>(matrix3x3[6]) * static_cast<double>(matrix3x3[1]) *
             static_cast<double>(matrix3x3[5]) +
         static_cast<double>(matrix3x3[2]) * static_cast<double>(matrix3x3[3]) *
             static_cast<double>(matrix3x3[7]) +
         static_cast<double>(matrix3x3[8]) * static_cast<double>(matrix3x3[0]) *
             static_cast<double>(matrix3x3[4]) -
         static_cast<double>(matrix3x3[6]) * static_cast<double>(matrix3x3[2]) *
             static_cast<double>(matrix3x3[4]) -
         static_cast<double>(matrix3x3[8]) * static_cast<double>(matrix3x3[1]) *
             static_cast<double>(matrix3x3[3]) -
         static_cast<double>(matrix3x3[0]) * static_cast<double>(matrix3x3[7]) *
             static_cast<double>(matrix3x3[5]);
}

inline float* SetRowMajor(float* out_matrix3x3,
                          float m00, float m01, float m02,
                          float m10, float m11, float m12,
                          float m20, float m21, float m22) {
  out_matrix3x3[0] = m00;
  out_matrix3x3[1] = m01;
  out_matrix3x3[2] = m02;
  out_matrix3x3[3] = m10;
  out_matrix3x3[4] = m11;
  out_matrix3x3[5] = m12;
  out_matrix3x3[6] = m20;
  out_matrix3x3[7] = m21;
  out_matrix3x3[8] = m22;
  return out_matrix3x3;
}

inline float* ComputeAdjugate(float* out_matrix3x3, const float* matrix3x3) {
  out_matrix3x3[0] = matrix3x3[4] * matrix3x3[8] - matrix3x3[7] * matrix3x3[5];
  out_matrix3x3[1] =
      -(matrix3x3[8] * matrix3x3[1] - matrix3x3[7] * matrix3x3[2]);
  out_matrix3x3[2] =
      matrix3x3[5] * matrix3x3[1] - matrix3x3[4] * matrix3x3[2];
  out_matrix3x3[3] =
      -(matrix3x3[3] * matrix3x3[8] - matrix3x3[5] * matrix3x3[6]);
  out_matrix3x3[4] =
      matrix3x3[0] * matrix3x3[8] - matrix3x3[6] * matrix3x3[2];
  out_matrix3x3[5] =
      -(matrix3x3[5] * matrix3x3[0] - matrix3x3[3] * matrix3x3[2]);
  out_matrix3x3[6] =
      matrix3x3[7] * matrix3x3[3] - matrix3x3[4] * matrix3x3[6];
  out_matrix3x3[7] =
      -(matrix3x3[7] * matrix3x3[0] - matrix3x3[6] * matrix3x3[1]);
  out_matrix3x3[8] =
      matrix3x3[4] * matrix3x3[0] - matrix3x3[3] * matrix3x3[1];
  return out_matrix3x3;
}

inline float* InvertWithDeterminant(float* out_matrix3x3,
                                    const float* matrix3x3,
                                    float determinant) {
  const float inverse_determinant = 1.0f / determinant;
  float adjugate[9]{};
  ComputeAdjugate(adjugate, matrix3x3);

  out_matrix3x3[0] = adjugate[0] * inverse_determinant;
  out_matrix3x3[1] = adjugate[1] * inverse_determinant;
  out_matrix3x3[2] = adjugate[2] * inverse_determinant;
  out_matrix3x3[3] = adjugate[3] * inverse_determinant;
  out_matrix3x3[4] = adjugate[4] * inverse_determinant;
  out_matrix3x3[5] = adjugate[5] * inverse_determinant;
  out_matrix3x3[6] = adjugate[6] * inverse_determinant;
  out_matrix3x3[7] = adjugate[7] * inverse_determinant;
  out_matrix3x3[8] = adjugate[8] * inverse_determinant;
  return out_matrix3x3;
}

inline void ScaleRows(float* matrix3x3, float sx, float sy, float sz) {
  matrix3x3[0] *= sx;
  matrix3x3[1] *= sx;
  matrix3x3[2] *= sx;

  matrix3x3[3] *= sy;
  matrix3x3[4] *= sy;
  matrix3x3[5] *= sy;

  matrix3x3[6] *= sz;
  matrix3x3[7] *= sz;
  matrix3x3[8] *= sz;
}

inline const float* ScaleRowsFromVec3(float* matrix3x3, const float* scale_xyz) {
  matrix3x3[0] *= scale_xyz[0];
  matrix3x3[1] *= scale_xyz[0];
  matrix3x3[2] *= scale_xyz[0];

  matrix3x3[3] *= scale_xyz[1];
  matrix3x3[4] *= scale_xyz[1];
  matrix3x3[5] *= scale_xyz[1];

  matrix3x3[6] *= scale_xyz[2];
  matrix3x3[7] *= scale_xyz[2];
  matrix3x3[8] *= scale_xyz[2];
  return scale_xyz;
}

inline float* MultiplyRowMajor(float* out_matrix3x3,
                               const float* left_matrix3x3,
                               const float* right_matrix3x3) {
  out_matrix3x3[0] = left_matrix3x3[1] * right_matrix3x3[3]
                     + right_matrix3x3[6] * left_matrix3x3[2]
                     + left_matrix3x3[0] * right_matrix3x3[0];
  out_matrix3x3[1] = right_matrix3x3[7] * left_matrix3x3[2]
                     + left_matrix3x3[0] * right_matrix3x3[1]
                     + left_matrix3x3[1] * right_matrix3x3[4];
  out_matrix3x3[2] = left_matrix3x3[1] * right_matrix3x3[5]
                     + right_matrix3x3[2] * left_matrix3x3[0]
                     + right_matrix3x3[8] * left_matrix3x3[2];
  out_matrix3x3[3] = left_matrix3x3[3] * right_matrix3x3[0]
                     + right_matrix3x3[6] * left_matrix3x3[5]
                     + right_matrix3x3[3] * left_matrix3x3[4];
  out_matrix3x3[4] = left_matrix3x3[3] * right_matrix3x3[1]
                     + right_matrix3x3[7] * left_matrix3x3[5]
                     + right_matrix3x3[4] * left_matrix3x3[4];
  out_matrix3x3[5] = left_matrix3x3[5] * right_matrix3x3[8]
                     + left_matrix3x3[4] * right_matrix3x3[5]
                     + left_matrix3x3[3] * right_matrix3x3[2];
  out_matrix3x3[6] = right_matrix3x3[6] * left_matrix3x3[8]
                     + left_matrix3x3[7] * right_matrix3x3[3]
                     + left_matrix3x3[6] * right_matrix3x3[0];
  out_matrix3x3[7] = left_matrix3x3[6] * right_matrix3x3[1]
                     + right_matrix3x3[7] * left_matrix3x3[8]
                     + right_matrix3x3[4] * left_matrix3x3[7];
  out_matrix3x3[8] = left_matrix3x3[8] * right_matrix3x3[8]
                     + left_matrix3x3[7] * right_matrix3x3[5]
                     + left_matrix3x3[6] * right_matrix3x3[2];
  return out_matrix3x3;
}

inline void MultiplyVec3(float* out_xyz, const float* vector_xyz,
                         const float* matrix3x3) {
  const float x = vector_xyz[0];
  const float y = vector_xyz[1];
  const float z = vector_xyz[2];

  out_xyz[0] = matrix3x3[0] * x + matrix3x3[3] * y + matrix3x3[6] * z;
  out_xyz[1] = matrix3x3[1] * x + matrix3x3[4] * y + matrix3x3[7] * z;
  out_xyz[2] = matrix3x3[2] * x + matrix3x3[5] * y + matrix3x3[8] * z;
}

inline float* TransformVec3ByPackedMat3x3(float* out_xyz, float* in_out_vec_xyz,
                                          const float* matrix3x3) {
  MultiplyVec3(in_out_vec_xyz, in_out_vec_xyz, matrix3x3);
  out_xyz[0] = in_out_vec_xyz[0];
  out_xyz[1] = in_out_vec_xyz[1];
  out_xyz[2] = in_out_vec_xyz[2];
  return out_xyz;
}

inline float* BuildZRotationMatrix(float* out_matrix3x3, float angle_radians) {
  const float c = std::cos(angle_radians);
  const float s = std::sin(angle_radians);
  out_matrix3x3[0] = c;
  out_matrix3x3[1] = s;
  out_matrix3x3[2] = 0.0f;
  out_matrix3x3[3] = -s;
  out_matrix3x3[4] = c;
  out_matrix3x3[5] = 0.0f;
  out_matrix3x3[6] = 0.0f;
  out_matrix3x3[7] = 0.0f;
  out_matrix3x3[8] = 1.0f;
  return out_matrix3x3;
}

inline float* PrependYRotation(float* matrix3x3, float angle_radians) {
  const float c = std::cos(angle_radians);
  const float s = std::sin(angle_radians);
  const float rotation[9] = {
      c,    0.0f, -s,
      0.0f, 1.0f, 0.0f,
      s,    0.0f, c,
  };
  float product[9]{};
  MultiplyRowMajor(product, rotation, matrix3x3);
  std::memcpy(matrix3x3, product, sizeof(product));
  return matrix3x3;
}

inline float* BuildPackedBasisFromYawPitchRoll(float* out_matrix3x3,
                                               float yaw_radians,
                                               float pitch_radians,
                                               float roll_radians) {

  const float cos_yaw = std::cos(yaw_radians);
  const float sin_yaw = std::sin(yaw_radians);
  const float cos_pitch = std::cos(pitch_radians);
  const float sin_pitch = std::sin(pitch_radians);
  const float cos_roll = std::cos(roll_radians);
  const float sin_roll = std::sin(roll_radians);

  const float rotate_z[9] = {
      cos_yaw, -sin_yaw, 0.0f,
      sin_yaw, cos_yaw,  0.0f,
      0.0f,    0.0f,     1.0f,
  };
  const float rotate_y[9] = {
      cos_pitch, 0.0f, sin_pitch,
      0.0f,      1.0f, 0.0f,
      -sin_pitch, 0.0f, cos_pitch,
  };
  const float rotate_x[9] = {
      1.0f, 0.0f,     0.0f,
      0.0f, cos_roll, -sin_roll,
      0.0f, sin_roll, cos_roll,
  };

  float pitch_roll[9]{};
  float composed[9]{};
  MultiplyRowMajor(pitch_roll, rotate_y, rotate_x);
  MultiplyRowMajor(composed, rotate_z, pitch_roll);
  return SetRowMajor(out_matrix3x3,
                     composed[0], composed[3], composed[6],
                     composed[1], composed[4], composed[7],
                     composed[2], composed[5], composed[8]);
}

inline float* BuildPackedAxisAngleRotationMatrix3x3(float* out_matrix3x3,
                                                    float radians,
                                                    const float* axis_xyz,
                                                    bool axis_is_normalized) {
  float axis_x = axis_xyz[0];
  float axis_y = axis_xyz[1];
  float axis_z = axis_xyz[2];
  if (!axis_is_normalized) {
    const float inverse_length =
        1.0f / std::sqrt(axis_x * axis_x + axis_y * axis_y + axis_z * axis_z);
    axis_x *= inverse_length;
    axis_y *= inverse_length;
    axis_z *= inverse_length;
  }

  const float cosine = std::cos(radians);
  const float sine = std::sin(radians);
  const float one_minus_cosine = 1.0f - cosine;
  const float xy = axis_x * axis_y;
  const float yz = axis_y * axis_z;
  const float xz = axis_z * axis_x;
  const float x_sine = axis_x * sine;
  const float y_sine = axis_y * sine;
  const float z_sine = axis_z * sine;

  out_matrix3x3[0] = axis_x * axis_x * one_minus_cosine + cosine;
  out_matrix3x3[1] = xy * one_minus_cosine + z_sine;
  out_matrix3x3[2] = xz * one_minus_cosine - y_sine;
  out_matrix3x3[3] = xy * one_minus_cosine - z_sine;
  out_matrix3x3[4] = axis_y * axis_y * one_minus_cosine + cosine;
  out_matrix3x3[5] = yz * one_minus_cosine + x_sine;
  out_matrix3x3[6] = xz * one_minus_cosine + y_sine;
  out_matrix3x3[7] = yz * one_minus_cosine - x_sine;
  out_matrix3x3[8] = axis_z * axis_z * one_minus_cosine + cosine;
  return out_matrix3x3;
}

inline float* PrependPackedAxisAngleRotationMatrix3x3(float* matrix3x3,
                                                      float radians,
                                                      const float* axis_xyz,
                                                      bool axis_is_normalized) {
  float rotation[9]{};
  float product[9]{};
  BuildPackedAxisAngleRotationMatrix3x3(rotation,
                                        radians,
                                        axis_xyz,
                                        axis_is_normalized);
  MultiplyRowMajor(product, rotation, matrix3x3);
  std::memcpy(matrix3x3, product, sizeof(product));
  return matrix3x3;
}

inline bool ExtractYawPitchRoll(const float* matrix3x3,
                                float* out_yaw_radians,
                                float* out_pitch_radians,
                                float* out_roll_radians) {
  constexpr float kHalfPi = 1.5707964f;

  if (!(matrix3x3[2] < 1.0f)) {
    *out_yaw_radians = std::atan2(-matrix3x3[3], -matrix3x3[6]);
    *out_pitch_radians = -kHalfPi;
    *out_roll_radians = 0.0f;
    return false;
  }

  if (!(matrix3x3[2] > -1.0f)) {
    *out_yaw_radians = -std::atan2(matrix3x3[3], matrix3x3[6]);
    *out_pitch_radians = kHalfPi;
    *out_roll_radians = 0.0f;
    return false;
  }

  *out_yaw_radians = std::atan2(matrix3x3[1], matrix3x3[0]);
  *out_pitch_radians = std::asin(-matrix3x3[2]);
  *out_roll_radians = std::atan2(matrix3x3[5], matrix3x3[8]);
  return true;
}

inline float* SparseRotateZAndMultiply(float* out_result,
                                       float* out_rotation,
                                       const float* input_matrix,
                                       float angle_radians,
                                       const float* axis_xyz) noexcept {
  const float cos_angle = std::cos(angle_radians);
  const float sin_angle = std::sin(angle_radians);
  const float z = axis_xyz[2];
  const float z_squared = z * z;
  const float sin_z = sin_angle * z;

  out_rotation[0] = cos_angle;
  out_rotation[1] = sin_z;
  out_rotation[3] = -sin_z;
  out_rotation[4] = cos_angle;
  out_rotation[8] = z_squared * (1.0f - cos_angle) + cos_angle;

  out_result[0] = cos_angle * input_matrix[0] + sin_z * input_matrix[3];
  out_result[1] = out_rotation[0] * input_matrix[1] +
                  out_rotation[1] * input_matrix[4];
  out_result[2] = out_rotation[0] * input_matrix[2] +
                  out_rotation[1] * input_matrix[5];

  out_result[3] = out_rotation[4] * input_matrix[3] +
                  input_matrix[0] * out_rotation[3];
  out_result[4] = out_rotation[4] * input_matrix[4] +
                  input_matrix[1] * out_rotation[3];
  out_result[5] = out_rotation[3] * input_matrix[2] +
                  out_rotation[4] * input_matrix[5];

  out_result[6] = input_matrix[6] * out_rotation[8];
  out_result[7] = input_matrix[7] * out_rotation[8];
  out_result[8] = input_matrix[8] * out_rotation[8];

  return out_rotation;
}

}

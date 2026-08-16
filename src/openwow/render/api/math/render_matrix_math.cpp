#include "openwow/render/api/math/render_matrix_math.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace openwow::render {
namespace {

constexpr float kProjectionEpsilon = 0.00000023841858f;

RenderVec3 TransformFrustumCorner(
    const float x, const float y, const float z, const float w,
    const RenderMatrix4x4View inverse_view_projection) noexcept {
  const auto world = TransformRowVector4x4(
      RenderVec4{x, y, z, w}, inverse_view_projection);
  return RenderVec3{world[0], world[1], world[2]};
}

RenderVec3 BilinearInterpolateFrustumFace(
    const RenderVec3& bottom_left, const RenderVec3& top_left,
    const RenderVec3& top_right, const RenderVec3& bottom_right,
    const float normalized_x, const float normalized_y) noexcept {
  RenderVec3 point{};
  for (std::size_t component = 0; component < point.size(); ++component) {
    const float left =
        bottom_left[component] +
        (top_left[component] - bottom_left[component]) * normalized_y;
    const float right =
        bottom_right[component] +
        (top_right[component] - bottom_right[component]) * normalized_y;
    point[component] = left + (right - left) * normalized_x;
  }
  return point;
}

float ExtractPerspectiveNearPlane(
    const RenderMatrix4x4View projection) noexcept {
  if (std::fabs(projection[11] - 1.0f) > 1.0e-5f ||
      std::fabs(projection[15]) > 1.0e-5f) {
    return 0.0f;
  }

  const float denominator = projection[10] + 1.0f;
  if (std::fabs(denominator) < 1.0e-6f) {
    return 0.0f;
  }

  const float near_plane = -projection[14] / denominator;
  return near_plane > 0.0f ? near_plane : 0.0f;
}

}

RenderMatrix4x4 AddMatrix4x4(const RenderMatrix4x4View lhs,
                             const RenderMatrix4x4View rhs) noexcept {
  RenderMatrix4x4 sum{};
  for (std::size_t index = 0; index < sum.size(); ++index) {
    sum[index] = lhs[index] + rhs[index];
  }
  return sum;
}

RenderMatrix4x4 ScaleMatrix4x4(const RenderMatrix4x4View matrix,
                               const float scale) noexcept {
  RenderMatrix4x4 scaled{};
  for (std::size_t index = 0; index < scaled.size(); ++index) {
    scaled[index] = scale * matrix[index];
  }
  return scaled;
}

RenderMatrix4x4 ScaleMatrix4x4BasisRows(
    const RenderMatrix4x4View matrix, const RenderVec3View scale) noexcept {
  RenderMatrix4x4 scaled{};
  std::copy(matrix.begin(), matrix.end(), scaled.begin());
  scaled[0] = scaled[0] * scale[0];
  scaled[1] = scaled[1] * scale[0];
  scaled[2] = scaled[2] * scale[0];
  scaled[4] = scaled[4] * scale[1];
  scaled[5] = scaled[5] * scale[1];
  scaled[6] = scaled[6] * scale[1];
  scaled[8] = scaled[8] * scale[2];
  scaled[9] = scaled[9] * scale[2];
  scaled[10] = scaled[10] * scale[2];
  return scaled;
}

RenderMatrix4x4 PrependMatrix4x4Translation(
    const RenderMatrix4x4View matrix, const RenderVec3View translation) noexcept {
  RenderMatrix4x4 translated{};
  std::copy(matrix.begin(), matrix.end(), translated.begin());
  translated[12] = translation[0] * matrix[0] + translation[1] * matrix[4] +
                   translation[2] * matrix[8] + matrix[12];
  translated[13] = translation[0] * matrix[1] + translation[1] * matrix[5] +
                   translation[2] * matrix[9] + matrix[13];
  translated[14] = translation[0] * matrix[2] + translation[1] * matrix[6] +
                   translation[2] * matrix[10] + matrix[14];
  return translated;
}

RenderMatrix4x4 TransposeMatrix4x4(const RenderMatrix4x4View matrix) noexcept {
  return RenderMatrix4x4{
      matrix[0], matrix[4], matrix[8], matrix[12],
      matrix[1], matrix[5], matrix[9], matrix[13],
      matrix[2], matrix[6], matrix[10], matrix[14],
      matrix[3], matrix[7], matrix[11], matrix[15],
  };
}

double DeterminantMatrix4x4(const RenderMatrix4x4View matrix) noexcept {
  const float cofactor0 =
      (((((matrix[15] * matrix[5] * matrix[10] +
           matrix[13] * matrix[11] * matrix[6]) +
          matrix[14] * matrix[9] * matrix[7]) -
         matrix[13] * matrix[10] * matrix[7]) -
        matrix[15] * matrix[9] * matrix[6]) -
       matrix[14] * matrix[5] * matrix[11]);
  const float cofactor1 =
      (((((matrix[15] * matrix[10] * matrix[4] +
           matrix[11] * matrix[6] * matrix[12]) +
          matrix[14] * matrix[7] * matrix[8]) -
         matrix[10] * matrix[7] * matrix[12]) -
        matrix[15] * matrix[6] * matrix[8]) -
       matrix[14] * matrix[11] * matrix[4]);
  const float cofactor2 =
      (((((matrix[15] * matrix[9] * matrix[4] +
           matrix[5] * matrix[11] * matrix[12]) +
          matrix[7] * matrix[8] * matrix[13]) -
         matrix[9] * matrix[7] * matrix[12]) -
        matrix[15] * matrix[8] * matrix[5]) -
       matrix[11] * matrix[4] * matrix[13]);
  const float cofactor3 =
      (((((matrix[9] * matrix[4] * matrix[14] +
           matrix[5] * matrix[10] * matrix[12]) +
          matrix[6] * matrix[8] * matrix[13]) -
         matrix[9] * matrix[6] * matrix[12]) -
        matrix[14] * matrix[8] * matrix[5]) -
       matrix[13] * matrix[10] * matrix[4]);

  const float determinant =
      ((cofactor0 * matrix[0] - cofactor1 * matrix[1]) +
       cofactor2 * matrix[2]) -
      cofactor3 * matrix[3];
  return static_cast<double>(determinant);
}

RenderMatrix4x4 AdjugateMatrix4x4(const RenderMatrix4x4View matrix) noexcept {
  const float m10 = matrix[10];
  const float m9 = matrix[9];
  const float m8 = matrix[8];
  const float m1 = matrix[1];
  const float m6 = matrix[6];
  const float m5 = matrix[5];
  const float m4 = matrix[4];
  const float m2 = matrix[2];
  const float m1_m6 = m1 * m6;
  const float m0 = matrix[0];
  const float m4_m2 = m4 * m2;
  const float m5_m0 = m5 * m0;
  const float m5_m2 = m5 * m2;
  const float m6_m0 = m6 * m0;
  const float m1_m4 = m1 * m4;
  const float m14 = matrix[14];
  const float m13 = matrix[13];
  const float m12 = matrix[12];
  const float m11 = matrix[11];
  const float m7 = matrix[7];
  const float m3 = matrix[3];
  const float m1_m7 = m1 * m7;
  const float m4_m3 = m4 * m3;
  const float m5_m3 = m5 * m3;
  const float m0_m7 = m0 * m7;
  const float m15 = matrix[15];
  const float m6_m3 = m6 * m3;
  const float m2_m7 = m2 * m7;

  RenderMatrix4x4 adjugate{};
  adjugate[0] = (((m10 * m5 * m15 + m13 * m6 * m11) + m14 * m9 * m7) -
                 m13 * m7 * m10) -
                m9 * m6 * m15 - m14 * m5 * m11;
  adjugate[1] = -(((((m10 * m1 * m15 + m13 * m2 * m11) + m14 * m9 * m3) -
                    m13 * m3 * m10) -
                   m9 * m2 * m15) -
                  m14 * m1 * m11);
  adjugate[2] = (((m1_m6 * m15 + m13 * m2_m7) + m14 * m5_m3) -
                 m13 * m6_m3) -
                m5_m2 * m15 - m14 * m1_m7;
  adjugate[3] = -(((((m1_m6 * m11 + m9 * m2_m7) + m10 * m5_m3) -
                    m9 * m6_m3) -
                   m5_m2 * m11) -
                  m10 * m1_m7);
  adjugate[4] = -(((((m10 * m4 * m15 + m6 * m11 * m12) + m14 * m8 * m7) -
                    m12 * m7 * m10) -
                   m8 * m6 * m15) -
                  m14 * m4 * m11);
  adjugate[5] = (((m10 * m0 * m15 + m12 * m2 * m11) + m14 * m8 * m3) -
                 m12 * m3 * m10) -
                m8 * m2 * m15 - m14 * m0 * m11;
  adjugate[6] = -(((((m6_m0 * m15 + m12 * m2_m7) + m14 * m4_m3) -
                    m12 * m6_m3) -
                   m4_m2 * m15) -
                  m14 * m0_m7);
  adjugate[7] = (((m6_m0 * m11 + m8 * m2_m7) + m10 * m4_m3) -
                 m8 * m6_m3) -
                m4_m2 * m11 - m10 * m0_m7;
  adjugate[8] = (((m9 * m4 * m15 + m12 * m5 * m11) + m13 * m8 * m7) -
                 m12 * m9 * m7) -
                m8 * m5 * m15 - m13 * m4 * m11;
  adjugate[9] = -(((((m9 * m0 * m15 + m12 * m1 * m11) + m13 * m8 * m3) -
                    m12 * m9 * m3) -
                   m8 * m1 * m15) -
                  m13 * m0 * m11);
  adjugate[10] = (((m5_m0 * m15 + m12 * m1_m7) + m13 * m4_m3) -
                  m12 * m5_m3) -
                 m1_m4 * m15 - m13 * m0_m7;
  adjugate[11] = -(((((m5_m0 * m11 + m8 * m1_m7) + m9 * m4_m3) -
                     m8 * m5_m3) -
                    m1_m4 * m11) -
                   m9 * m0_m7);
  adjugate[12] = -(((((m14 * m9 * m4 + m12 * m10 * m5) + m13 * m8 * m6) -
                     m12 * m9 * m6) -
                    m14 * m8 * m5) -
                   m13 * m10 * m4);
  adjugate[13] = (((m14 * m9 * m0 + m12 * m10 * m1) + m13 * m8 * m2) -
                  m12 * m9 * m2) -
                 m14 * m8 * m1 - m13 * m10 * m0;
  adjugate[14] = -(((((m5_m0 * m14 + m1_m6 * m12) + m4_m2 * m13) -
                     m5_m2 * m12) -
                    m1_m4 * m14) -
                   m6_m0 * m13);
  adjugate[15] = (((m10 * m5_m0 + m8 * m1_m6) + m9 * m4_m2) -
                  m8 * m5_m2) -
                 m10 * m1_m4 - m9 * m6_m0;
  return adjugate;
}

RenderMatrix4x4 InvertMatrix4x4WithDeterminant(const RenderMatrix4x4View matrix,
                                               const float determinant) noexcept {
  return ScaleMatrix4x4(AdjugateMatrix4x4(matrix), 1.0f / determinant);
}

RenderMatrix4x4 InvertMatrix4x4(const RenderMatrix4x4View matrix) noexcept {
  return InvertMatrix4x4WithDeterminant(
      matrix, static_cast<float>(DeterminantMatrix4x4(matrix)));
}

RenderMatrix4x4 MultiplyMatrix4x4(const RenderMatrix4x4View lhs,
                                  const RenderMatrix4x4View rhs) noexcept {
  RenderMatrix4x4 product{};
  product[0] = rhs[0] * lhs[0] + rhs[4] * lhs[1] +
               rhs[8] * lhs[2] + rhs[12] * lhs[3];
  product[1] = rhs[1] * lhs[0] + rhs[5] * lhs[1] +
               rhs[9] * lhs[2] + rhs[13] * lhs[3];
  product[2] = rhs[2] * lhs[0] + rhs[6] * lhs[1] +
               rhs[10] * lhs[2] + rhs[14] * lhs[3];
  product[3] = lhs[0] * rhs[3] + lhs[1] * rhs[7] +
               lhs[2] * rhs[11] + lhs[3] * rhs[15];
  product[4] = rhs[0] * lhs[4] + rhs[4] * lhs[5] +
               rhs[8] * lhs[6] + rhs[12] * lhs[7];
  product[5] = rhs[1] * lhs[4] + rhs[5] * lhs[5] +
               rhs[9] * lhs[6] + rhs[13] * lhs[7];
  product[6] = rhs[2] * lhs[4] + rhs[6] * lhs[5] +
               rhs[10] * lhs[6] + rhs[14] * lhs[7];
  product[7] = lhs[4] * rhs[3] + rhs[7] * lhs[5] +
               rhs[11] * lhs[6] + rhs[15] * lhs[7];
  product[8] = rhs[0] * lhs[8] + rhs[4] * lhs[9] +
               rhs[8] * lhs[10] + rhs[12] * lhs[11];
  product[9] = rhs[1] * lhs[8] + rhs[5] * lhs[9] +
               rhs[9] * lhs[10] + rhs[13] * lhs[11];
  product[10] = rhs[2] * lhs[8] + rhs[6] * lhs[9] +
                rhs[10] * lhs[10] + rhs[14] * lhs[11];
  product[11] = lhs[8] * rhs[3] + rhs[7] * lhs[9] +
                rhs[11] * lhs[10] + rhs[15] * lhs[11];
  product[12] = lhs[12] * rhs[0] + lhs[13] * rhs[4] +
                lhs[14] * rhs[8] + lhs[15] * rhs[12];
  product[13] = lhs[12] * rhs[1] + lhs[13] * rhs[5] +
                lhs[14] * rhs[9] + lhs[15] * rhs[13];
  product[14] = lhs[12] * rhs[2] + lhs[13] * rhs[6] +
                lhs[14] * rhs[10] + lhs[15] * rhs[14];
  product[15] = lhs[12] * rhs[3] + lhs[13] * rhs[7] +
                lhs[14] * rhs[11] + lhs[15] * rhs[15];
  return product;
}

RenderVec4 TransformRowVector4x4(const RenderVec4View vector,
                                 const RenderMatrix4x4View matrix) noexcept {
  return RenderVec4{
      vector[0] * matrix[0] + vector[1] * matrix[4] +
          vector[2] * matrix[8] + vector[3] * matrix[12],
      vector[0] * matrix[1] + vector[1] * matrix[5] +
          vector[2] * matrix[9] + vector[3] * matrix[13],
      vector[0] * matrix[2] + vector[1] * matrix[6] +
          vector[2] * matrix[10] + vector[3] * matrix[14],
      vector[0] * matrix[3] + vector[1] * matrix[7] +
          vector[2] * matrix[11] + vector[3] * matrix[15],
  };
}

RenderVec3 TransformAffinePoint4x4(const RenderVec3View point,
                                   const RenderMatrix4x4View matrix) noexcept {
  return RenderVec3{
      point[0] * matrix[0] + point[1] * matrix[4] +
          point[2] * matrix[8] + matrix[12],
      point[0] * matrix[1] + point[1] * matrix[5] +
          point[2] * matrix[9] + matrix[13],
      point[0] * matrix[2] + point[1] * matrix[6] +
          point[2] * matrix[10] + matrix[14],
  };
}

RenderVec3 ExtractCameraPositionFromRetailViewMatrix(
    const RenderMatrix4x4View view) noexcept {
  const float tx = view[12];
  const float ty = view[13];
  const float tz = view[14];
  return RenderVec3{
      -(view[0] * tx + view[1] * ty + view[2] * tz),
      -(view[4] * tx + view[5] * ty + view[6] * tz),
      -(view[8] * tx + view[9] * ty + view[10] * tz),
  };
}

RenderFrustumCorners ComputeFrustumCornersFromViewProjection(
    const RenderMatrix4x4View view_matrix,
    const RenderMatrix4x4View projection_matrix) noexcept {
  const auto inverse_view = InvertMatrix4x4WithDeterminant(
      view_matrix, static_cast<float>(DeterminantMatrix4x4(view_matrix)));
  const auto inverse_projection = InvertMatrix4x4WithDeterminant(
      projection_matrix,
      static_cast<float>(DeterminantMatrix4x4(projection_matrix)));
  const auto inverse_view_projection =
      MultiplyMatrix4x4(inverse_projection, inverse_view);

  RenderFrustumCorners corners{};
  if (std::fabs(projection_matrix[15] - 1.0f) < kProjectionEpsilon) {
    constexpr std::array<RenderVec4, 8> kOrthographicClipCorners{{
        {-1.0f, -1.0f, -1.0f, 1.0f},
        {-1.0f, 1.0f, -1.0f, 1.0f},
        {1.0f, 1.0f, -1.0f, 1.0f},
        {1.0f, -1.0f, -1.0f, 1.0f},
        {-1.0f, -1.0f, 1.0f, 1.0f},
        {-1.0f, 1.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f, 1.0f},
        {1.0f, -1.0f, 1.0f, 1.0f},
    }};
    for (std::size_t index = 0; index < corners.size(); ++index) {
      const auto& clip = kOrthographicClipCorners[index];
      corners[index] = TransformFrustumCorner(
          clip[0], clip[1], clip[2], clip[3], inverse_view_projection);
    }
    return corners;
  }

  const float clip_w0 =
      -projection_matrix[14] / (projection_matrix[10] + 1.0f);
  const float clip_w1 =
      -projection_matrix[14] / (projection_matrix[10] - 1.0f);
  const float clip_z0 = -clip_w0;
  const float clip_xy1 = -clip_w1;

  corners[0] = TransformFrustumCorner(
      clip_z0, clip_z0, clip_z0, clip_w0, inverse_view_projection);
  corners[1] = TransformFrustumCorner(
      clip_z0, clip_w0, clip_z0, clip_w0, inverse_view_projection);
  corners[2] = TransformFrustumCorner(
      clip_w0, clip_w0, clip_z0, clip_w0, inverse_view_projection);
  corners[3] = TransformFrustumCorner(
      clip_w0, clip_z0, clip_z0, clip_w0, inverse_view_projection);
  corners[4] = TransformFrustumCorner(
      clip_xy1, clip_xy1, clip_w1, clip_w1, inverse_view_projection);
  corners[5] = TransformFrustumCorner(
      clip_xy1, clip_w1, clip_w1, clip_w1, inverse_view_projection);
  corners[6] = TransformFrustumCorner(
      clip_w1, clip_w1, clip_w1, clip_w1, inverse_view_projection);
  corners[7] = TransformFrustumCorner(
      clip_w1, clip_xy1, clip_w1, clip_w1, inverse_view_projection);
  return corners;
}

std::optional<RenderViewportRayEndpoints>
ComputeNormalizedViewportRayEndpoints(
    const float normalized_x, const float normalized_y,
    const RenderFrustumCorners& frustum_corners) noexcept {
  if (normalized_x < 0.0f || normalized_x > 1.0f ||
      normalized_y < 0.0f || normalized_y > 1.0f) {
    return std::nullopt;
  }

  return RenderViewportRayEndpoints{
      .near_point = BilinearInterpolateFrustumFace(
          frustum_corners[0], frustum_corners[1], frustum_corners[2],
          frustum_corners[3], normalized_x, normalized_y),
      .far_point = BilinearInterpolateFrustumFace(
          frustum_corners[4], frustum_corners[5], frustum_corners[6],
          frustum_corners[7], normalized_x, normalized_y),
  };
}

RenderScreenProjection ProjectWorldPointToViewport(
    const RenderVec3View world_point, const RenderMatrix4x4View view_matrix,
    const RenderMatrix4x4View projection_matrix,
    const RenderProjectionViewport& viewport) noexcept {
  RenderScreenProjection result{};
  const float near_plane = ExtractPerspectiveNearPlane(projection_matrix);
  const bool perspective = near_plane > 0.0f;

  RenderVec4 input{};
  if (perspective) {
    const auto eye =
        ExtractCameraPositionFromRetailViewMatrix(view_matrix);
    input = RenderVec4{world_point[0] - eye[0], world_point[1] - eye[1],
                       world_point[2] - eye[2], 0.0f};
  } else {
    input =
        RenderVec4{world_point[0], world_point[1], world_point[2], 1.0f};
  }

  const auto view_space = TransformRowVector4x4(input, view_matrix);
  const auto clip =
      TransformRowVector4x4(view_space, projection_matrix);
  result.position[2] = clip[2];
  if (clip[2] < near_plane || clip[3] <= 1.0e-8f) {
    return result;
  }

  const float inverse_w = 1.0f / clip[3];
  const float normalized_x = (clip[0] * inverse_w + 1.0f) * 0.5f;
  const float normalized_y = (clip[1] * inverse_w + 1.0f) * 0.5f;
  result.position[0] =
      viewport.left + normalized_x * (viewport.right - viewport.left);
  result.position[1] =
      viewport.top + normalized_y * (viewport.bottom - viewport.top);

  if (result.position[0] >= viewport.left) {
    result.clip_flags |= 1;
  }
  if (result.position[1] >= viewport.top) {
    result.clip_flags |= 2;
  }
  if (result.position[0] <= viewport.right) {
    result.clip_flags |= 4;
  }
  if (result.position[1] <= viewport.bottom) {
    result.clip_flags |= 8;
  }
  result.on_screen = result.clip_flags == 15;
  return result;
}

RenderMatrix4x4 BuildRotationMatrix4x4X(const float radians) noexcept {
  const float cosine = std::cos(radians);
  const float sine = std::sin(radians);
  return RenderMatrix4x4{
      1.0f, 0.0f, 0.0f, 0.0f,
      0.0f, cosine, sine, 0.0f,
      0.0f, -sine, cosine, 0.0f,
      0.0f, 0.0f, 0.0f, 1.0f,
  };
}

RenderMatrix4x4 BuildRotationMatrix4x4Y(const float radians) noexcept {
  const float cosine = std::cos(radians);
  const float sine = std::sin(radians);
  return RenderMatrix4x4{
      cosine, 0.0f, -sine, 0.0f,
      0.0f, 1.0f, 0.0f, 0.0f,
      sine, 0.0f, cosine, 0.0f,
      0.0f, 0.0f, 0.0f, 1.0f,
  };
}

RenderMatrix4x4 BuildRotationMatrix4x4Z(const float radians) noexcept {
  const float cosine = std::cos(radians);
  const float sine = std::sin(radians);
  return RenderMatrix4x4{
      cosine, sine, 0.0f, 0.0f,
      -sine, cosine, 0.0f, 0.0f,
      0.0f, 0.0f, 1.0f, 0.0f,
      0.0f, 0.0f, 0.0f, 1.0f,
  };
}

RenderMatrix4x4 BuildRotationMatrix4x4AxisAngle(
    const float radians, const RenderVec3View axis,
    const RotationAxisNormalization normalization) noexcept {
  float axis_x = axis[0];
  float axis_y = axis[1];
  float axis_z = axis[2];
  if (normalization == RotationAxisNormalization::kNormalize) {
    const float inverse_length =
        1.0f / std::sqrt(axis_x * axis_x + axis_y * axis_y + axis_z * axis_z);
    axis_x = axis_x * inverse_length;
    axis_y = axis_y * inverse_length;
    axis_z = axis_z * inverse_length;
  }

  const float cosine = std::cos(radians);
  const float sine = std::sin(radians);
  const float one_minus_cosine = 1.0f - cosine;
  const float xz_one_minus_cosine = axis_x * axis_z * one_minus_cosine;
  const float xy_one_minus_cosine = axis_x * axis_y * one_minus_cosine;
  const float yz_one_minus_cosine = axis_z * axis_y * one_minus_cosine;

  return RenderMatrix4x4{
      axis_x * axis_x * one_minus_cosine + cosine,
      sine * axis_z + xy_one_minus_cosine,
      xz_one_minus_cosine - sine * axis_y,
      0.0f,
      xy_one_minus_cosine - sine * axis_z,
      axis_y * axis_y * one_minus_cosine + cosine,
      axis_x * sine + yz_one_minus_cosine,
      0.0f,
      sine * axis_y + xz_one_minus_cosine,
      yz_one_minus_cosine - axis_x * sine,
      axis_z * axis_z * one_minus_cosine + cosine,
      0.0f,
      0.0f,
      0.0f,
      0.0f,
      1.0f,
  };
}

RenderMatrix4x4 BuildRotationMatrix4x4Quaternion(
    const RenderVec4View quaternion) noexcept {
  const float x = quaternion[0];
  const float y = quaternion[1];
  const float z = quaternion[2];
  const float w = quaternion[3];
  const float two_y = y + y;
  const float two_wx = (x + x) * w;
  const float two_z = z + z;
  const float two_xx = (x + x) * x;

  return RenderMatrix4x4{
      1.0f - (two_y * y + z * two_z),
      w * two_z + x * two_y,
      x * two_z - two_y * w,
      0.0f,
      x * two_y - w * two_z,
      1.0f - (z * two_z + two_xx),
      two_wx + y * two_z,
      0.0f,
      two_y * w + x * two_z,
      y * two_z - two_wx,
      1.0f - (two_y * y + two_xx),
      0.0f,
      0.0f,
      0.0f,
      0.0f,
      1.0f,
  };
}

RenderMatrix3x3 BuildRotationMatrix3x3Quaternion(
    const RenderVec4View quaternion) noexcept {
  const float x = quaternion[0];
  const float y = quaternion[1];
  const float z = quaternion[2];
  const float w = quaternion[3];
  const float two_x = x * 2.0f;
  const float two_y = y * 2.0f;
  const float two_z = z * 2.0f;
  const float two_xx = two_x * x;
  const float two_xy = x * two_y;
  const float two_xz = x * two_z;
  const float two_yy = two_y * y;
  const float two_yz = y * two_z;
  const float two_zz = two_z * z;
  const float two_wx = w * two_x;
  const float two_wy = w * two_y;
  const float two_wz = w * two_z;

  return RenderMatrix3x3{
      1.0f - (two_zz + two_yy), two_xy + two_wz, two_xz - two_wy,
      two_xy - two_wz, 1.0f - (two_zz + two_xx), two_yz + two_wx,
      two_wy + two_xz, two_yz - two_wx, 1.0f - (two_xx + two_yy),
  };
}

RenderMatrix4x4 PrependRotationMatrix4x4X(
    const RenderMatrix4x4View matrix, const float radians) noexcept {
  return MultiplyMatrix4x4(BuildRotationMatrix4x4X(radians), matrix);
}

RenderMatrix4x4 PrependRotationMatrix4x4Y(
    const RenderMatrix4x4View matrix, const float radians) noexcept {
  return MultiplyMatrix4x4(BuildRotationMatrix4x4Y(radians), matrix);
}

RenderMatrix4x4 PrependRotationMatrix4x4Z(
    const RenderMatrix4x4View matrix, const float radians) noexcept {
  return MultiplyMatrix4x4(BuildRotationMatrix4x4Z(radians), matrix);
}

RenderMatrix4x4 PrependRotationMatrix4x4Quaternion(
    const RenderMatrix4x4View matrix, const RenderVec4View quaternion) noexcept {
  return MultiplyMatrix4x4(BuildRotationMatrix4x4Quaternion(quaternion), matrix);
}

}

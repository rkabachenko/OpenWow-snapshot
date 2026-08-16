#pragma once

#include "openwow/render/api/math/render_math_types.h"

#include <optional>

namespace openwow::render {

enum class RotationAxisNormalization {
  kNormalize,
  kAlreadyNormalized,
};

struct RenderProjectionViewport {
  float left{0.0f};
  float top{0.0f};
  float right{1.0f};
  float bottom{1.0f};
};

[[nodiscard]] RenderMatrix4x4 AddMatrix4x4(RenderMatrix4x4View lhs,
                                           RenderMatrix4x4View rhs) noexcept;

[[nodiscard]] RenderMatrix4x4 ScaleMatrix4x4(RenderMatrix4x4View matrix, float scale) noexcept;

[[nodiscard]] RenderMatrix4x4 ScaleMatrix4x4BasisRows(
    RenderMatrix4x4View matrix, RenderVec3View scale) noexcept;

[[nodiscard]] RenderMatrix4x4 PrependMatrix4x4Translation(
    RenderMatrix4x4View matrix, RenderVec3View translation) noexcept;

[[nodiscard]] RenderMatrix4x4 TransposeMatrix4x4(RenderMatrix4x4View matrix) noexcept;

[[nodiscard]] double DeterminantMatrix4x4(RenderMatrix4x4View matrix) noexcept;

[[nodiscard]] RenderMatrix4x4 AdjugateMatrix4x4(RenderMatrix4x4View matrix) noexcept;

[[nodiscard]] RenderMatrix4x4 InvertMatrix4x4WithDeterminant(
    RenderMatrix4x4View matrix, float determinant) noexcept;

[[nodiscard]] RenderMatrix4x4 InvertMatrix4x4(RenderMatrix4x4View matrix) noexcept;

[[nodiscard]] RenderMatrix4x4 MultiplyMatrix4x4(RenderMatrix4x4View lhs,
                                                RenderMatrix4x4View rhs) noexcept;

[[nodiscard]] RenderVec4 TransformRowVector4x4(RenderVec4View vector,
                                               RenderMatrix4x4View matrix) noexcept;

[[nodiscard]] RenderVec3 TransformAffinePoint4x4(
    RenderVec3View point, RenderMatrix4x4View matrix) noexcept;

[[nodiscard]] RenderVec3 ExtractCameraPositionFromRetailViewMatrix(
    RenderMatrix4x4View view) noexcept;

[[nodiscard]] RenderFrustumCorners ComputeFrustumCornersFromViewProjection(
    RenderMatrix4x4View view_matrix,
    RenderMatrix4x4View projection_matrix) noexcept;

[[nodiscard]] std::optional<RenderViewportRayEndpoints>
ComputeNormalizedViewportRayEndpoints(
    float normalized_x, float normalized_y,
    const RenderFrustumCorners& frustum_corners) noexcept;

[[nodiscard]] RenderScreenProjection ProjectWorldPointToViewport(
    RenderVec3View world_point, RenderMatrix4x4View view_matrix,
    RenderMatrix4x4View projection_matrix,
    const RenderProjectionViewport& viewport) noexcept;

[[nodiscard]] RenderMatrix4x4 BuildRotationMatrix4x4X(float radians) noexcept;
[[nodiscard]] RenderMatrix4x4 BuildRotationMatrix4x4Y(float radians) noexcept;
[[nodiscard]] RenderMatrix4x4 BuildRotationMatrix4x4Z(float radians) noexcept;

[[nodiscard]] RenderMatrix4x4 BuildRotationMatrix4x4AxisAngle(
    float radians, RenderVec3View axis,
    RotationAxisNormalization normalization) noexcept;

[[nodiscard]] RenderMatrix4x4 BuildRotationMatrix4x4Quaternion(
    RenderVec4View quaternion) noexcept;

[[nodiscard]] RenderMatrix3x3 BuildRotationMatrix3x3Quaternion(
    RenderVec4View quaternion) noexcept;

[[nodiscard]] RenderMatrix4x4 PrependRotationMatrix4x4X(
    RenderMatrix4x4View matrix, float radians) noexcept;
[[nodiscard]] RenderMatrix4x4 PrependRotationMatrix4x4Y(
    RenderMatrix4x4View matrix, float radians) noexcept;
[[nodiscard]] RenderMatrix4x4 PrependRotationMatrix4x4Z(
    RenderMatrix4x4View matrix, float radians) noexcept;

[[nodiscard]] RenderMatrix4x4 PrependRotationMatrix4x4Quaternion(
    RenderMatrix4x4View matrix, RenderVec4View quaternion) noexcept;

}

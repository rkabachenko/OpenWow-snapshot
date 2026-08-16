#include "openwow/render/m2/m2_camera_math.h"

#include "openwow/foundation/math/float_compare.h"
#include "openwow/foundation/math/row_major_mat4x4.h"
#include "openwow/foundation/math/vec3_cross.h"
#include "openwow/foundation/math/vec3_normalize.h"
#include "openwow/render/api/math/render_matrix_math.h"

#include <cmath>

namespace openwow::render::m2 {

namespace {

constexpr float kRotationBasisEpsilon = 1.0e-8f;

[[nodiscard]] bool NormalizeBasisVector(RenderVec3& vector) {
  const float length_squared = vector[0] * vector[0] +
                               vector[1] * vector[1] +
                               vector[2] * vector[2];
  if (length_squared <= kRotationBasisEpsilon) {
    return false;
  }
  const float inverse_length = 1.0f / std::sqrt(length_squared);
  vector[0] *= inverse_length;
  vector[1] *= inverse_length;
  vector[2] *= inverse_length;
  return true;
}

[[nodiscard]] RenderMatrix4x4 ExtractRowNormalizedRotation(
    const RenderMatrix4x4View matrix) {
  RenderVec3 rows[3] = {
      RenderVec3{matrix[0], matrix[1], matrix[2]},
      RenderVec3{matrix[4], matrix[5], matrix[6]},
      RenderVec3{matrix[8], matrix[9], matrix[10]},
  };

  for (int row = 0; row < 3; ++row) {
    if (!NormalizeBasisVector(rows[row])) {
      rows[row] = {row == 0 ? 1.0f : 0.0f, row == 1 ? 1.0f : 0.0f,
                   row == 2 ? 1.0f : 0.0f};
    }
  }

  return RenderMatrix4x4{
      rows[0][0], rows[0][1], rows[0][2], 0.0f,
      rows[1][0], rows[1][1], rows[1][2], 0.0f,
      rows[2][0], rows[2][1], rows[2][2], 0.0f,
      0.0f,       0.0f,       0.0f,       1.0f,
  };
}

}

M2CameraBasis BuildM2CameraBasis(const M2CameraPose &state) {
  M2CameraBasis basis;
  basis.forward[0] = state.target[0] - state.position[0];
  basis.forward[1] = state.target[1] - state.position[1];
  basis.forward[2] = state.target[2] - state.position[2];
  openwow::math::vec3::NormalizeInPlace(basis.forward.data());

  const RenderVec3 reference_up{
      0.0f,
      -std::sin(state.roll_rad),
      std::cos(state.roll_rad),
  };

  const float dot = reference_up[0] * basis.forward[0] + reference_up[1] * basis.forward[1] +
                    reference_up[2] * basis.forward[2];
  if (std::fabs(std::fabs(dot) - 1.0f)
      < openwow::math::float_compare::kClientFloatEpsilon) {
    basis.right[0] = 1.0f;
    basis.right[1] = 0.0f;
    basis.right[2] = 0.0f;
  } else {
    openwow::math::vec3::Cross(basis.right.data(), basis.forward.data(), reference_up.data());
    openwow::math::vec3::NormalizeInPlace(basis.right.data());
  }

  openwow::math::vec3::Cross(basis.up.data(), basis.right.data(), basis.forward.data());
  openwow::math::vec3::NormalizeInPlace(basis.up.data());

  return basis;
}

float BuildM2CameraVerticalFov(const float fov_rad, const float aspect_ratio) {
  const float authored_fov = fov_rad > 0.0f ? fov_rad : kM2DefaultCameraFovRad;
  const float safe_aspect = aspect_ratio > 0.0f ? aspect_ratio : 1.0f;

  return authored_fov / std::sqrt(safe_aspect * safe_aspect + 1.0f);
}

M2CameraPose TransformM2CameraPoseByModelMatrix(
    const M2CameraPose &state, const RenderMatrix4x4View model_matrix) {
  M2CameraPose transformed = state;
  openwow::math::row_major_mat4x4::TransformPointByRowMajorAffine4x4(
      transformed.position.data(), state.position.data(), model_matrix.data());
  openwow::math::row_major_mat4x4::TransformPointByRowMajorAffine4x4(
      transformed.target.data(), state.target.data(), model_matrix.data());
  return transformed;
}

RenderMatrix4x4 BuildM2CameraViewMatrix(const M2CameraPose &state) {
  const M2CameraBasis basis = BuildM2CameraBasis(state);

  RenderMatrix4x4 view{};
  view[0] = basis.right[0];
  view[1] = basis.up[0];
  view[2] = basis.forward[0];
  view[3] = 0.0f;
  view[4] = basis.right[1];
  view[5] = basis.up[1];
  view[6] = basis.forward[1];
  view[7] = 0.0f;
  view[8] = basis.right[2];
  view[9] = basis.up[2];
  view[10] = basis.forward[2];
  view[11] = 0.0f;

  view[12] = -(basis.right[0] * state.position[0] +
               basis.right[1] * state.position[1] +
               basis.right[2] * state.position[2]);
  view[13] = -(basis.up[0] * state.position[0] +
               basis.up[1] * state.position[1] +
               basis.up[2] * state.position[2]);
  view[14] = -(basis.forward[0] * state.position[0] +
               basis.forward[1] * state.position[1] +
               basis.forward[2] * state.position[2]);
  view[15] = 1.0f;
  return view;
}

RenderMatrix4x4 BuildM2BillboardInverseModelViewRotation(
    const RenderMatrix4x4View model_matrix,
    const RenderMatrix4x4View view_matrix) {
  const RenderMatrix4x4 model_rotation =
      ExtractRowNormalizedRotation(model_matrix);
  const RenderMatrix4x4 view_rotation =
      ExtractRowNormalizedRotation(view_matrix);
  const RenderMatrix4x4 model_view_rotation = MultiplyMatrix4x4(
      model_rotation, view_rotation);
  RenderMatrix4x4 inverse = TransposeMatrix4x4(model_view_rotation);
  inverse[3] = 0.0f;
  inverse[7] = 0.0f;
  inverse[11] = 0.0f;
  inverse[12] = 0.0f;
  inverse[13] = 0.0f;
  inverse[14] = 0.0f;
  inverse[15] = 1.0f;
  return inverse;
}

}

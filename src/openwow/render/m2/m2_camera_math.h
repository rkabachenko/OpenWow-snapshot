#pragma once

#include "openwow/render/m2/m2_public_types.h"
#include "openwow/render/api/math/render_math_types.h"

namespace openwow::render::m2 {

[[nodiscard]] M2CameraBasis BuildM2CameraBasis(const M2CameraPose &state);

[[nodiscard]] float BuildM2CameraVerticalFov(float fov_rad, float aspect_ratio);

[[nodiscard]] M2CameraPose TransformM2CameraPoseByModelMatrix(
    const M2CameraPose &state, RenderMatrix4x4View model_matrix);

[[nodiscard]] RenderMatrix4x4 BuildM2CameraViewMatrix(const M2CameraPose &state);

[[nodiscard]] RenderMatrix4x4 BuildM2BillboardInverseModelViewRotation(
    RenderMatrix4x4View model_matrix, RenderMatrix4x4View view_matrix);

}

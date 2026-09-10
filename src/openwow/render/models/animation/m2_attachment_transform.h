#pragma once

#include "openwow/render/api/math/render_math_types.h"

#include <cstdint>

namespace openwow::render {

enum class AttachmentOrientationMode : std::uint32_t {
    kNone         = 0,
    kCrossProduct = 1,
    kPassthrough  = 2,
    kGravityAlign = 3,
};

void AlignMatrixBasisToGravityVector(RenderMatrix4x4& matrix, RenderVec3View up_vector);

void BuildCrossProductOrientation(RenderMatrix4x4& matrix, RenderVec3View up_vector);

[[nodiscard]] float M2SequenceGroundAlignmentWeight(
    std::uint32_t flags, std::uint32_t time_ms, std::uint32_t duration_ms,
    float playback_speed);

void ApplyM2SequenceGroundAlignment(
    RenderMatrix4x4& matrix, float facing, float scale,
    RenderVec3View up_vector, float weight);

[[nodiscard]] RenderMatrix4x4 BuildM2AttachmentTransformMatrix(
    RenderVec3View position,
    float facing,
    float scale,
    RenderVec3View up_vector,
    AttachmentOrientationMode orientation_mode);

}

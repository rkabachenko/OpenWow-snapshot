
#include "openwow/render/models/animation/m2_attachment_transform.h"

#include "openwow/foundation/math/vec3_normalize_if_length_squared_exceeds_client_epsilon.h"
#include "openwow/render/api/math/render_matrix_math.h"

#include <algorithm>

namespace openwow::render {

namespace {

void ScaleMatrixBasisVectorsUniform(RenderMatrix4x4& matrix, const float s) {
    matrix[0]  *= s;
    matrix[1]  *= s;
    matrix[2]  *= s;
    matrix[4]  *= s;
    matrix[5]  *= s;
    matrix[6]  *= s;
    matrix[8]  *= s;
    matrix[9]  *= s;
    matrix[10] *= s;
}

}

void AlignMatrixBasisToGravityVector(RenderMatrix4x4& matrix, const RenderVec3View up_vector) {

    matrix[4] = up_vector[1] * matrix[2] - up_vector[2] * matrix[1];
    matrix[5] = up_vector[2] * matrix[0] - up_vector[0] * matrix[2];
    matrix[6] = up_vector[0] * matrix[1] - up_vector[1] * matrix[0];

    math::vec3::NormalizeInPlaceIfLengthSquaredExceedsClientEpsilon(
        &matrix[4]);

    matrix[0] = matrix[5] * up_vector[2] - up_vector[1] * matrix[6];
    matrix[1] = up_vector[0] * matrix[6] - matrix[4] * up_vector[2];
    matrix[2] = matrix[4] * up_vector[1] - up_vector[0] * matrix[5];

    matrix[8]  = up_vector[0];
    matrix[9]  = up_vector[1];
    matrix[10] = up_vector[2];
}

void BuildCrossProductOrientation(RenderMatrix4x4& matrix, const RenderVec3View up_vector) {

    const float neg_row0_y = -matrix[1];
    const float row0_x     = matrix[0];
    matrix[4] = neg_row0_y;
    matrix[5] = row0_x;
    matrix[6] = 0.0f;
    math::vec3::NormalizeInPlaceIfLengthSquaredExceedsClientEpsilon(
        &matrix[4]);

    matrix[0] = matrix[5] * up_vector[2] - up_vector[1] * matrix[6];
    matrix[1] = up_vector[0] * matrix[6] - matrix[4] * up_vector[2];
    matrix[2] = up_vector[1] * matrix[4] - up_vector[0] * matrix[5];
    math::vec3::NormalizeInPlaceIfLengthSquaredExceedsClientEpsilon(
        &matrix[0]);

    matrix[8]  = matrix[1] * matrix[6] - matrix[5] * matrix[2];
    matrix[9]  = matrix[4] * matrix[2] - matrix[6] * matrix[0];
    matrix[10] = matrix[5] * matrix[0] - matrix[1] * matrix[4];
}

RenderMatrix4x4 BuildM2AttachmentTransformMatrix(
    const RenderVec3View position,
    const float facing,
    const float scale,
    const RenderVec3View up_vector,
    const AttachmentOrientationMode orientation_mode) {
    RenderMatrix4x4 matrix{kRenderIdentityMatrix4x4};

    matrix = PrependRotationMatrix4x4Z(matrix, facing);

    matrix[12] = position[0];
    matrix[13] = position[1];
    matrix[14] = position[2];

    switch (orientation_mode) {
        case AttachmentOrientationMode::kCrossProduct:
            BuildCrossProductOrientation(matrix, up_vector);
            break;
        case AttachmentOrientationMode::kGravityAlign:
            AlignMatrixBasisToGravityVector(matrix, up_vector);
            break;
        case AttachmentOrientationMode::kNone:
        case AttachmentOrientationMode::kPassthrough:
        default:
            break;
    }

    ScaleMatrixBasisVectorsUniform(matrix, scale);
    return matrix;
}

float M2SequenceGroundAlignmentWeight(
    const std::uint32_t flags, const std::uint32_t time_ms,
    const std::uint32_t duration_ms, const float playback_speed) {
    const auto mode = flags & 0xEu;
    if (mode == 8u) return 1.0f;
    if (mode != 2u && mode != 4u) return 0.0f;

    float weight = 0.0f;
    if (playback_speed == 0.0f) {
        weight = 1.0f;
    } else if (playback_speed > 0.0f) {
        const auto half_duration_ms = duration_ms / 2u;
        weight = half_duration_ms == 0u
                     ? 1.0f
                     : std::clamp(static_cast<float>(time_ms) /
                                      static_cast<float>(half_duration_ms),
                                  0.0f, 1.0f);
    }
    return mode == 4u ? 1.0f - weight : weight;
}

void ApplyM2SequenceGroundAlignment(
    RenderMatrix4x4& matrix, const float facing, const float scale,
    const RenderVec3View up_vector, const float weight) {
    if (weight <= 0.0f) return;
    const RenderVec3 position{matrix[12], matrix[13], matrix[14]};
    const auto aligned = BuildM2AttachmentTransformMatrix(
        RenderVec3View{position}, facing, scale, up_vector,
        AttachmentOrientationMode::kGravityAlign);
    // Blend basis components with the model's ordinary orientation. Translation
    // and the authored animation pose retain their separate owners.
    for (const auto index : {0u, 1u, 2u, 4u, 5u, 6u, 8u, 9u, 10u}) {
        matrix[index] = matrix[index] * (1.0f - weight) + aligned[index] * weight;
    }
}

}

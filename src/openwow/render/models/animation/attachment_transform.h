#pragma once

#include "openwow/render/models/animation/attachment_graph.h"
#include "openwow/render/api/math/render_math_types.h"

#include <algorithm>
#include <optional>

namespace openwow::render {

inline AttachmentTransform BuildAttachmentTransformFromRowMajorAffine4x4(
    const RenderMatrix4x4View m) {
  AttachmentTransform t;
  std::copy(m.begin(), m.end(), t.local_matrix.m);
  return t;
}

inline AttachmentTransform BuildAttachmentTransformFromBoneRowMajorAffine4x4(
    const RenderMatrix4x4View bone_matrix,
    const std::optional<RenderVec3View> local_position = std::nullopt) {
  RenderMatrix4x4 attachment_matrix{};
  std::copy(bone_matrix.begin(), bone_matrix.end(), attachment_matrix.begin());

  if (local_position.has_value()) {
    attachment_matrix[12] += attachment_matrix[0] * (*local_position)[0] +
                             attachment_matrix[4] * (*local_position)[1] +
                             attachment_matrix[8] * (*local_position)[2];
    attachment_matrix[13] += attachment_matrix[1] * (*local_position)[0] +
                             attachment_matrix[5] * (*local_position)[1] +
                             attachment_matrix[9] * (*local_position)[2];
    attachment_matrix[14] += attachment_matrix[2] * (*local_position)[0] +
                             attachment_matrix[6] * (*local_position)[1] +
                             attachment_matrix[10] * (*local_position)[2];
  }

  return BuildAttachmentTransformFromRowMajorAffine4x4(RenderMatrix4x4View{attachment_matrix});
}

}

#pragma once

#include "openwow/render/models/animation/attachment_transform.h"
#include "openwow/render/m2/m2_system.h"

#include <cstddef>
#include <optional>
#include <span>

namespace openwow::render::glue {

[[nodiscard]] inline std::optional<AttachmentTransform>
ResolveGlueAttachmentLinkTransform(
    const m2::M2AttachmentInfoQuery& attachment,
    const std::span<const float> sampled_bone_matrices) {
  constexpr std::size_t kMatrixFloatCount = 16u;
  const std::size_t bone_count = sampled_bone_matrices.size() / kMatrixFloatCount;

  if (attachment.status != m2::M2ResultStatus::kReady) {
    return std::nullopt;
  }

  const std::size_t bone_index =
      static_cast<std::size_t>(attachment.attachment.bone_index);
  if (bone_index >= bone_count) {
    return std::nullopt;
  }
  return BuildAttachmentTransformFromBoneRowMajorAffine4x4(
      RenderMatrix4x4View{sampled_bone_matrices.data() + bone_index * kMatrixFloatCount,
                          kMatrixFloatCount},
      RenderVec3View{attachment.attachment.local_position});
}

}

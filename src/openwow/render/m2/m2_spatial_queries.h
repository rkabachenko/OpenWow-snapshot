#pragma once

#include "openwow/render/m2/m2_model_queries.h"
#include "openwow/render/m2/m2_instance_store.h"
#include "openwow/render/scene/selection_triangle.h"

#include <vector>

namespace openwow::render::m2 {

enum class M2CameraLookupKind : std::uint8_t {
  kIndex,
  kType,
};

class M2SpatialQueries {
 public:
  M2SpatialQueries(M2SystemMutex& mutex,
                    const M2ModelStore& models,
                   const M2InstanceStore& instances,
                   const data::dbc::DbcLoader* const& dbc) noexcept;

  [[nodiscard]] M2InstanceReadinessQuery QueryReadiness(
      std::uint32_t instance_id) const;
  [[nodiscard]] M2InstanceAnimationInfoQuery QueryAnimationInfo(
      std::uint32_t instance_id) const;
  [[nodiscard]] M2InstanceEventQuery QueryEvent(
      std::uint32_t instance_id, std::uint32_t animation_id,
      std::uint32_t event_identifier) const;
  [[nodiscard]] M2InstanceSpatialInfoQuery QuerySpatialInfo(
      std::uint32_t instance_id) const;
  [[nodiscard]] M2SegmentIntersectionQuery QueryClosestSegmentIntersection(
      std::uint32_t instance_id, const RenderVec3& segment_start,
      const RenderVec3& segment_end) const;
  [[nodiscard]] bool ModelHasAnimation(std::uint32_t instance_id,
                                       std::uint32_t animation_id) const;
  [[nodiscard]] M2BoneMatricesQuery QueryBoneMatrices(
      std::uint32_t instance_id) const;
  [[nodiscard]] M2SampleBoneMatricesQuery QuerySampleBoneMatrices(
      std::uint32_t instance_id,
      const std::optional<RenderMatrix4x4View>& camera_inverse_view) const;
  [[nodiscard]] M2CameraSampleQuery QueryCameraSample(
      std::uint32_t instance_id, int camera_selector,
      M2CameraLookupKind lookup_kind) const;
  [[nodiscard]] M2LightSampleQuery QueryLightSample(
      std::uint32_t instance_id, int light_index,
      std::span<const float> bone_matrices) const;
  [[nodiscard]] std::vector<M2TriggeredEvent> CollectTriggeredEvents(
      std::uint32_t instance_id, std::uint32_t previous_time_ms,
      std::uint32_t current_time_ms, std::span<const float> bone_matrices,
      const std::optional<RenderMatrix4x4>& model_matrix) const;
  [[nodiscard]] M2KeyBoneQuery QueryKeyBone(
      std::uint32_t instance_id, std::uint32_t key_bone_lookup_index) const;
  [[nodiscard]] M2AttachmentInfoQuery QueryAttachmentInfo(
      std::uint32_t instance_id, std::uint32_t attachment_lookup_index) const;
  [[nodiscard]] M2AttachmentPositionQuery QueryAttachmentPosition(
      std::uint32_t instance_id, std::uint32_t attachment_lookup_index,
      const std::optional<RenderVec3>& local_offset) const;
  [[nodiscard]] M2AttachmentTransformQuery QueryAttachmentTransformMatrix(
      std::uint32_t instance_id, std::uint32_t attachment_lookup_index) const;

  void QueryAttachmentPlacements(
      std::span<const M2AttachmentPlacementRequest> requests,
      std::span<M2AttachmentPlacementQuery> out_results) const;
  [[nodiscard]] M2ModelWorldPointQuery QueryModelWorldPoint(
      std::uint32_t instance_id) const;
  [[nodiscard]] M2ModelWorldTransformMatrixQuery QueryModelWorldTransformMatrix(
      std::uint32_t instance_id) const;
  [[nodiscard]] M2RootBoneWorldMatrixQuery QueryRootBoneWorldMatrix(
      std::uint32_t instance_id) const;
  [[nodiscard]] std::optional<SelectionTriangleVertices>
  BuildSelectionTriangleVerticesForSample(
      std::uint32_t model_id, const std::vector<float>& bone_matrices,
      std::size_t bone_count, const RenderMatrix4x4& model_matrix) const;
  [[nodiscard]] M2WorldBoundingSphereQuery QueryWorldBoundingSphere(
      std::uint32_t instance_id) const;
  [[nodiscard]] M2WorldBoundingBoxQuery QueryWorldBoundingBox(
      std::uint32_t instance_id) const;
  [[nodiscard]] M2ModelBoundingSphereQuery QueryModelBoundingSphere(
      std::uint32_t instance_id) const;

  [[nodiscard]] M2InstanceReadinessQuery QueryReadinessOfLocked(
      std::uint32_t instance_id, const detail::M2Instance& instance) const;

  [[nodiscard]] static M2InstanceAnimationInfo BuildAnimationInfoLocked(
      const detail::M2Instance& instance,
      const detail::M2ModelResource& resource) noexcept;

  [[nodiscard]] static M2ModelSpatialInfo BuildSpatialInfoLocked(
      const detail::M2Instance& instance,
      const detail::M2ModelResource& resource);

 private:
  [[nodiscard]] M2InstanceReadinessQuery QueryReadinessLocked(
      std::uint32_t instance_id, std::vector<std::uint32_t>& visited) const;

  [[nodiscard]] M2InstanceReadinessQuery QueryReadinessResolvedLocked(
      const detail::M2Instance& instance,
      std::vector<std::uint32_t>& visited) const;

  [[nodiscard]] M2InstanceReadinessQuery QueryReadinessScratchLocked(
      std::uint32_t instance_id) const;

  [[nodiscard]] M2AttachmentTransformQuery QueryAttachmentTransformMatrixLocked(
      std::uint32_t instance_id, std::uint32_t attachment_lookup_index) const;

  M2SystemMutex& mutex_;
  const M2ModelStore& models_;
  const M2InstanceStore& instances_;
  const data::dbc::DbcLoader* const& dbc_;

  mutable std::vector<std::uint32_t> readiness_visited_scratch_;
};

[[nodiscard]] RenderMatrix4x4 ComputeM2ModelMatrix(
    const detail::M2Instance& instance);
[[nodiscard]] int ResolveM2RenderableAnimationIndex(
    const detail::M2Instance& instance);
[[nodiscard]] const std::vector<float>* SampleM2InstanceBoneMatricesCached(
    const detail::M2Instance& instance,
    const detail::M2ModelResource& resource,
    const std::optional<RenderMatrix4x4View>& camera_inverse_view = std::nullopt);
[[nodiscard]] std::optional<std::vector<float>>
ComputeM2InstanceBoneMatricesForQuery(
    const detail::M2Instance& instance,
    const detail::M2ModelResource& resource,
    const std::optional<RenderMatrix4x4View>& camera_inverse_view = std::nullopt);

}

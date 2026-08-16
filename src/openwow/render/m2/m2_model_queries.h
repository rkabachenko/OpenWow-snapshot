#pragma once

#include "openwow/render/m2/m2_model_repository.h"
#include "openwow/render/m2/m2_runtime_state.h"

#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_map>

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::render::m2 {

using M2ModelStore =
    std::unordered_map<std::uint32_t, std::unique_ptr<detail::M2ModelResource>>;

class M2ModelQueries {
 public:
  M2ModelQueries(M2SystemMutex& mutex,
                  const M2ModelStore& models,
                 const data::dbc::DbcLoader* const& dbc) noexcept;

  [[nodiscard]] M2ModelReadinessQuery QueryReadiness(std::uint32_t model_id) const;
  [[nodiscard]] M2FirstAnimationDurationQuery QueryFirstAnimationDuration(
      std::uint32_t model_id) const;
  [[nodiscard]] M2ModelAnimationSequenceQuery QueryAnimationSequence(
      std::uint32_t model_id, std::uint32_t animation_id,
      std::int32_t sub_animation_index) const;

  [[nodiscard]] M2ModelAnimationSequenceQuery QueryAnimationSequenceLocked(
      const detail::M2ModelResource& resource, std::uint32_t animation_id,
      std::int32_t sub_animation_index) const;
  [[nodiscard]] M2ModelCollisionGeometryQuery QueryCollisionGeometry(
      std::uint32_t model_id) const;
  [[nodiscard]] M2AttachmentInfoQuery QueryAttachmentInfo(
      std::uint32_t model_id, std::uint32_t attachment_lookup_index) const;
  [[nodiscard]] M2ModelSpatialInfoQuery QuerySpatialInfo(
      std::uint32_t model_id, std::uint16_t sequence_index) const;
  [[nodiscard]] M2ModelGeometryInfoQuery QueryGeometryInfo(
      std::uint32_t model_id) const;
  [[nodiscard]] M2ModelInfoQuery QueryInfo(std::uint32_t model_id) const;
  [[nodiscard]] M2ModelAnimationListQuery QueryAnimationList(
      std::uint32_t model_id) const;
  [[nodiscard]] M2ModelTextureDependenciesQuery QueryTextureDependencies(
      std::uint32_t model_id) const;
  [[nodiscard]] M2ModelSubmeshSectionIdsQuery QuerySubmeshSectionIds(
      std::uint32_t model_id) const;
  [[nodiscard]] M2ModelLightCountQuery QueryLightCount(std::uint32_t model_id) const;
  [[nodiscard]] bool HasSubmeshId(std::uint32_t model_id,
                                  std::uint16_t submesh_id) const;

  [[nodiscard]] bool ContainsAnimation(std::uint32_t model_id,
                                       std::uint32_t animation_id) const;
  [[nodiscard]] bool HasAnimation(std::uint32_t model_id,
                                  std::uint32_t animation_id) const;
  [[nodiscard]] M2SampleBoneMatricesQuery QuerySampleBoneMatrices(
      std::uint32_t model_id, int sequence_index, std::uint32_t time_ms,
      const std::optional<RenderMatrix4x4View>& camera_inverse_view) const;
  [[nodiscard]] M2CameraSampleQuery QueryCameraSample(
      std::uint32_t model_id, int camera_index, int sequence_index,
      std::uint32_t time_ms) const;
  [[nodiscard]] M2LightSampleQuery QueryLightSample(
      std::uint32_t model_id, int light_index, int sequence_index,
      std::uint32_t time_ms, std::span<const float> bone_matrices) const;
  [[nodiscard]] std::vector<M2TriggeredEvent> CollectTriggeredEvents(
      std::uint32_t model_id, int sequence_index,
      std::uint32_t previous_time_ms, std::uint32_t current_time_ms,
      std::span<const float> bone_matrices,
      const std::optional<RenderMatrix4x4>& model_matrix) const;

 private:
  M2SystemMutex& mutex_;
  const M2ModelStore& models_;
  const data::dbc::DbcLoader* const& dbc_;
};

}

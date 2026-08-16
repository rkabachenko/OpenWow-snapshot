#include "openwow/render/m2/m2_model_queries.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/render/m2/m2_animation_selection.h"
#include "openwow/render/m2/m2_animator.h"
#include "openwow/render/m2/m2_material_pipeline.h"
#include "openwow/render/m2/m2_texture_unit_preparation.h"

#include <algorithm>
#include <limits>
#include <string>

namespace openwow::render::m2 {
namespace {

std::optional<M2AnimationAliasInfo> LookupAnimationAliasInfo(
    const std::uint32_t animation_id, const data::dbc::DbcLoader* dbc) {
  if (dbc == nullptr) {
    return std::nullopt;
  }
  const auto* entry = dbc->animation_data().LookupEntry(animation_id);
  if (entry == nullptr) {
    return std::nullopt;
  }
  return M2AnimationAliasInfo{.flags = entry->flags,
                              .target_animation_id = entry->fallback};
}

RenderAabb MakeAabb(const RenderVec3View min_xyz,
                    const RenderVec3View max_xyz) noexcept {
  return {min_xyz[0], min_xyz[1], min_xyz[2],
          max_xyz[0], max_xyz[1], max_xyz[2]};
}

RenderVec3 AabbCenter(const RenderAabbView aabb) noexcept {
  return {(aabb[0] + aabb[3]) * 0.5f,
          (aabb[1] + aabb[4]) * 0.5f,
          (aabb[2] + aabb[5]) * 0.5f};
}

RenderAabb HeaderBoundingBox(const data::model::M2Header& header) noexcept {
  return MakeAabb(RenderVec3View{header.bounding_box_min},
                  RenderVec3View{header.bounding_box_max});
}

bool HasStrictBounds(const RenderVec3View min_xyz,
                     const RenderVec3View max_xyz) noexcept {
  return min_xyz[0] < max_xyz[0] && min_xyz[1] < max_xyz[1] &&
         min_xyz[2] < max_xyz[2];
}

M2ModelSpatialInfo BuildM2ModelSpatialInfo(
    const detail::M2ModelResource& resource,
    const std::uint16_t sequence_index) noexcept {
  M2ModelSpatialInfo info{};
  info.local_bounds = HeaderBoundingBox(resource.model_data.header);
  const RenderVec3 header_center = AabbCenter(RenderAabbView{info.local_bounds});
  info.local_bounding_sphere = {header_center[0], header_center[1], header_center[2],
                                resource.model_data.header.bounding_sphere_radius};
  if (sequence_index != kInvalidM2AnimationSequenceIndex &&
      static_cast<std::size_t>(sequence_index) <
          resource.model_data.animation_sequences.size()) {
    const auto& sequence = resource.model_data.animation_sequences[sequence_index];
    if (HasStrictBounds(RenderVec3View{sequence.bounding_box_min},
                        RenderVec3View{sequence.bounding_box_max})) {
      info.local_bounds = MakeAabb(RenderVec3View{sequence.bounding_box_min},
                                   RenderVec3View{sequence.bounding_box_max});
      const RenderVec3 center = AabbCenter(RenderAabbView{info.local_bounds});
      info.local_bounding_sphere = {center[0], center[1], center[2],
                                    sequence.bounding_radius};
    }
  }
  return info;
}

struct ResolvedAttachment {
  const data::model::M2Attachment* attachment = nullptr;
  std::uint16_t attachment_index = std::numeric_limits<std::uint16_t>::max();
};

ResolvedAttachment ResolveAttachmentLookup(
    const data::model::M2Model& model,
    const std::uint32_t attachment_lookup_index) noexcept {
  if (attachment_lookup_index >= model.attachment_lookups.size()) {
    return {};
  }
  const std::int16_t index = model.attachment_lookups[attachment_lookup_index];
  if (index < 0 || static_cast<std::size_t>(index) >= model.attachments.size()) {
    return {};
  }
  return {.attachment = &model.attachments[static_cast<std::size_t>(index)],
          .attachment_index = static_cast<std::uint16_t>(index)};
}

RenderVec3 AttachmentLocalPosition(
    const data::model::M2Attachment& attachment) noexcept {
  return {attachment.position[0], attachment.position[1], attachment.position[2]};
}

}

M2ModelQueries::M2ModelQueries(M2SystemMutex& mutex,
                               const M2ModelStore& models,
                               const data::dbc::DbcLoader* const& dbc) noexcept
    : mutex_(mutex), models_(models), dbc_(dbc) {}

M2ModelReadinessQuery M2ModelQueries::QueryReadiness(
    const std::uint32_t model_id) const {
  std::lock_guard lock(mutex_);
  const auto it = models_.find(model_id);
  if (it == models_.end()) {
    return {.status = M2ResultStatus::kFailed, .reason = M2ResultReason::kInvalidHandle,
            .detail = "model_id=" + std::to_string(model_id)};
  }
  return {.status = M2ResultStatus::kReady, .loaded = it->second->loaded,
          .render_ready = it->second->IsReadyForRender()};
}

M2FirstAnimationDurationQuery M2ModelQueries::QueryFirstAnimationDuration(
    const std::uint32_t model_id) const {
  std::lock_guard lock(mutex_);
  const auto it = models_.find(model_id);
  if (it == models_.end()) {
    return {.status = M2ResultStatus::kFailed, .reason = M2ResultReason::kInvalidHandle,
            .detail = "model_id=" + std::to_string(model_id)};
  }
  if (!it->second->loaded) {
    return {.status = M2ResultStatus::kNotReady, .reason = M2ResultReason::kMissingFile,
            .detail = it->second->model_path};
  }
  if (it->second->model_data.animation_durations_ms.empty()) {
    return {.status = M2ResultStatus::kReady, .duration_ms = 0u};
  }
  return {.status = M2ResultStatus::kReady,
          .duration_ms = it->second->model_data.animation_durations_ms.front()};
}

M2ModelAnimationSequenceQuery M2ModelQueries::QueryAnimationSequence(
    const std::uint32_t model_id, const std::uint32_t animation_id,
    const std::int32_t sub_animation_index) const {
  std::lock_guard lock(mutex_);
  const auto it = models_.find(model_id);
  if (it == models_.end()) {
    return {.status = M2ResultStatus::kFailed, .reason = M2ResultReason::kInvalidHandle,
            .detail = "model_id=" + std::to_string(model_id)};
  }
  if (!it->second->loaded) {
    return {.status = M2ResultStatus::kNotReady, .reason = M2ResultReason::kMissingFile,
            .detail = it->second->model_path};
  }
  return QueryAnimationSequenceLocked(*it->second, animation_id,
                                      sub_animation_index);
}

M2ModelAnimationSequenceQuery M2ModelQueries::QueryAnimationSequenceLocked(
    const detail::M2ModelResource& resource, const std::uint32_t animation_id,
    const std::int32_t sub_animation_index) const {
  const auto& model = resource.model_data;
  const auto resolved_id = ResolveM2AnimationId(model, animation_id, [this](const auto id) {
    return LookupAnimationAliasInfo(id, dbc_);
  });
  if (resolved_id >= kInvalidM2AnimationBehaviorId) {
    return {.status = M2ResultStatus::kReady, .has_sequence = false};
  }
  const auto sub_index = sub_animation_index < 0
                             ? 0u
                             : static_cast<std::uint32_t>(sub_animation_index);
  const auto sequence_index = FindM2AnimationSequenceIndex(model, resolved_id, sub_index);
  if (sequence_index == kInvalidM2AnimationSequenceIndex) {
    return {.status = M2ResultStatus::kReady, .has_sequence = false};
  }
  M2ModelAnimationSequenceInfo info{.animation_id = resolved_id,
                                    .sequence_index = sequence_index};
  if (sequence_index < model.animation_durations_ms.size()) {
    info.duration_ms = model.animation_durations_ms[sequence_index];
  }
  if (sequence_index < model.animation_sequences.size()) {
    info.move_speed = model.animation_sequences[sequence_index].move_speed;
    info.flags = model.animation_sequences[sequence_index].flags;
  }
  return {.status = M2ResultStatus::kReady, .has_sequence = true, .sequence = info};
}

M2ModelCollisionGeometryQuery M2ModelQueries::QueryCollisionGeometry(
    const std::uint32_t model_id) const {
  std::lock_guard lock(mutex_);
  const auto it = models_.find(model_id);
  if (it == models_.end()) {
    return {.status = M2ResultStatus::kFailed, .reason = M2ResultReason::kInvalidHandle,
            .detail = "model_id=" + std::to_string(model_id)};
  }
  if (!it->second->loaded) {
    return {.status = M2ResultStatus::kNotReady, .reason = M2ResultReason::kMissingFile,
            .detail = "model_id=" + std::to_string(model_id)};
  }
  return {.status = M2ResultStatus::kReady, .geometry = it->second->collision_geometry};
}

M2AttachmentInfoQuery M2ModelQueries::QueryAttachmentInfo(
    const std::uint32_t model_id,
    const std::uint32_t attachment_lookup_index) const {
  std::lock_guard lock(mutex_);
  const auto it = models_.find(model_id);
  if (it == models_.end()) {
    return {.status = M2ResultStatus::kFailed, .reason = M2ResultReason::kInvalidHandle,
            .detail = "model_id=" + std::to_string(model_id)};
  }
  if (!it->second->loaded) {
    return {.status = M2ResultStatus::kNotReady, .reason = M2ResultReason::kMissingFile,
            .detail = "model_id=" + std::to_string(model_id)};
  }
  const auto resolved = ResolveAttachmentLookup(it->second->model_data,
                                                attachment_lookup_index);
  if (resolved.attachment == nullptr) {
    return {.status = M2ResultStatus::kUnsupported,
            .reason = M2ResultReason::kMissingAttachment,
            .detail = "attachment_lookup=" + std::to_string(attachment_lookup_index)};
  }
  return {.status = M2ResultStatus::kReady,
          .attachment = {.attachment_index = resolved.attachment_index,
                         .bone_index = resolved.attachment->bone,
                         .local_position = AttachmentLocalPosition(*resolved.attachment)}};
}

M2ModelSpatialInfoQuery M2ModelQueries::QuerySpatialInfo(
    const std::uint32_t model_id, const std::uint16_t sequence_index) const {
  std::lock_guard lock(mutex_);
  const auto it = models_.find(model_id);
  if (it == models_.end()) {
    return {.status = M2ResultStatus::kFailed, .reason = M2ResultReason::kInvalidHandle,
            .detail = "model_id=" + std::to_string(model_id)};
  }
  if (!it->second->loaded) {
    return {.status = M2ResultStatus::kNotReady, .reason = M2ResultReason::kMissingFile,
            .detail = "model_id=" + std::to_string(model_id)};
  }
  return {.status = M2ResultStatus::kReady,
          .spatial = BuildM2ModelSpatialInfo(*it->second, sequence_index)};
}

M2ModelGeometryInfoQuery M2ModelQueries::QueryGeometryInfo(
    const std::uint32_t model_id) const {
  std::lock_guard lock(mutex_);
  const auto it = models_.find(model_id);
  if (it == models_.end()) {
    return {.status = M2ResultStatus::kFailed, .reason = M2ResultReason::kInvalidHandle,
            .detail = "model_id=" + std::to_string(model_id)};
  }
  if (!it->second->loaded) {
    return {.status = M2ResultStatus::kNotReady, .reason = M2ResultReason::kMissingFile,
            .detail = "model_id=" + std::to_string(model_id)};
  }
  const auto& vertices = it->second->model_data.vertices;
  M2ModelGeometryInfo info{};
  if (vertices.empty()) {
    return {.status = M2ResultStatus::kReady, .info = info};
  }
  info.has_vertices = true;
  info.vertex_bounds = {vertices.front().position[0], vertices.front().position[1],
                        vertices.front().position[2], vertices.front().position[0],
                        vertices.front().position[1], vertices.front().position[2]};
  for (const auto& vertex : vertices) {
    info.vertex_bounds[0] = std::min(info.vertex_bounds[0], vertex.position[0]);
    info.vertex_bounds[1] = std::min(info.vertex_bounds[1], vertex.position[1]);
    info.vertex_bounds[2] = std::min(info.vertex_bounds[2], vertex.position[2]);
    info.vertex_bounds[3] = std::max(info.vertex_bounds[3], vertex.position[0]);
    info.vertex_bounds[4] = std::max(info.vertex_bounds[4], vertex.position[1]);
    info.vertex_bounds[5] = std::max(info.vertex_bounds[5], vertex.position[2]);
  }
  return {.status = M2ResultStatus::kReady, .info = info};
}

M2ModelInfoQuery M2ModelQueries::QueryInfo(const std::uint32_t model_id) const {
  std::lock_guard lock(mutex_);
  const auto it = models_.find(model_id);
  if (it == models_.end()) {
    return {.status = M2ResultStatus::kFailed, .reason = M2ResultReason::kInvalidHandle,
            .detail = "model_id=" + std::to_string(model_id)};
  }
  const auto& r = *it->second;
  return {.status = M2ResultStatus::kReady,
          .info = {.model_path = r.model_path, .name = r.model_data.name,
                   .version = r.model_data.header.version,
                   .global_flags = r.model_data.header.global_flags,
                   .view_count = r.model_data.header.num_views,
                   .selected_skin_profile = r.selected_skin_profile,
                   .loaded = r.loaded, .render_ready = r.IsReadyForRender(),
                   .vertex_count = r.model_data.vertices.size(),
                   .bone_count = r.model_data.bones.size(),
                   .key_bone_lookup_count = r.model_data.key_bone_lookup.size(),
                   .texture_count = r.model_data.textures.size(),
                   .texture_unit_count = r.skin_data.texture_units.size(),
                   .submesh_count = r.skin_data.submeshes.size(),
                   .gpu_texture_count = r.gpu_textures.size(),
                   .color_count = r.model_data.colors.size(),
                   .transparency_count = r.model_data.transparencies.size(),
                   .global_sequence_count = r.model_data.global_sequences_ms.size(),
                   .animation_duration_count = r.model_data.animation_durations_ms.size(),
                   .light_count = r.model_data.lights.size(),
                   .camera_count = r.model_data.cameras.size(),
                   .attachment_count = r.model_data.attachments.size(),
                   .event_count = r.model_data.events.size(),
                   .particle_emitter_count = r.model_data.particle_emitters.size(),
                   .ribbon_emitter_count = r.model_data.ribbon_emitters.size()}};
}

M2ModelAnimationListQuery M2ModelQueries::QueryAnimationList(
    const std::uint32_t model_id) const {
  std::lock_guard lock(mutex_);
  const auto it = models_.find(model_id);
  if (it == models_.end()) {
    return {.status = M2ResultStatus::kFailed, .reason = M2ResultReason::kInvalidHandle,
            .detail = "model_id=" + std::to_string(model_id)};
  }
  if (!it->second->loaded) {
    return {.status = M2ResultStatus::kNotReady, .reason = M2ResultReason::kMissingFile,
            .detail = "model_id=" + std::to_string(model_id)};
  }
  M2ModelAnimationListQuery result{.status = M2ResultStatus::kReady};
  const auto& sequences = it->second->model_data.animation_sequences;
  result.animations.reserve(sequences.size());
  for (std::size_t index = 0; index < sequences.size(); ++index) {
    result.animations.push_back({.sequence_index = static_cast<std::uint16_t>(index),
                                 .animation_id = sequences[index].animation_id,
                                 .duration_ms = sequences[index].length_ms});
  }
  return result;
}

M2ModelTextureDependenciesQuery M2ModelQueries::QueryTextureDependencies(
    const std::uint32_t model_id) const {
  std::lock_guard lock(mutex_);
  const auto it = models_.find(model_id);
  if (it == models_.end()) {
    return {.status = M2ResultStatus::kFailed, .reason = M2ResultReason::kInvalidHandle,
            .detail = "model_id=" + std::to_string(model_id)};
  }
  if (!it->second->loaded) {
    return {.status = M2ResultStatus::kNotReady, .reason = M2ResultReason::kMissingFile,
            .detail = "model_id=" + std::to_string(model_id)};
  }
  const auto& resource = *it->second;
  const auto& model = resource.model_data;
  M2ModelTextureDependenciesQuery result{.status = M2ResultStatus::kReady};
  std::vector<std::uint16_t> seen;
  const auto add_index = [&](const std::uint16_t index) {
    if (index >= model.textures.size()) return;
    const auto& texture = model.textures[index];
    if (texture.type != 0u || texture.name_text.empty() ||
        std::find(seen.begin(), seen.end(), index) != seen.end()) return;
    seen.push_back(index);
    result.dependencies.push_back({.texture_index = index,
                                   .texture_path = texture.name_text});
  };
  if (!resource.IsReadyForRender()) {
    for (std::size_t index = 0; index < model.textures.size(); ++index) {
      if (index > std::numeric_limits<std::uint16_t>::max()) break;
      add_index(static_cast<std::uint16_t>(index));
    }
    return result;
  }
  for (const auto& unit : resource.skin_data.texture_units) {
    const auto shader = ResolveM2SkinTextureUnitShader(model, unit);
    if (!shader.valid || !shader.draws || shader.texture_count == 0u) continue;
    const auto refs = ResolveM2SkinTextureUnitCombos(model, unit);
    add_index(refs.primary_texture_index);
    if (shader.texture_count > 1u && refs.secondary_texture_index.has_value()) {
      add_index(*refs.secondary_texture_index);
    }
  }
  for (const auto& emitter : model.particle_emitters) add_index(emitter.texture);
  for (const auto& ribbon : model.ribbon_emitters) {
    for (const auto index : ribbon.texture_indices) add_index(index);
  }
  return result;
}

M2ModelSubmeshSectionIdsQuery M2ModelQueries::QuerySubmeshSectionIds(
    const std::uint32_t model_id) const {
  std::lock_guard lock(mutex_);
  const auto it = models_.find(model_id);
  if (it == models_.end()) return {.status = M2ResultStatus::kFailed,
      .reason = M2ResultReason::kInvalidHandle, .detail = "model_id=" + std::to_string(model_id)};
  if (!it->second->loaded) return {.status = M2ResultStatus::kNotReady,
      .reason = M2ResultReason::kMissingFile, .detail = "model_id=" + std::to_string(model_id)};
  M2ModelSubmeshSectionIdsQuery result{.status = M2ResultStatus::kReady};
  result.section_ids.reserve(it->second->skin_data.submeshes.size());
  for (const auto& submesh : it->second->skin_data.submeshes) {
    result.section_ids.push_back(submesh.section_id);
  }
  return result;
}

M2ModelLightCountQuery M2ModelQueries::QueryLightCount(
    const std::uint32_t model_id) const {
  std::lock_guard lock(mutex_);
  const auto it = models_.find(model_id);
  if (it == models_.end()) return {.status = M2ResultStatus::kFailed,
      .reason = M2ResultReason::kInvalidHandle, .detail = "model_id=" + std::to_string(model_id)};
  if (!it->second->loaded) return {.status = M2ResultStatus::kNotReady,
      .reason = M2ResultReason::kMissingFile, .detail = "model_id=" + std::to_string(model_id)};
  return {.status = M2ResultStatus::kReady,
          .light_count = it->second->model_data.lights.size()};
}

bool M2ModelQueries::HasSubmeshId(const std::uint32_t model_id,
                                  const std::uint16_t submesh_id) const {
  std::lock_guard lock(mutex_);
  const auto it = models_.find(model_id);
  if (it == models_.end() || !it->second->loaded) return false;
  return std::any_of(it->second->skin_data.submeshes.begin(),
                     it->second->skin_data.submeshes.end(),
                     [submesh_id](const auto& submesh) {
                       return submesh.section_id == submesh_id;
                     });
}

bool M2ModelQueries::ContainsAnimation(const std::uint32_t model_id,
                                      const std::uint32_t animation_id) const {
  std::lock_guard lock(mutex_);
  const auto it = models_.find(model_id);

  return it != models_.end() &&
         M2ModelHasAnimationId(it->second->model_data, animation_id);
}

bool M2ModelQueries::HasAnimation(const std::uint32_t model_id,
                                  const std::uint32_t animation_id) const {
  std::lock_guard lock(mutex_);
  const auto it = models_.find(model_id);
  return it != models_.end() && M2ModelHasAnimationId(
      it->second->model_data, animation_id,
      [this](const auto id) { return LookupAnimationAliasInfo(id, dbc_); });
}

M2SampleBoneMatricesQuery M2ModelQueries::QuerySampleBoneMatrices(
    const std::uint32_t model_id, const int sequence_index,
    const std::uint32_t time_ms,
    const std::optional<RenderMatrix4x4View>& camera_inverse_view) const {
  std::lock_guard lock(mutex_);
  const auto it = models_.find(model_id);
  if (it == models_.end()) return {.status = M2ResultStatus::kFailed,
      .reason = M2ResultReason::kInvalidHandle, .detail = "model_id=" + std::to_string(model_id)};
  if (!it->second->loaded) return {.status = M2ResultStatus::kNotReady,
      .reason = M2ResultReason::kMissingFile, .detail = it->second->model_path};
  M2Animator animator(&it->second->model_data);
  auto matrices = animator.ComputeBoneMatrices(sequence_index, time_ms, camera_inverse_view);
  if (!matrices.has_value()) return {.status = M2ResultStatus::kFailed,
      .reason = M2ResultReason::kBonePoseFailed, .detail = "model_id=" + std::to_string(model_id)};
  return {.status = M2ResultStatus::kReady, .bone_matrices = std::move(*matrices)};
}

M2CameraSampleQuery M2ModelQueries::QueryCameraSample(
    const std::uint32_t model_id, const int camera_index,
    const int sequence_index, const std::uint32_t time_ms) const {
  std::lock_guard lock(mutex_);
  const auto it = models_.find(model_id);
  if (it == models_.end()) return {.status = M2ResultStatus::kFailed,
      .reason = M2ResultReason::kInvalidHandle, .detail = "model_id=" + std::to_string(model_id)};
  if (!it->second->loaded) return {.status = M2ResultStatus::kNotReady,
      .reason = M2ResultReason::kMissingFile, .detail = it->second->model_path};
  if (camera_index < 0 || static_cast<std::size_t>(camera_index) >=
                              it->second->model_data.cameras.size()) {
    return {.status = M2ResultStatus::kUnsupported, .reason = M2ResultReason::kMissingCamera,
            .detail = "camera_index=" + std::to_string(camera_index)};
  }
  M2Animator animator(&it->second->model_data);
  const auto pose = animator.SampleCamera(camera_index, sequence_index, time_ms);
  if (!pose.has_value()) return {.status = M2ResultStatus::kUnsupported,
      .reason = M2ResultReason::kMissingCamera, .detail = "camera_index=" + std::to_string(camera_index)};
  return {.status = M2ResultStatus::kReady, .pose = *pose};
}

M2LightSampleQuery M2ModelQueries::QueryLightSample(
    const std::uint32_t model_id, const int light_index,
    const int sequence_index, const std::uint32_t time_ms,
    std::span<const float> bone_matrices) const {
  std::lock_guard lock(mutex_);
  const auto it = models_.find(model_id);
  if (it == models_.end()) return {.status = M2ResultStatus::kFailed,
      .reason = M2ResultReason::kInvalidHandle, .detail = "model_id=" + std::to_string(model_id)};
  if (!it->second->loaded) return {.status = M2ResultStatus::kNotReady,
      .reason = M2ResultReason::kMissingFile, .detail = it->second->model_path};
  if (light_index < 0 || static_cast<std::size_t>(light_index) >=
                             it->second->model_data.lights.size()) {
    return {.status = M2ResultStatus::kUnsupported, .reason = M2ResultReason::kMissingLight,
            .detail = "light_index=" + std::to_string(light_index)};
  }
  M2Animator animator(&it->second->model_data);
  return {.status = M2ResultStatus::kReady,
          .light = animator.SampleLight(light_index, sequence_index, time_ms, bone_matrices)};
}

std::vector<M2TriggeredEvent> M2ModelQueries::CollectTriggeredEvents(
    const std::uint32_t model_id, const int sequence_index,
    const std::uint32_t previous_time_ms, const std::uint32_t current_time_ms,
    std::span<const float> bone_matrices,
    const std::optional<RenderMatrix4x4>& model_matrix) const {
  std::lock_guard lock(mutex_);
  const auto it = models_.find(model_id);
  if (it == models_.end() || !it->second->loaded) return {};
  M2Animator animator(&it->second->model_data);
  return animator.CollectTriggeredEvents(sequence_index, previous_time_ms,
                                          current_time_ms, bone_matrices,
                                          model_matrix);
}

}

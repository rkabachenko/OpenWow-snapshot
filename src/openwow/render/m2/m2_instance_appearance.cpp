#include "openwow/render/m2/m2_instance_store.h"

#include "openwow/render/backend/bgfx/bgfx_texture_lease.h"
#include "openwow/render/resources/textures/texture_manager.h"

#include <algorithm>
#include <cmath>

namespace openwow::render::m2 {
namespace {

constexpr std::uint32_t kDefaultParticleColorRawGreen = 0xff00ff00u;

bool IsFinite(const RenderVec4& value) noexcept {
  return std::all_of(value.begin(), value.end(),
                     [](const float component) {
                       return std::isfinite(component);
                     });
}

bool IsFinite(const RenderMatrix4x4& value) noexcept {
  return std::all_of(value.begin(), value.end(),
                     [](const float component) {
                       return std::isfinite(component);
                     });
}

M2InstanceAttachmentVisualState BuildAttachmentState(
    const detail::M2Instance& instance) {
  M2InstanceAttachmentVisualState state;
  state.ready_for_updates = instance.ready_for_attachment_visual_updates;
  state.data_initialized = instance.attachment_visual_data_initialized;
  state.flags = instance.attachment_visual_flags;
  state.primary_visual_count = instance.primary_visual_count;
  state.active_primary_visual_count = instance.active_primary_visual_count;
  state.current_animation_duration_ms = instance.current_animation_duration_ms;
  state.active_primary_visual_duration_ms =
      instance.active_primary_visual_duration_ms;
  state.primary_visual_weight = instance.primary_visual_weight;
  for (const auto& entry : instance.primary_visual_entries) {
    state.primary_visual_entry_flags.push_back(entry.flags);
  }
  return state;
}

}

M2VisibleSubmeshFilterQuery M2InstanceStore::QueryVisibleSubmeshFilter(
    const std::uint32_t instance_id) const {
  std::lock_guard lock(mutex_);
  const auto found = instances_.find(instance_id);
  if (found == instances_.end()) {
    return {.status = M2ResultStatus::kFailed,
            .reason = M2ResultReason::kInvalidHandle,
            .detail = "instance_id=" + std::to_string(instance_id)};
  }
  return {.status = M2ResultStatus::kReady,
          .filter = {.enabled = found->second->has_visible_submesh_filter,
                     .dirty = (found->second->dirty_flags &
                               detail::M2Instance::kDirtyVisibleSubmeshes) != 0u,
                     .indices = found->second->visible_submesh_indices}};
}

M2InstanceAttachmentVisualStateQuery
M2InstanceStore::QueryAttachmentVisualState(
    const std::uint32_t instance_id) const {
  std::lock_guard lock(mutex_);
  const auto found = instances_.find(instance_id);
  if (found == instances_.end()) {
    return {.status = M2ResultStatus::kFailed,
            .reason = M2ResultReason::kInvalidHandle,
            .detail = "instance_id=" + std::to_string(instance_id)};
  }
  return {.status = M2ResultStatus::kReady,
          .visual_state = BuildAttachmentState(*found->second)};
}

M2ResultStatus M2InstanceStore::SetAttachmentVisualState(
    const std::uint32_t instance_id,
    const M2InstanceAttachmentVisualState& state) {
  std::lock_guard lock(mutex_);
  const auto found = instances_.find(instance_id);
  if (found == instances_.end()) return M2ResultStatus::kFailed;
  auto& instance = *found->second;
  const auto previous = BuildAttachmentState(instance);
  instance.ready_for_attachment_visual_updates = state.ready_for_updates;
  instance.attachment_visual_data_initialized = state.data_initialized;
  instance.attachment_visual_flags = state.flags;
  instance.primary_visual_count = state.primary_visual_count;
  instance.active_primary_visual_count = state.active_primary_visual_count;
  instance.current_animation_duration_ms = state.current_animation_duration_ms;
  instance.active_primary_visual_duration_ms =
      state.active_primary_visual_duration_ms;
  instance.primary_visual_weight = state.primary_visual_weight;
  instance.primary_visual_entries.clear();
  for (const auto flags : state.primary_visual_entry_flags) {
    instance.primary_visual_entries.push_back({.flags = flags});
  }
  if (previous.ready_for_updates != state.ready_for_updates ||
      previous.data_initialized != state.data_initialized ||
      previous.flags != state.flags ||
      previous.primary_visual_count != state.primary_visual_count ||
      previous.active_primary_visual_count != state.active_primary_visual_count ||
      previous.current_animation_duration_ms != state.current_animation_duration_ms ||
      previous.active_primary_visual_duration_ms !=
          state.active_primary_visual_duration_ms ||
      previous.primary_visual_weight != state.primary_visual_weight ||
      previous.primary_visual_entry_flags != state.primary_visual_entry_flags) {
    BumpVisualRevision(instance);
  }
  return M2ResultStatus::kReady;
}

M2ResultStatus M2InstanceStore::SetEffectContext(
    const std::uint32_t instance_id,
    const M2InstanceEffectContext& effect_context) {
  std::lock_guard lock(mutex_);
  const auto found = instances_.find(instance_id);
  if (found == instances_.end()) return M2ResultStatus::kFailed;
  found->second->effect_context = effect_context;
  return M2ResultStatus::kReady;
}

M2ResultStatus M2InstanceStore::SetReplaceableTexturePath(
    const std::uint32_t instance_id, const std::uint32_t texture_type,
    const std::string& texture_path) {
  if (texture_path.empty()) return M2ResultStatus::kFailed;
  {
    std::lock_guard lock(mutex_);
    if (!instances_.contains(instance_id)) return M2ResultStatus::kFailed;
  }
  TextureLease lease = texture_manager_ != nullptr
                           ? texture_manager_->AcquireTextureStrict(texture_path)
                           : TextureLease{};
  const auto handle = BgfxTextureLeaseAccess::Get(lease);
  std::lock_guard lock(mutex_);
  const auto found = instances_.find(instance_id);
  if (found == instances_.end()) return M2ResultStatus::kFailed;
  auto& overrides = found->second->texture_overrides;
  const auto existing = std::find_if(
      overrides.begin(), overrides.end(), [texture_type](const auto& override) {
        return override.type_id == texture_type;
      });
  if (existing == overrides.end()) {
    overrides.push_back({.type_id = texture_type,
                         .texture_path = texture_path,
                         .texture = handle,
                         .texture_lease = std::move(lease)});
    BumpVisualRevision(*found->second);
  } else if (existing->texture_path == texture_path) {
    if (lease) {
      existing->texture = handle;
      existing->texture_lease = std::move(lease);
    }
  } else {
    existing->texture_path = texture_path;
    existing->texture = handle;
    existing->texture_lease = std::move(lease);
    BumpVisualRevision(*found->second);
  }
  return M2ResultStatus::kReady;
}

M2ResultStatus M2InstanceStore::ClearReplaceableTexturePath(
    const std::uint32_t instance_id, const std::uint32_t texture_type) {
  std::lock_guard lock(mutex_);
  const auto found = instances_.find(instance_id);
  if (found == instances_.end()) return M2ResultStatus::kFailed;
  ApplyClearReplaceableTexturePathLocked(*found->second, texture_type);
  return M2ResultStatus::kReady;
}

void M2InstanceStore::ApplyClearReplaceableTexturePathLocked(
    detail::M2Instance& instance, const std::uint32_t texture_type) {
  auto& overrides = instance.texture_overrides;
  const auto old_size = overrides.size();
  overrides.erase(std::remove_if(overrides.begin(), overrides.end(),
                                 [texture_type](const auto& override) {
                                   return override.type_id == texture_type;
                                 }),
                  overrides.end());
  if (old_size != overrides.size()) BumpVisualRevision(instance);
}

M2ResultStatus M2InstanceStore::ClearReplaceableTexturePaths(
    const std::uint32_t instance_id) {
  std::lock_guard lock(mutex_);
  const auto found = instances_.find(instance_id);
  if (found == instances_.end()) return M2ResultStatus::kFailed;
  if (!found->second->texture_overrides.empty()) {
    found->second->texture_overrides.clear();
    BumpVisualRevision(*found->second);
  }
  return M2ResultStatus::kReady;
}

M2ResultStatus M2InstanceStore::SetVisible(const std::uint32_t instance_id,
                                            const bool visible) {
  std::lock_guard lock(mutex_);
  const auto found = instances_.find(instance_id);
  if (found == instances_.end()) return M2ResultStatus::kFailed;
  if (found->second->visible != visible) {
    found->second->visible = visible;
    BumpVisualRevision(*found->second);
  }
  return M2ResultStatus::kReady;
}

M2ResultStatus M2InstanceStore::SetEffectEmittersEnabled(
    const std::uint32_t instance_id, const bool enabled) {
  std::lock_guard lock(mutex_);
  const auto found = instances_.find(instance_id);
  if (found == instances_.end()) return M2ResultStatus::kFailed;
  if (found->second->effect_emitters_enabled != enabled) {
    found->second->effect_emitters_enabled = enabled;
    BumpVisualRevision(*found->second);
  }
  return M2ResultStatus::kReady;
}

M2ResultStatus M2InstanceStore::BeginTransientWeaponTrail(
    const std::uint32_t instance_id, const std::uint32_t packed_argb,
    const std::uint32_t duration_ms, const std::uint32_t start_tick_ms) {
  std::lock_guard lock(mutex_);
  const auto found = instances_.find(instance_id);
  if (found == instances_.end()) return M2ResultStatus::kFailed;
  if (duration_ms == 0u) return M2ResultStatus::kReady;
  found->second->transient_weapon_trail = {
      .packed_argb = packed_argb,
      .duration_ms = duration_ms,
      .start_tick_ms = start_tick_ms,
      .previous_tick_ms = start_tick_ms};
  BumpVisualRevision(*found->second);
  return M2ResultStatus::kReady;
}

M2ResultStatus M2InstanceStore::SetTintColor(
    const std::uint32_t instance_id, const RenderVec4& rgba) {
  if (!IsFinite(rgba)) return M2ResultStatus::kFailed;
  std::lock_guard lock(mutex_);
  const auto found = instances_.find(instance_id);
  if (found == instances_.end()) return M2ResultStatus::kFailed;
  if (found->second->tint_color != rgba) {
    found->second->tint_color = rgba;
    BumpVisualRevision(*found->second);
  }
  return M2ResultStatus::kReady;
}

M2ResultStatus M2InstanceStore::SetAlpha(const std::uint32_t instance_id,
                                          const float alpha) {
  std::lock_guard lock(mutex_);
  const auto found = instances_.find(instance_id);
  if (found == instances_.end()) return M2ResultStatus::kFailed;
  if (found->second->alpha != alpha) {
    found->second->alpha = alpha;
    BumpVisualRevision(*found->second);
  }
  return M2ResultStatus::kReady;
}

M2ResultStatus M2InstanceStore::SetSelectionGlowColor(
    const std::uint32_t instance_id, const RenderVec4& rgba) {
  if (!IsFinite(rgba)) return M2ResultStatus::kFailed;
  std::lock_guard lock(mutex_);
  const auto found = instances_.find(instance_id);
  if (found == instances_.end()) return M2ResultStatus::kFailed;
  if (found->second->selection_glow_color != rgba) {
    found->second->selection_glow_color = rgba;
    BumpVisualRevision(*found->second);
  }
  return M2ResultStatus::kReady;
}

M2SelectionGlowColorQuery M2InstanceStore::QuerySelectionGlowColor(
    const std::uint32_t instance_id) const {
  std::lock_guard lock(mutex_);
  const auto found = instances_.find(instance_id);
  if (found == instances_.end()) {
    return {.status = M2ResultStatus::kFailed,
            .reason = M2ResultReason::kInvalidHandle,
            .detail = "instance_id=" + std::to_string(instance_id)};
  }
  return {.status = M2ResultStatus::kReady,
          .rgba = found->second->selection_glow_color};
}

M2ResultStatus M2InstanceStore::ApplyCreatureDisplayRecordOverrides(
    const std::uint32_t instance_id,
    const std::array<std::string, 3>& texture_paths,
    std::optional<M2ParticleColorRecord> record) {
  M2ResultStatus result = M2ResultStatus::kReady;
  for (std::uint32_t index = 0; index < texture_paths.size(); ++index) {
    const auto type = index + 11u;
    result = MergeM2ResultStatus(
        result, ClearReplaceableTexturePath(instance_id, type));
    if (!texture_paths[index].empty()) {
      result = MergeM2ResultStatus(
          result, SetReplaceableTexturePath(instance_id, type,
                                             texture_paths[index]));
    }
  }
  return MergeM2ResultStatus(
      result,
      ApplyParticleColorRecordSlots11To13(instance_id, std::move(record)));
}

M2ResultStatus M2InstanceStore::SetParticleColorOverride(
    const std::uint32_t instance_id, const std::uint32_t color_index,
    const std::uint32_t start_raw, const std::uint32_t mid_raw,
    const std::uint32_t end_raw) {
  std::lock_guard lock(mutex_);
  const auto found = instances_.find(instance_id);
  if (found == instances_.end()) return M2ResultStatus::kFailed;
  auto& overrides = found->second->particle_color_overrides;
  const auto existing = std::find_if(
      overrides.begin(), overrides.end(), [color_index](const auto& override) {
        return override.color_index == color_index;
      });
  if (existing == overrides.end()) {
    overrides.push_back({color_index, start_raw, mid_raw, end_raw});
    BumpVisualRevision(*found->second);
  } else if (existing->start_raw != start_raw || existing->mid_raw != mid_raw ||
             existing->end_raw != end_raw) {
    existing->start_raw = start_raw;
    existing->mid_raw = mid_raw;
    existing->end_raw = end_raw;
    BumpVisualRevision(*found->second);
  }
  return M2ResultStatus::kReady;
}

M2ResultStatus M2InstanceStore::ClearParticleColorOverride(
    const std::uint32_t instance_id, const std::uint32_t color_index) {
  std::lock_guard lock(mutex_);
  const auto found = instances_.find(instance_id);
  if (found == instances_.end()) return M2ResultStatus::kFailed;
  auto& overrides = found->second->particle_color_overrides;
  const auto old_size = overrides.size();
  overrides.erase(std::remove_if(overrides.begin(), overrides.end(),
                                 [color_index](const auto& override) {
                                   return override.color_index == color_index;
                                 }),
                  overrides.end());
  if (old_size != overrides.size()) BumpVisualRevision(*found->second);
  return M2ResultStatus::kReady;
}

M2ResultStatus M2InstanceStore::SetReplacementColors(
    const std::uint32_t instance_id,
    const std::array<std::uint32_t, 3>& colors) {
  std::lock_guard lock(mutex_);
  const auto found = instances_.find(instance_id);
  if (found == instances_.end()) return M2ResultStatus::kFailed;
  const bool has_colors = colors[0] != 0u || colors[1] != 0u || colors[2] != 0u;
  if (found->second->replacement_colors != colors ||
      found->second->has_replacement_colors != has_colors) {
    found->second->replacement_colors = colors;
    found->second->has_replacement_colors = has_colors;
    BumpVisualRevision(*found->second);
  }
  return M2ResultStatus::kReady;
}

M2ResultStatus M2InstanceStore::ClearReplacementColors(
    const std::uint32_t instance_id) {
  std::lock_guard lock(mutex_);
  const auto found = instances_.find(instance_id);
  if (found == instances_.end()) return M2ResultStatus::kFailed;
  if (found->second->has_replacement_colors ||
      found->second->replacement_colors != std::array<std::uint32_t, 3>{}) {
    found->second->replacement_colors = {};
    found->second->has_replacement_colors = false;
    BumpVisualRevision(*found->second);
  }
  return M2ResultStatus::kReady;
}

M2ResultStatus M2InstanceStore::ApplyParticleColorRecordSlots11To13(
    const std::uint32_t instance_id,
    std::optional<M2ParticleColorRecord> record) {
  M2ResultStatus result = M2ResultStatus::kReady;
  for (std::uint32_t index = 0; index < 3u; ++index) {
    result = MergeM2ResultStatus(
        result,
        SetParticleColorOverride(
            instance_id, index + 11u,
            record ? record->start[index] : kDefaultParticleColorRawGreen,
            record ? record->mid[index] : kDefaultParticleColorRawGreen,
            record ? record->end[index] : kDefaultParticleColorRawGreen));
  }
  return result;
}

M2ResultStatus M2InstanceStore::SetVisibleSubmeshIndices(
    const std::uint32_t instance_id,
    std::vector<std::size_t> visible_submesh_indices) {
  std::lock_guard lock(mutex_);
  const auto found = instances_.find(instance_id);
  if (found == instances_.end()) return M2ResultStatus::kFailed;
  if (!found->second->has_visible_submesh_filter ||
      found->second->visible_submesh_indices != visible_submesh_indices) {
    found->second->visible_submesh_indices = std::move(visible_submesh_indices);
    found->second->has_visible_submesh_filter = true;
    found->second->dirty_flags |=
        detail::M2Instance::kDirtyVisibleSubmeshes;
    BumpVisualRevision(*found->second);
  }
  return M2ResultStatus::kReady;
}

M2ResultStatus M2InstanceStore::ClearVisibleSubmeshIndices(
    const std::uint32_t instance_id) {
  std::lock_guard lock(mutex_);
  const auto found = instances_.find(instance_id);
  if (found == instances_.end()) return M2ResultStatus::kFailed;
  if (found->second->has_visible_submesh_filter ||
      !found->second->visible_submesh_indices.empty()) {
    found->second->visible_submesh_indices.clear();
    found->second->has_visible_submesh_filter = false;
    found->second->dirty_flags |=
        detail::M2Instance::kDirtyVisibleSubmeshes;
    BumpVisualRevision(*found->second);
  }
  return M2ResultStatus::kReady;
}

M2ResultStatus M2InstanceStore::ApplyDoodadFrameRenderStateLocked(
    const std::uint32_t instance_id, const RenderMatrix4x4& world_transform,
    const std::uint64_t world_transform_revision, const M2BatchUniforms* uniforms,
    const M2SharedBatchUniformsHandle shared_uniforms,
    const RenderVec4& tint_rgba, const float alpha) {
  const auto found = instances_.find(instance_id);
  if (found == instances_.end()) return M2ResultStatus::kFailed;
  auto& instance = *found->second;
  M2ResultStatus status = M2ResultStatus::kReady;

  const bool transform_already_adopted =
      world_transform_revision != 0u && instance.has_explicit_world_transform &&
      instance.explicit_world_transform_revision == world_transform_revision;
  if (transform_already_adopted) {

  } else if (IsFinite(world_transform)) {
    StoreExplicitWorldTransformLocked(instance, world_transform,
                                      world_transform_revision);
  } else {
    status = MergeM2ResultStatus(status, M2ResultStatus::kFailed);
  }

  if (shared_uniforms.block != nullptr) {
    AdoptSharedBatchUniformsLocked(instance, shared_uniforms.block);
  } else {
    StoreBatchUniformsLocked(instance, *uniforms);
  }

  if (instance.has_visible_submesh_filter ||
      !instance.visible_submesh_indices.empty()) {
    instance.visible_submesh_indices.clear();
    instance.has_visible_submesh_filter = false;
    instance.dirty_flags |= detail::M2Instance::kDirtyVisibleSubmeshes;
    BumpVisualRevision(instance);
  }

  if (IsFinite(tint_rgba)) {
    if (instance.tint_color != tint_rgba) {
      instance.tint_color = tint_rgba;
      BumpVisualRevision(instance);
    }
  } else {
    status = MergeM2ResultStatus(status, M2ResultStatus::kFailed);
  }

  if (instance.alpha != alpha) {
    instance.alpha = alpha;
    BumpVisualRevision(instance);
  }

  if (!instance.visible) {
    instance.visible = true;
    BumpVisualRevision(instance);
  }

  return status;
}

M2ResultStatus M2InstanceStore::SetDoodadFrameRenderState(
    const std::uint32_t instance_id, const RenderMatrix4x4& world_transform,
    const M2BatchUniforms& uniforms, const RenderVec4& tint_rgba,
    const float alpha) {
  std::lock_guard lock(mutex_);

  return ApplyDoodadFrameRenderStateLocked(instance_id, world_transform, 0u,
                                           &uniforms, {}, tint_rgba, alpha);
}

void M2InstanceStore::SetDoodadFrameRenderStates(
    const std::span<const M2DoodadFrameRenderRequest> requests,
    const std::span<M2ResultStatus> out_statuses) {
  std::lock_guard lock(mutex_);
  for (std::size_t index = 0; index < requests.size(); ++index) {
    const M2DoodadFrameRenderRequest& request = requests[index];

    out_statuses[index] =
        (request.world_transform == nullptr ||
         (request.uniforms == nullptr && !request.shared_uniforms))
            ? M2ResultStatus::kFailed
            : ApplyDoodadFrameRenderStateLocked(
                  request.instance_id, *request.world_transform,
                  request.world_transform_revision, request.uniforms,
                  request.shared_uniforms, request.tint_rgba, request.alpha);
  }
}

M2ResultStatus M2InstanceStore::SetVisualPresentation(
    const std::uint32_t instance_id, const bool visible,
    const RenderVec4& tint_rgba, const float alpha) {
  std::lock_guard lock(mutex_);
  const auto found = instances_.find(instance_id);
  if (found == instances_.end()) return M2ResultStatus::kFailed;
  return ApplyVisualPresentationLocked(*found->second, visible, tint_rgba,
                                       alpha);
}

M2ResultStatus M2InstanceStore::ApplyVisualPresentationLocked(
    detail::M2Instance& instance, const bool visible,
    const RenderVec4& tint_rgba, const float alpha) {
  M2ResultStatus status = M2ResultStatus::kReady;

  if (instance.visible != visible) {
    instance.visible = visible;
    BumpVisualRevision(instance);
  }

  if (IsFinite(tint_rgba)) {
    if (instance.tint_color != tint_rgba) {
      instance.tint_color = tint_rgba;
      BumpVisualRevision(instance);
    }
  } else {
    status = MergeM2ResultStatus(status, M2ResultStatus::kFailed);
  }

  if (instance.alpha != alpha) {
    instance.alpha = alpha;
    BumpVisualRevision(instance);
  }

  return status;
}

M2TransformVisibilityResult M2InstanceStore::SetTransformAndVisibility(
    const std::uint32_t instance_id, const RenderMatrix4x4& matrix,
    const bool visible) {
  std::lock_guard lock(mutex_);
  const auto found = instances_.find(instance_id);
  if (found == instances_.end()) {
    return {.transform_status = M2ResultStatus::kFailed,
            .visibility_status = M2ResultStatus::kFailed};
  }
  auto& instance = *found->second;
  M2TransformVisibilityResult result;

  if (IsFinite(matrix)) {
    StoreExplicitWorldTransformLocked(instance, matrix);
  } else {
    result.transform_status = M2ResultStatus::kFailed;
  }

  if (instance.visible != visible) {
    instance.visible = visible;
    BumpVisualRevision(instance);
  }

  return result;
}

M2ResultStatus M2InstanceStore::ApplyAttachmentFrameRenderStateLocked(
    const std::uint32_t instance_id, const RenderMatrix4x4& world_transform,
    const M2BatchUniforms* uniforms,
    const M2SharedBatchUniformsHandle shared_uniforms, const bool visible,
    const RenderVec4& tint_rgba, const float alpha) {
  const auto found = instances_.find(instance_id);
  if (found == instances_.end()) return M2ResultStatus::kFailed;
  auto& instance = *found->second;
  M2ResultStatus status = M2ResultStatus::kReady;

  if (IsFinite(world_transform)) {
    StoreExplicitWorldTransformLocked(instance, world_transform);
  } else {
    status = MergeM2ResultStatus(status, M2ResultStatus::kFailed);
  }

  if (shared_uniforms.block != nullptr) {
    AdoptSharedBatchUniformsLocked(instance, shared_uniforms.block);
  } else {
    StoreBatchUniformsLocked(instance, *uniforms);
  }

  if (instance.visible != visible) {
    instance.visible = visible;
    BumpVisualRevision(instance);
  }

  if (IsFinite(tint_rgba)) {
    if (instance.tint_color != tint_rgba) {
      instance.tint_color = tint_rgba;
      BumpVisualRevision(instance);
    }
  } else {
    status = MergeM2ResultStatus(status, M2ResultStatus::kFailed);
  }

  if (instance.alpha != alpha) {
    instance.alpha = alpha;
    BumpVisualRevision(instance);
  }

  return status;
}

M2ResultStatus M2InstanceStore::SetAttachmentFrameRenderState(
    const std::uint32_t instance_id, const RenderMatrix4x4& world_transform,
    const M2BatchUniforms& uniforms, const bool visible,
    const RenderVec4& tint_rgba, const float alpha) {
  std::lock_guard lock(mutex_);
  return ApplyAttachmentFrameRenderStateLocked(instance_id, world_transform,
                                               &uniforms, {}, visible,
                                               tint_rgba, alpha);
}

void M2InstanceStore::SetAttachmentFrameRenderStates(
    const std::span<const M2AttachmentFrameRenderRequest> requests,
    const std::span<M2ResultStatus> out_statuses) {
  std::lock_guard lock(mutex_);
  for (std::size_t index = 0; index < requests.size(); ++index) {
    const M2AttachmentFrameRenderRequest& request = requests[index];

    out_statuses[index] =
        (request.world_transform == nullptr ||
         (request.uniforms == nullptr && !request.shared_uniforms))
            ? M2ResultStatus::kFailed
            : ApplyAttachmentFrameRenderStateLocked(
                  request.instance_id, *request.world_transform,
                  request.uniforms, request.shared_uniforms, request.visible,
                  request.tint_rgba, request.alpha);
  }
}

M2ResultStatus M2InstanceStore::SetBatchUniforms(
    const std::uint32_t instance_id, const M2BatchUniforms& uniforms) {
  std::lock_guard lock(mutex_);
  const auto found = instances_.find(instance_id);
  if (found == instances_.end()) return M2ResultStatus::kFailed;
  StoreBatchUniformsLocked(*found->second, uniforms);
  return M2ResultStatus::kReady;
}

M2ResultStatus M2InstanceStore::ClearBatchUniforms(
    const std::uint32_t instance_id) {
  std::lock_guard lock(mutex_);
  const auto found = instances_.find(instance_id);
  if (found == instances_.end()) return M2ResultStatus::kFailed;
  ReleaseInstanceBatchUniformsLocked(*found->second);
  return M2ResultStatus::kReady;
}

M2SharedBatchUniformsHandle M2InstanceStore::PublishSharedBatchUniforms(
    const M2BatchUniforms& uniforms) {
  std::lock_guard lock(mutex_);
  detail::M2SharedBatchUniforms* const block =
      AllocateSharedBatchUniformsLocked();
  block->uniforms = uniforms;
  block->ref_count = 1u;
  return {.block = block};
}

void M2InstanceStore::ReleaseSharedBatchUniforms(
    const M2SharedBatchUniformsHandle handle) {
  if (handle.block == nullptr) return;
  std::lock_guard lock(mutex_);
  ReleaseSharedBatchUniformsLocked(handle.block);
}

M2ResultStatus M2InstanceStore::SetSharedBatchUniforms(
    const std::uint32_t instance_id, const M2SharedBatchUniformsHandle handle) {

  if (handle.block == nullptr) return M2ResultStatus::kFailed;
  std::lock_guard lock(mutex_);
  const auto found = instances_.find(instance_id);
  if (found == instances_.end()) return M2ResultStatus::kFailed;
  return ApplySharedBatchUniformsLocked(*found->second, handle);
}

M2ResultStatus M2InstanceStore::ApplySharedBatchUniformsLocked(
    detail::M2Instance& instance, const M2SharedBatchUniformsHandle handle) {
  if (handle.block == nullptr) return M2ResultStatus::kFailed;
  AdoptSharedBatchUniformsLocked(instance, handle.block);
  return M2ResultStatus::kReady;
}

void M2InstanceStore::StoreBatchUniformsLocked(detail::M2Instance& instance,
                                               const M2BatchUniforms& uniforms) {
  detail::M2SharedBatchUniforms* block = instance.batch_uniforms_block;
  if (block == nullptr || block->ref_count != 1u) {

    ReleaseInstanceBatchUniformsLocked(instance);
    block = AllocateSharedBatchUniformsLocked();
    block->ref_count = 1u;
    instance.batch_uniforms_block = block;
  }
  block->uniforms = uniforms;
}

void M2InstanceStore::AdoptSharedBatchUniformsLocked(
    detail::M2Instance& instance, detail::M2SharedBatchUniforms* const block) {
  if (instance.batch_uniforms_block == block) return;
  ReleaseInstanceBatchUniformsLocked(instance);
  ++block->ref_count;
  instance.batch_uniforms_block = block;
}

void M2InstanceStore::ReleaseInstanceBatchUniformsLocked(
    detail::M2Instance& instance) {
  if (instance.batch_uniforms_block == nullptr) return;
  ReleaseSharedBatchUniformsLocked(instance.batch_uniforms_block);
  instance.batch_uniforms_block = nullptr;
}

detail::M2SharedBatchUniforms*
M2InstanceStore::AllocateSharedBatchUniformsLocked() {
  if (!shared_batch_uniforms_free_.empty()) {
    detail::M2SharedBatchUniforms* const block =
        shared_batch_uniforms_free_.back();
    shared_batch_uniforms_free_.pop_back();
    return block;
  }
  return shared_batch_uniforms_pool_
      .emplace_back(std::make_unique<detail::M2SharedBatchUniforms>())
      .get();
}

void M2InstanceStore::ReleaseSharedBatchUniformsLocked(
    detail::M2SharedBatchUniforms* const block) {
  if (--block->ref_count == 0u) {
    shared_batch_uniforms_free_.push_back(block);
  }
}

M2ResultStatus M2InstanceStore::SetAttachedModelVisualSelectorFlag(
    const std::uint32_t instance_id, const bool enabled) {
  if (instance_id == 0u) return M2ResultStatus::kFailed;
  std::lock_guard lock(mutex_);
  const auto found = instances_.find(instance_id);
  if (found == instances_.end() ||
      !found->second->ready_for_attachment_visual_updates) {
    return found == instances_.end() ? M2ResultStatus::kFailed
                                     : M2ResultStatus::kNotReady;
  }
  found->second->attachment_visual_data_initialized = true;
  bool updated = false;
  for (const auto& link : found->second->child_links) {
    const auto child = instances_.find(link.instance_id);
    if (child == instances_.end()) continue;
    if (enabled) {
      child->second->attachment_visual_flags |=
          kM2AttachmentVisualSelectorFlag;
    } else {
      child->second->attachment_visual_flags &=
          ~kM2AttachmentVisualSelectorFlag;
    }
    updated = true;
  }
  return updated ? M2ResultStatus::kReady : M2ResultStatus::kNotReady;
}

M2PrimaryVisualStateResult M2InstanceStore::EnablePrimaryVisualState(
    const std::uint32_t instance_id, const float duration_seconds) {
  std::lock_guard lock(mutex_);
  const auto found = instances_.find(instance_id);
  if (found == instances_.end()) {
    return {.status = M2ResultStatus::kFailed, .reset_on_detach = true};
  }
  auto& instance = *found->second;
  if (!instance.ready_for_attachment_visual_updates) {
    return {.status = M2ResultStatus::kNotReady, .reset_on_detach = true};
  }
  instance.attachment_visual_data_initialized = true;
  M2PrimaryVisualStateResult result{
      .status = M2ResultStatus::kReady,
      .reset_on_detach = instance.active_primary_visual_count == 0u};
  std::uint32_t duration_ms = duration_seconds > 0.0f
                                  ? static_cast<std::uint32_t>(
                                        duration_seconds * 1000.0f)
                                  : 0u;
  if (duration_ms != 0u && instance.current_animation_duration_ms != 0u &&
      instance.current_animation_duration_ms < duration_ms) {
    duration_ms = instance.current_animation_duration_ms;
  }
  if (duration_ms != 0u) instance.active_primary_visual_duration_ms = duration_ms;
  instance.active_primary_visual_count =
      instance.primary_visual_count == 0u ? 1u : instance.primary_visual_count;
  for (const auto& link : instance.child_links) {
    if (const auto child = instances_.find(link.instance_id);
        child != instances_.end()) {
      child->second->attachment_visual_flags |=
          kM2AttachedPrimaryVisualEnabledFlag;
    }
  }
  for (auto& entry : instance.primary_visual_entries) {
    entry.flags |= kM2PrimaryVisualEntryEnabledFlag;
  }
  return result;
}

M2ResultStatus M2InstanceStore::DisablePrimaryVisualState(
    const std::uint32_t instance_id, const bool reset_on_detach) {
  if (!reset_on_detach) return M2ResultStatus::kReady;
  std::lock_guard lock(mutex_);
  const auto found = instances_.find(instance_id);
  if (found == instances_.end() ||
      !found->second->ready_for_attachment_visual_updates) {
    return M2ResultStatus::kFailed;
  }
  auto& instance = *found->second;
  instance.attachment_visual_data_initialized = true;
  instance.active_primary_visual_count = 0u;
  instance.active_primary_visual_duration_ms = 0u;
  for (const auto& link : instance.child_links) {
    if (const auto child = instances_.find(link.instance_id);
        child != instances_.end()) {
      child->second->attachment_visual_flags &=
          ~kM2AttachedPrimaryVisualEnabledFlag;
    }
  }
  for (auto& entry : instance.primary_visual_entries) {
    entry.flags &= ~kM2PrimaryVisualEntryEnabledFlag;
    entry.flags &= ~kM2PrimaryVisualEntryActiveFlag;
  }
  return M2ResultStatus::kReady;
}

M2ResultStatus M2InstanceStore::ResetPrimaryVisualStateForLightning(
    const std::uint32_t instance_id) {
  if (instance_id == 0u) return M2ResultStatus::kFailed;
  std::lock_guard lock(mutex_);
  const auto found = instances_.find(instance_id);
  if (found == instances_.end()) return M2ResultStatus::kFailed;
  auto& instance = *found->second;
  instance.active_primary_visual_count =
      instance.primary_visual_count == 0u ? 1u : instance.primary_visual_count;
  instance.primary_visual_weight = 0.0f;
  for (const auto& link : instance.child_links) {
    if (const auto child = instances_.find(link.instance_id);
        child != instances_.end()) {
      child->second->attachment_visual_flags &=
          ~kM2AttachedPrimaryVisualEnabledFlag;
    }
  }
  for (auto& entry : instance.primary_visual_entries) {
    entry.flags &= ~kM2PrimaryVisualEntryEnabledFlag;
    entry.flags &= ~kM2PrimaryVisualEntryActiveFlag;
  }
  return M2ResultStatus::kReady;
}

}

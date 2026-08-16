#include "openwow/render/m2/m2_instance_store.h"

#include "openwow/render/m2/m2_model_repository.h"

#include <algorithm>
#include <cstddef>
#include <unordered_set>

namespace openwow::render::m2 {
namespace {

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
#endif

#if !defined(_MSC_VER)
constexpr std::size_t kM2InstanceHotLineBytes = 128u;
constexpr std::size_t kM2InstanceWarmLineBytes = 256u;
constexpr std::size_t kM2InstanceSizeBefore2026_08_15 = 6320u;
static_assert(offsetof(detail::M2Instance, model_id) < kM2InstanceHotLineBytes);
static_assert(offsetof(detail::M2Instance, dirty_flags) < kM2InstanceHotLineBytes);
static_assert(offsetof(detail::M2Instance, visual_revision) < kM2InstanceHotLineBytes);
static_assert(offsetof(detail::M2Instance, world_transform) < kM2InstanceHotLineBytes);
static_assert(offsetof(detail::M2Instance, explicit_world_transform_revision) < kM2InstanceHotLineBytes);
static_assert(offsetof(detail::M2Instance, batch_uniforms_block) < kM2InstanceHotLineBytes);
static_assert(offsetof(detail::M2Instance, tint_color) < kM2InstanceHotLineBytes);
static_assert(offsetof(detail::M2Instance, alpha) < kM2InstanceHotLineBytes);
static_assert(offsetof(detail::M2Instance, animation_time) < kM2InstanceHotLineBytes);
static_assert(offsetof(detail::M2Instance, animation_speed) < kM2InstanceHotLineBytes);
static_assert(offsetof(detail::M2Instance, has_explicit_world_transform) < kM2InstanceHotLineBytes);
static_assert(offsetof(detail::M2Instance, visible) < kM2InstanceHotLineBytes);
static_assert(offsetof(detail::M2Instance, has_visible_submesh_filter) < kM2InstanceHotLineBytes);
static_assert(offsetof(detail::M2Instance, animation_id) < kM2InstanceWarmLineBytes);
static_assert(offsetof(detail::M2Instance, animation_sequence_index) < kM2InstanceWarmLineBytes);
static_assert(offsetof(detail::M2Instance, active_animation_slot_count) < kM2InstanceWarmLineBytes);
static_assert(offsetof(detail::M2Instance, animation_state_generation) < kM2InstanceWarmLineBytes);
static_assert(offsetof(detail::M2Instance, pose_blend_remaining) < kM2InstanceWarmLineBytes);
static_assert(offsetof(detail::M2Instance, current_animation_duration_ms) < kM2InstanceWarmLineBytes);
static_assert(offsetof(detail::M2Instance, base_loop_boundaries_pending) < kM2InstanceWarmLineBytes);
static_assert(offsetof(detail::M2Instance, slot_loop_wrapped_mask) < kM2InstanceWarmLineBytes);
static_assert(offsetof(detail::M2Instance, last_rendered_effect_frame) < kM2InstanceWarmLineBytes);
static_assert(offsetof(detail::M2Instance, visible_submesh_indices) < kM2InstanceWarmLineBytes);
static_assert(offsetof(detail::M2Instance, child_links) < kM2InstanceWarmLineBytes);
static_assert(offsetof(detail::M2Instance, query_pose_cache) <= kM2InstanceWarmLineBytes);
static_assert(sizeof(detail::M2Instance) <= kM2InstanceSizeBefore2026_08_15);
static_assert(sizeof(detail::M2PoseCache) <= 128u);
#endif
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

M2InstanceAttachmentVisualState BuildAttachmentVisualState(
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
  state.primary_visual_entry_flags.reserve(
      instance.primary_visual_entries.size());
  for (const auto& entry : instance.primary_visual_entries) {
    state.primary_visual_entry_flags.push_back(entry.flags);
  }
  return state;
}

M2InstanceInfo BuildInstanceInfo(const detail::M2Instance& instance) {
  M2InstanceInfo info;
  info.model_id = instance.model_id;
  info.visual_revision = instance.visual_revision;
  info.position = instance.position;
  info.rotation_degrees = instance.rotation;
  info.scale = instance.scale;
  info.world_transform = instance.world_transform;
  info.has_explicit_world_transform = instance.has_explicit_world_transform;
  info.world_transform_dirty =
      (instance.dirty_flags & kM2InstanceDirtyWorldTransform) != 0u;
  info.requested_animation_id = instance.animation_id;
  info.resolved_animation_id = instance.resolved_animation_id;
  info.animation_sequence_index = instance.animation_sequence_index;
  info.animation_sub_index = instance.animation_sub_index;
  info.animation_lookup_id = instance.animation_lookup_id;
  info.animation_lookup_sequence_index = instance.animation_lookup_sequence_index;
  info.animation_loop_count = instance.animation_loop_count;
  info.animation_used_random_variant = instance.animation_used_random_variant;
  info.animation_time_seconds = instance.animation_time;
  info.animation_speed = instance.animation_speed;
  info.processed_event_time_ms = instance.processed_event_time_ms;
  info.animation_slots = instance.animation_slots;
  info.visible = instance.visible;
  info.parent_instance_id = instance.parent_instance_id;
  info.child_links.reserve(instance.child_links.size());
  for (const auto& child : instance.child_links) {
    info.child_links.push_back({.instance_id = child.instance_id,
                                .destroy_policy = child.destroy_policy,
                                .attachment_slot = child.attachment_slot});
  }
  info.has_animation_changed_callback =
      static_cast<bool>(instance.animation_changed_callback);
  info.has_animation_request_callback =
      static_cast<bool>(instance.animation_request_callback);
  info.has_animation_completion_callback =
      static_cast<bool>(instance.animation_completion_callback);
  info.has_triggered_event_callback =
      static_cast<bool>(instance.triggered_event_callback);
  info.destroy_in_progress = instance.destroy_in_progress;
  info.effect_context = instance.effect_context;
  info.attachment_visual_state = BuildAttachmentVisualState(instance);
  if (instance.pose_playback_bound) {
    info.pose_playback = M2PosePlaybackInfo{
        .bound = true,
        .blending = instance.IsPoseBlending(),
        .current_sequence_index = instance.pose_sequence_index,
        .blend_source_sequence_index = instance.GetBlendSourceSequenceIndex(),
        .current_time = instance.animation_time,
        .speed = instance.animation_speed};
  }
  info.texture_overrides.reserve(instance.texture_overrides.size());
  for (const auto& override : instance.texture_overrides) {
    info.texture_overrides.push_back(
        {.type_id = override.type_id,
         .texture_path = override.texture_path,
         .texture_loaded = bgfx::isValid(override.texture)});
  }
  info.particle_color_overrides.reserve(
      instance.particle_color_overrides.size());
  for (const auto& override : instance.particle_color_overrides) {
    info.particle_color_overrides.push_back(
        {.color_index = override.color_index,
         .start_raw = override.start_raw,
         .mid_raw = override.mid_raw,
         .end_raw = override.end_raw});
  }
  info.has_replacement_colors = instance.has_replacement_colors;
  info.replacement_colors = instance.replacement_colors;
  info.has_visible_submesh_filter = instance.has_visible_submesh_filter;
  info.visible_submesh_filter_dirty =
      (instance.dirty_flags & kM2InstanceDirtyVisibleSubmeshes) != 0u;
  info.visible_submesh_indices = instance.visible_submesh_indices;
  info.tint_color = instance.tint_color;
  info.alpha = instance.alpha;
  info.selection_glow_color = instance.selection_glow_color;
  info.has_batch_uniforms = instance.batch_uniforms_block != nullptr;
  info.effect_emitters_enabled = instance.effect_emitters_enabled;
  info.transient_weapon_trail = instance.transient_weapon_trail;
  info.particle_system_bound = instance.particle_system_bound;
  info.ribbon_system_initialized = instance.ribbon_system_initialized;
  return info;
}

void RemoveChildLink(std::vector<detail::M2Instance::ChildLink>& links,
                     const std::uint32_t child_instance_id) {
  links.erase(std::remove_if(links.begin(), links.end(),
                             [child_instance_id](const auto& link) {
                               return link.instance_id == child_instance_id;
                             }),
              links.end());
}

}

M2InstanceStore::M2InstanceStore(M2SystemMutex& mutex,
                                 ModelStore& models,
                                 TextureManager*& texture_manager)
    : mutex_(mutex), models_(models), texture_manager_(texture_manager) {

  instances_.rehash(kM2InstanceBucketFloor);
  instance_count_by_model_.rehash(kM2ModelBucketFloor);
  effect_carriers_.rehash(kM2EmitterCarrierBucketFloor);
  lease_control_ = std::make_shared<std::function<void(std::uint32_t)>>(
      [this](const std::uint32_t id) {
        (void)Destroy(id, destroy_callback_collector_);
      });
}

M2InstanceStore::~M2InstanceStore() { RevokeLeases(); }

M2InstanceStoreLease M2InstanceStore::AcquireLease(
    const std::uint32_t root_instance_id) {
  std::lock_guard lock(mutex_);
  if (!instances_.contains(root_instance_id)) {
    return {};
  }
  return M2InstanceStoreLease::Create(lease_control_, root_instance_id);
}

void M2InstanceStore::RevokeLeases() noexcept { lease_control_.reset(); }

void M2InstanceStore::SetDestroyCallbackCollector(
    DestroyCallbackCollector collector) {
  destroy_callback_collector_ = std::move(collector);
}

void M2InstanceStore::BumpVisualRevision(
    detail::M2Instance& instance) noexcept {
  if (++instance.visual_revision == 0u) {
    instance.visual_revision = 1u;
  }
}

void M2InstanceStore::ResetModelDependentState(detail::M2Instance& instance) {
  instance.resolved_animation_id = 0u;
  instance.animation_sequence_index = kInvalidM2AnimationSequenceIndex;
  instance.animation_sub_index = -1;
  instance.animation_lookup_id = -1;
  instance.animation_lookup_sequence_index = 0u;
  instance.animation_loop_count = 0;
  instance.animation_used_random_variant = false;
  instance.processed_event_time_ms = 0u;
  instance.animation_completion_fired = false;
  instance.pose_playback_bound = false;
  instance.pose_sequence_index = kInvalidM2AnimationSequenceIndex;
  instance.pose_blend_source_sequence_index = kInvalidM2AnimationSequenceIndex;
  instance.pose_blend_source_time = 0.0f;
  instance.pose_blend_duration = 0.0f;
  instance.pose_blend_remaining = 0.0f;
  instance.pose_blend_source_duration_ms = 0u;
  instance.pose_blend_source_clamped_at_end = false;
  instance.has_visible_submesh_filter = false;
  instance.visible_submesh_indices.clear();
  instance.dirty_flags |= detail::M2Instance::kDirtyWorldTransform |
                          detail::M2Instance::kDirtyVisibleSubmeshes;
  instance.particle_system = M2ParticleSystem{};
  instance.particle_system_model_key.clear();
  instance.particle_system_bound = false;
  instance.ribbon_system = M2RibbonEmitterSystem{};
  instance.ribbon_system_model_key.clear();
  instance.ribbon_system_initialized = false;
  instance.transient_weapon_trail.reset();
}

M2InstanceCreateResult M2InstanceStore::Create(const std::uint32_t model_id) {
  std::lock_guard lock(mutex_);
  const auto model = models_.find(model_id);
  if (model == models_.end()) {
    return {.status = M2ResultStatus::kFailed,
            .reason = M2ResultReason::kInvalidHandle,
            .detail = "model_id=" + std::to_string(model_id)};
  }
  if (!model->second->loaded) {
    return {.status = M2ResultStatus::kNotReady,
            .reason = M2ResultReason::kMissingFile,
            .detail = model->second->model_path};
  }
  const std::uint32_t id = next_instance_id_++;
  auto instance = std::make_unique<detail::M2Instance>();
  instance->model_id = model_id;
  detail::M2Instance& record = *instance;
  instances_.emplace(id, std::move(instance));
  RetainModelInstanceLocked(model_id);
  SyncEffectCarrierLocked(id, record, model_id);
  return {.status = M2ResultStatus::kReady, .instance_id = id};
}

M2ResultStatus M2InstanceStore::Destroy(
    const std::uint32_t instance_id,
    const DestroyCallbackCollector& collect_callbacks) {
  DeferredCallbacks callbacks;
  {
    std::lock_guard lock(mutex_);
    if (!instances_.contains(instance_id)) {
      return M2ResultStatus::kFailed;
    }
    DestroyRecursiveLocked(instance_id, collect_callbacks, &callbacks);
  }
  for (auto& callback : callbacks) {
    callback();
  }
  return M2ResultStatus::kReady;
}

void M2InstanceStore::DestroyByModelLocked(
    const std::uint32_t model_id,
    const DestroyCallbackCollector& collect_callbacks,
    DeferredCallbacks* callbacks) {
  std::vector<std::uint32_t> ids;
  for (const auto& [id, instance] : instances_) {
    if (instance->model_id == model_id) ids.push_back(id);
  }
  for (const auto id : ids) {
    DestroyRecursiveLocked(id, collect_callbacks, callbacks);
  }
}

void M2InstanceStore::DestroyRecursiveLocked(
    const std::uint32_t instance_id,
    const DestroyCallbackCollector& collect_callbacks,
    DeferredCallbacks* callbacks) {
  const auto found = instances_.find(instance_id);
  if (found == instances_.end() || found->second->destroy_in_progress) return;
  found->second->destroy_in_progress = true;
  if (collect_callbacks) {
    if (const auto model = models_.find(found->second->model_id);
        model != models_.end()) {
      collect_callbacks(*found->second, *model->second, callbacks);
    }
  }
  const auto children = found->second->child_links;
  for (const auto& child : children) {
    if (child.destroy_policy == M2ChildDestroyPolicy::kDestroyWithParent) {
      DestroyRecursiveLocked(child.instance_id, collect_callbacks, callbacks);
    }
  }
  EraseLocked(instance_id);
}

void M2InstanceStore::EraseLocked(const std::uint32_t instance_id) {
  const auto found = instances_.find(instance_id);
  if (found == instances_.end()) return;
  if (found->second->parent_instance_id != 0u) {
    if (const auto parent = instances_.find(found->second->parent_instance_id);
        parent != instances_.end()) {
      const auto old_size = parent->second->child_links.size();
      RemoveChildLink(parent->second->child_links, instance_id);
      if (old_size != parent->second->child_links.size()) {
        BumpVisualRevision(*parent->second);
      }
    }
  }
  for (const auto& child : found->second->child_links) {
    if (const auto entry = instances_.find(child.instance_id);
        entry != instances_.end()) {
      entry->second->parent_instance_id = 0u;
      BumpVisualRevision(*entry->second);
    }
  }
  const std::uint32_t bound_model_id = found->second->model_id;
  ReleaseInstanceBatchUniformsLocked(*found->second);
  instances_.erase(found);
  ReleaseModelInstanceLocked(bound_model_id);
  effect_carriers_.erase(instance_id);
}

void M2InstanceStore::ResetLocked() {
  for (auto& [instance_id, instance] : instances_) {
    ReleaseInstanceBatchUniformsLocked(*instance);
  }
  instances_.clear();
  instance_count_by_model_.clear();
  effect_carriers_.clear();
}

bool M2InstanceStore::HasInstancesOfModel(
    const std::uint32_t model_id) const noexcept {
  return instance_count_by_model_.contains(model_id);
}

void M2InstanceStore::RetainModelInstanceLocked(const std::uint32_t model_id) {
  ++instance_count_by_model_[model_id];
}

void M2InstanceStore::ReleaseModelInstanceLocked(
    const std::uint32_t model_id) noexcept {
  const auto found = instance_count_by_model_.find(model_id);
  if (found == instance_count_by_model_.end()) {
    return;
  }
  if (--found->second == 0u) {
    instance_count_by_model_.erase(found);
  }
}

void M2InstanceStore::SyncEffectCarrierLocked(const std::uint32_t instance_id,
                                              detail::M2Instance& instance,
                                              const std::uint32_t model_id) {
  const auto model = models_.find(model_id);
  const bool carries_emitters =
      model != models_.end() && model->second->loaded &&
      (!model->second->model_data.particle_emitters.empty() ||
       !model->second->model_data.ribbon_emitters.empty());
  if (carries_emitters) {
    effect_carriers_.insert_or_assign(instance_id, &instance);
  } else {
    effect_carriers_.erase(instance_id);
  }
}

M2InstanceInfoQuery M2InstanceStore::QueryInfo(
    const std::uint32_t instance_id) const {
  std::lock_guard lock(mutex_);
  const auto found = instances_.find(instance_id);
  if (found == instances_.end()) {
    return {.status = M2ResultStatus::kFailed,
            .reason = M2ResultReason::kInvalidHandle,
            .detail = "instance_id=" + std::to_string(instance_id)};
  }
  return {.status = M2ResultStatus::kReady,
          .info = BuildInstanceInfo(*found->second)};
}

M2VisualTreeRevisionQuery M2InstanceStore::QueryVisualTreeRevision(
    const std::uint32_t source_instance_id) const {
  if (source_instance_id == 0u) {
    return {.status = M2ResultStatus::kFailed,
            .reason = M2ResultReason::kInvalidHandle,
            .detail = "visual source instance is zero"};
  }
  constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
  constexpr std::uint64_t kFnvPrime = 1099511628211ull;
  std::uint64_t revision = kFnvOffset;
  const auto hash = [&revision](const std::uint64_t value) {
    for (std::uint32_t byte = 0; byte < 8u; ++byte) {
      revision ^= (value >> (byte * 8u)) & 0xffu;
      revision *= kFnvPrime;
    }
  };
  std::lock_guard lock(mutex_);
  std::vector<std::uint32_t> pending{source_instance_id};
  std::unordered_set<std::uint32_t> visited;
  for (std::size_t index = 0; index < pending.size(); ++index) {
    const auto id = pending[index];
    if (!visited.insert(id).second) {
      return {.status = M2ResultStatus::kFailed,
              .reason = M2ResultReason::kInvalidHandle,
              .detail = "visual attachment cycle at instance_id=" +
                        std::to_string(id)};
    }
    const auto found = instances_.find(id);
    if (found == instances_.end()) {
      return {.status = M2ResultStatus::kFailed,
              .reason = M2ResultReason::kInvalidHandle,
              .detail = "instance_id=" + std::to_string(id)};
    }
    const auto model = models_.find(found->second->model_id);
    if (model == models_.end()) {
      return {.status = M2ResultStatus::kFailed,
              .reason = M2ResultReason::kInvalidHandle,
              .detail = "model_id=" + std::to_string(found->second->model_id)};
    }
    if (!model->second->loaded || !model->second->IsReadyForRender()) {
      return {.status = M2ResultStatus::kNotReady,
              .reason = model->second->loaded
                            ? M2ResultReason::kGpuGeometryNotReady
                            : M2ResultReason::kMissingFile,
              .detail = model->second->model_path};
    }
    hash(id);
    hash(found->second->model_id);
    hash(found->second->visual_revision);
    hash(found->second->child_links.size());
    for (const auto& child : found->second->child_links) {
      if (child.instance_id == 0u) continue;
      hash(child.instance_id);
      hash(static_cast<std::uint32_t>(child.attachment_slot));
      pending.push_back(child.instance_id);
    }
  }
  return {.status = M2ResultStatus::kReady,
          .revision = revision == 0u ? 1u : revision};
}

M2InstanceModelQuery M2InstanceStore::QueryModel(
    const std::uint32_t instance_id) const {
  std::lock_guard lock(mutex_);
  const auto found = instances_.find(instance_id);
  if (found == instances_.end()) {
    return {.status = M2ResultStatus::kFailed,
            .reason = M2ResultReason::kInvalidHandle,
            .detail = "instance_id=" + std::to_string(instance_id)};
  }
  return {.status = M2ResultStatus::kReady,
          .model_id = found->second->model_id};
}

M2ResultStatus M2InstanceStore::SetModel(const std::uint32_t instance_id,
                                         const std::uint32_t model_id) {
  std::lock_guard lock(mutex_);
  const auto instance = instances_.find(instance_id);
  const auto model = models_.find(model_id);
  if (instance == instances_.end() || model == models_.end() ||
      !model->second->loaded) {
    return M2ResultStatus::kFailed;
  }
  if (instance->second->model_id != model_id) {
    ReleaseModelInstanceLocked(instance->second->model_id);
    instance->second->model_id = model_id;
    RetainModelInstanceLocked(model_id);
    ResetModelDependentState(*instance->second);
    BumpVisualRevision(*instance->second);
    SyncEffectCarrierLocked(instance_id, *instance->second, model_id);
  }
  return M2ResultStatus::kReady;
}

M2ChildLinksQuery M2InstanceStore::QueryChildLinks(
    const std::uint32_t instance_id) const {
  std::lock_guard lock(mutex_);
  const auto found = instances_.find(instance_id);
  if (found == instances_.end()) {
    return {.status = M2ResultStatus::kFailed,
            .reason = M2ResultReason::kInvalidHandle,
            .detail = "instance_id=" + std::to_string(instance_id)};
  }
  M2ChildLinksQuery result{.status = M2ResultStatus::kReady};
  for (const auto& child : found->second->child_links) {
    result.child_links.push_back({.instance_id = child.instance_id,
                                  .destroy_policy = child.destroy_policy,
                                  .attachment_slot = child.attachment_slot});
  }
  return result;
}

M2ResultStatus M2InstanceStore::AttachChild(
    const std::uint32_t parent_id, const std::uint32_t child_id,
    const std::int32_t attachment_slot,
    const M2ChildDestroyPolicy destroy_policy) {
  if (parent_id == 0u || child_id == 0u || parent_id == child_id) {
    return M2ResultStatus::kFailed;
  }
  std::lock_guard lock(mutex_);
  const auto parent = instances_.find(parent_id);
  const auto child = instances_.find(child_id);
  if (parent == instances_.end() || child == instances_.end()) {
    return M2ResultStatus::kFailed;
  }
  for (std::uint32_t cursor = parent_id; cursor != 0u;) {
    if (cursor == child_id) return M2ResultStatus::kFailed;
    const auto current = instances_.find(cursor);
    if (current == instances_.end()) break;
    cursor = current->second->parent_instance_id;
  }
  const auto find_link = [child_id](auto& links) {
    return std::find_if(links.begin(), links.end(), [child_id](const auto& link) {
      return link.instance_id == child_id;
    });
  };
  if (child->second->parent_instance_id == parent_id) {
    if (auto link = find_link(parent->second->child_links);
        link != parent->second->child_links.end()) {
      const bool changed = link->destroy_policy != destroy_policy ||
                           link->attachment_slot != attachment_slot;
      link->destroy_policy = destroy_policy;
      link->attachment_slot = attachment_slot;
      if (changed) {
        BumpVisualRevision(*parent->second);
        BumpVisualRevision(*child->second);
      }
    }
    child->second->dirty_flags |= detail::M2Instance::kDirtyWorldTransform;
    return M2ResultStatus::kReady;
  }
  if (child->second->parent_instance_id != 0u) {
    if (const auto old_parent =
            instances_.find(child->second->parent_instance_id);
        old_parent != instances_.end()) {
      const auto old_size = old_parent->second->child_links.size();
      RemoveChildLink(old_parent->second->child_links, child_id);
      if (old_size != old_parent->second->child_links.size()) {
        BumpVisualRevision(*old_parent->second);
      }
    }
  }
  child->second->parent_instance_id = parent_id;
  if (auto link = find_link(parent->second->child_links);
      link != parent->second->child_links.end()) {
    link->destroy_policy = destroy_policy;
    link->attachment_slot = attachment_slot;
  } else {
    parent->second->child_links.push_back(
        {child_id, destroy_policy, attachment_slot});
  }
  child->second->dirty_flags |= detail::M2Instance::kDirtyWorldTransform;
  BumpVisualRevision(*parent->second);
  BumpVisualRevision(*child->second);
  return M2ResultStatus::kReady;
}

}

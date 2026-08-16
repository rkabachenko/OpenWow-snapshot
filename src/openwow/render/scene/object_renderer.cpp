
#include "openwow/render/scene/object_renderer.h"

#include "openwow/data/formats/m2/model_path.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/memory/prefetch.h"
#include "openwow/foundation/math/row_major_mat4x4.h"
#include "openwow/game/character_animation.h"
#include "openwow/game/loading_screen_world_entry_gate.h"
#include "openwow/game/object_types.h"
#include "openwow/game/transport_manager.h"
#include "openwow/game/update_fields.h"
#include "openwow/game/violence_level.h"
#include "openwow/render/api/math/render_matrix_math.h"
#include "openwow/render/m2/m2_system.h"
#include "openwow/render/models/characters/bowstring_renderer.h"
#include "openwow/render/models/animation/model_instance_transform.h"
#include "openwow/render/resources/textures/texture_manager.h"
#include "openwow/render/scene/m2_instance_render_cost.h"
#include "openwow/render/scene/occlusion/occlusion_depth_buffer.h"
#include "openwow/world/world_render_pipeline.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <exception>
#include <string_view>

namespace openwow::render {

namespace {

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
#endif
constexpr std::size_t kRenderInstanceHotLineBytes = 128u;
constexpr std::size_t kRenderInstanceWarmLineBytes = 256u;
static_assert(offsetof(RenderInstance, handle) < kRenderInstanceHotLineBytes);
static_assert(offsetof(RenderInstance, guid) < kRenderInstanceHotLineBytes);
static_assert(offsetof(RenderInstance, presentation_slot) < kRenderInstanceHotLineBytes);
static_assert(offsetof(RenderInstance, type_id) < kRenderInstanceHotLineBytes);
static_assert(offsetof(RenderInstance, visible) < kRenderInstanceHotLineBytes);
static_assert(offsetof(RenderInstance, needs_model_load) < kRenderInstanceHotLineBytes);
static_assert(offsetof(RenderInstance, needs_display_resolve) < kRenderInstanceHotLineBytes);
static_assert(offsetof(RenderInstance, visible_submeshes_applied) < kRenderInstanceHotLineBytes);
static_assert(offsetof(RenderInstance, m2_model_id) < kRenderInstanceHotLineBytes);
static_assert(offsetof(RenderInstance, m2_instance_id) < kRenderInstanceHotLineBytes);
static_assert(offsetof(RenderInstance, model_retry_seconds) < kRenderInstanceHotLineBytes);
static_assert(offsetof(RenderInstance, position) < kRenderInstanceHotLineBytes);
static_assert(offsetof(RenderInstance, tint_color) < kRenderInstanceHotLineBytes);
static_assert(offsetof(RenderInstance, alpha) < kRenderInstanceHotLineBytes);
static_assert(offsetof(RenderInstance, character_appearance_declared) < kRenderInstanceHotLineBytes);
static_assert(offsetof(RenderInstance, character_appearance_applied) < kRenderInstanceHotLineBytes);
static_assert(offsetof(RenderInstance, has_destructible_area_scene_states) < kRenderInstanceWarmLineBytes);
static_assert(offsetof(RenderInstance, animation_sample_ready) < kRenderInstanceWarmLineBytes);
static_assert(offsetof(RenderInstance, world_transform) < kRenderInstanceWarmLineBytes);
static_assert(offsetof(RenderInstance, animation) < kRenderInstanceWarmLineBytes);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

constexpr std::size_t kGameObjectArtKitTextureSlotCount = 3u;
constexpr std::uint32_t kGameObjectArtKitTextureTypeBase = 11u;
constexpr std::int32_t kGameObjectM2DefaultRepeatCount = 0;
constexpr std::int32_t kGameObjectM2SequenceRepeatCount = 1;
constexpr float kM2NegativeRetrySeconds = 10.0f;
constexpr std::size_t kMaxEquipmentRequestsPerFrame = 8u;

[[nodiscard]] std::optional<std::uint32_t>
ResolveGameObjectAnimationRequestTransition(
    const std::uint32_t requested_animation_id) noexcept {
  switch (requested_animation_id) {
  case 0x92u:
    return 0x93u;
  case 0x94u:
    return 0x95u;
  case 0xA2u:
    return 0xA3u;
  case 0xA4u:
    return 0u;
  default:
    return std::nullopt;
  }
}

constexpr std::uint32_t kCharacterBodyReplaceableTextureType = 1u;
constexpr std::uint32_t kCharacterCapeReplaceableTextureType = 2u;
constexpr std::uint32_t kCharacterHairReplaceableTextureType = 6u;
constexpr std::uint32_t kCharacterExtraSkinReplaceableTextureType = 8u;
constexpr std::array<std::uint32_t, 4> kCharacterReplaceableTextureTypes{
    kCharacterBodyReplaceableTextureType, kCharacterCapeReplaceableTextureType,
    kCharacterHairReplaceableTextureType,
    kCharacterExtraSkinReplaceableTextureType};

[[nodiscard]] std::uint32_t ResolvePresentationAnimationDurationMs(
    const m2::M2System& system, const RenderInstance& instance) {

  if (instance.m2_instance_id != 0u) {
    const auto info =
        system.QueryInstanceAnimationInfo(instance.m2_instance_id);
    if (info.status == m2::M2ResultStatus::kReady &&
        info.info.requested_animation_id == instance.animation.current_anim() &&
        info.info.sequence_index != m2::kInvalidM2AnimationSequenceIndex &&
        info.info.duration_ms != 0u) {
      return info.info.duration_ms;
    }
  }
  if (instance.m2_model_id != 0u) {
    const auto sequence = system.QueryModelAnimationSequence(
        instance.m2_model_id, instance.animation.current_anim());
    if (sequence.status == m2::M2ResultStatus::kReady &&
        sequence.has_sequence) {
      return sequence.sequence.duration_ms;
    }
  }
  return 0u;
}

[[nodiscard]] std::uint32_t ResolveModelAnimationDurationMs(
    const m2::M2System& system, const RenderInstance& instance,
    const std::uint32_t animation_id) {
  if (instance.m2_model_id == 0u) {
    return 0u;
  }
  const auto sequence =
      system.QueryModelAnimationSequence(instance.m2_model_id, animation_id);
  return sequence.status == m2::M2ResultStatus::kReady && sequence.has_sequence
             ? sequence.sequence.duration_ms
             : 0u;
}

[[nodiscard]] constexpr bool IsSpeedScaledLocomotionAnimation(
    const std::uint32_t animation_id) noexcept {
  switch (animation_id) {
  case AnimId::kWalk:
  case AnimId::kRun:
  case AnimId::kShuffleLeft:
  case AnimId::kShuffleRight:
  case AnimId::kWalkBackwards:
  case AnimId::kJumpStart:
  case AnimId::kJump:
  case AnimId::kJumpEnd:
  case AnimId::kSwim:
  case AnimId::kSwimLeft:
  case AnimId::kSwimRight:
  case AnimId::kSwimBackwards:
  case AnimId::kStealthWalk:
  case AnimId::kFly:
  case AnimId::kSprint:
  case AnimId::kJumpLandRun:
  case AnimId::kStealthRun:
    return true;
  default:
    return false;
  }
}

[[nodiscard]] float ResolveLocomotionPlaybackRate(
    const m2::M2System& system, const RenderInstance& instance,
    const std::uint32_t animation_id, const std::uint32_t movement_flags,
    const float locomotion_speed) {
  constexpr float kUnscaledPlaybackRate = 1.0f;
  constexpr std::uint32_t kDirectionalLocomotionFlags =
      game::kMoveFlagForward | game::kMoveFlagBackward |
      game::kMoveFlagStrafeLeft | game::kMoveFlagStrafeRight |
      game::kMoveFlagAscending | game::kMoveFlagDescending;
  if (instance.m2_model_id == 0u ||
      !IsSpeedScaledLocomotionAnimation(animation_id) ||
      (movement_flags & kDirectionalLocomotionFlags) == 0u) {
    return kUnscaledPlaybackRate;
  }

  if (!(locomotion_speed > 0.0f)) {
    return kUnscaledPlaybackRate;
  }
  const auto sequence =
      system.QueryModelAnimationSequence(instance.m2_model_id, animation_id);
  if (sequence.status != m2::M2ResultStatus::kReady || !sequence.has_sequence ||
      sequence.sequence.move_speed == 0.0f) {
    return kUnscaledPlaybackRate;
  }
  return locomotion_speed / std::abs(sequence.sequence.move_speed);
}

[[nodiscard]] std::uint32_t ResolveLocomotionPhaseStartMs(
    const m2::M2System& system, const RenderInstance& instance,
    const std::uint32_t incoming_animation_id,
    const std::uint32_t movement_flags) {
  constexpr std::uint32_t kStartAtSequenceHead = 0u;
  constexpr std::uint32_t kDirectionalLocomotionFlags =
      game::kMoveFlagForward | game::kMoveFlagBackward |
      game::kMoveFlagStrafeLeft | game::kMoveFlagStrafeRight |
      game::kMoveFlagAscending | game::kMoveFlagDescending;
  if (instance.m2_model_id == 0u ||
      !IsSpeedScaledLocomotionAnimation(incoming_animation_id) ||
      (movement_flags & kDirectionalLocomotionFlags) == 0u) {
    return kStartAtSequenceHead;
  }
  const auto incoming = system.QueryModelAnimationSequence(
      instance.m2_model_id, incoming_animation_id);
  if (incoming.status != m2::M2ResultStatus::kReady || !incoming.has_sequence ||
      incoming.sequence.move_speed == 0.0f ||
      incoming.sequence.duration_ms == 0u) {
    return kStartAtSequenceHead;
  }
  const auto outgoing = system.QueryModelAnimationSequence(
      instance.m2_model_id, instance.animation.current_anim());
  if (outgoing.status != m2::M2ResultStatus::kReady || !outgoing.has_sequence ||
      outgoing.sequence.duration_ms == 0u ||
      outgoing.sequence.move_speed == 0.0f) {
    return kStartAtSequenceHead;
  }
  const auto scaled =
      static_cast<std::uint64_t>(incoming.sequence.duration_ms) *
      static_cast<std::uint64_t>(instance.animation.current_time_ms());
  return static_cast<std::uint32_t>((scaled / outgoing.sequence.duration_ms) %
                                    incoming.sequence.duration_ms);
}

[[nodiscard]] bool AttachmentVisualsEqual(
    const std::shared_ptr<const WeaponAttachmentVisual> &lhs,
    const std::shared_ptr<const WeaponAttachmentVisual> &rhs) noexcept {

  if (lhs == rhs) {
    return true;
  }

  static const WeaponAttachmentVisual kAbsentVisual{};
  const WeaponAttachmentVisual &left = lhs != nullptr ? *lhs : kAbsentVisual;
  const WeaponAttachmentVisual &right = rhs != nullptr ? *rhs : kAbsentVisual;
  return left.attachment_id == right.attachment_id &&
         left.model_path == right.model_path &&
         left.texture_path == right.texture_path &&
         left.replaceable_texture_type == right.replaceable_texture_type &&

         left.requires_bowstring == right.requires_bowstring &&
         left.item_visual_children == right.item_visual_children;
}

[[nodiscard]] bool AttachmentSpecsEqual(const ModelAttachmentSpec &lhs,
                                        const ModelAttachmentSpec &rhs) noexcept {
  return lhs.role == rhs.role && lhs.scale == rhs.scale &&
         lhs.animation_id == rhs.animation_id &&
         AttachmentVisualsEqual(lhs.visual, rhs.visual);
}

void ReleaseItemVisualChildren(m2::M2System &m2_system, ModelAttachmentBinding &binding,
                               const bool destroy_instances) {
  auto &system = m2_system;
  for (auto &child : binding.item_visual_children) {
    if (destroy_instances && child.m2_instance_id != 0u) {
      static_cast<void>(system.DestroyInstance(child.m2_instance_id));
    }
    if (child.stream_ticket) {
      m2_system.ReleaseModelAsync(child.stream_ticket);
    }
  }
  binding.item_visual_children.clear();
}

void ResetItemVisualChildInstances(ModelAttachmentBinding &binding) {
  for (auto &child : binding.item_visual_children) {
    child.m2_model_id = 0u;
    child.m2_instance_id = 0u;
    child.bound_model_path.clear();
  }
}

void DestroyItemVisualChildInstances(m2::M2System &m2_system, ModelAttachmentBinding &binding) {
  auto &system = m2_system;
  for (auto &child : binding.item_visual_children) {
    if (child.m2_instance_id != 0u) {
      static_cast<void>(system.DestroyInstance(child.m2_instance_id));
    }
  }
  ResetItemVisualChildInstances(binding);
}

void ReconcileItemVisualChildren(m2::M2System &m2_system, ModelAttachmentBinding &binding) {

  static const std::vector<ItemVisualChild> kNoItemVisualChildren;
  const auto &desired = binding.desired.visual != nullptr
                            ? binding.desired.visual->item_visual_children
                            : kNoItemVisualChildren;
  bool equal = desired.size() == binding.item_visual_children.size();
  if (equal) {
    for (std::size_t index = 0u; index < desired.size(); ++index) {
      if (!(desired[index] == binding.item_visual_children[index].desired)) {
        equal = false;
        break;
      }
    }
  }
  if (equal) {
    return;
  }

  ReleaseItemVisualChildren(m2_system, binding, true);
  binding.item_visual_children.reserve(desired.size());
  for (const auto &child : desired) {
    binding.item_visual_children.push_back({.desired = child});
  }
}

[[nodiscard]] std::string BuildCreatureDisplayTexturePath(const std::string_view model_path,
                                                          const std::string_view texture_name) {
  if (texture_name.empty()) {
    return {};
  }
  const auto separator = model_path.find_last_of("\\/");
  if (separator == std::string_view::npos) {
    return std::string(texture_name);
  }
  std::string path(model_path.substr(0u, separator + 1u));
  path.append(texture_name);
  return path;
}

[[nodiscard]] RenderMatrix4x4 BuildM2InstanceModelMatrix(const RenderInstance &inst,
                                                         const MountRenderer &mount_renderer) {
  if (inst.has_explicit_world_transform) {
    return inst.world_transform;
  }

  float render_x = inst.position[0];
  float render_y = inst.position[1];
  float render_z = inst.position[2];

  if (inst.is_mounted) {
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    float offset_z = 0.0f;
    if (mount_renderer.GetRiderOffset(inst.guid, offset_x, offset_y, offset_z)) {
      render_x += offset_x;
      render_y += offset_y;
      render_z += offset_z;
    }
  }

  return BuildM2ModelInstanceTransform(render_x, render_y, render_z,
                                       inst.orientation, inst.scale);
}

[[nodiscard]] bool HasPathSuffix(const std::string &path, const std::string_view suffix) {
  return path.size() >= suffix.size() && std::equal(suffix.rbegin(), suffix.rend(), path.rbegin());
}

[[nodiscard]] bool UsesGameObjectM2Animation(const RenderInstance &instance) {
  return instance.type_id == game::TypeID::kGameObject && instance.game_object_m2_animation.active;
}

[[nodiscard]] bool UsesDynamicObjectM2Animation(const RenderInstance &instance) {
  return instance.type_id == game::TypeID::kDynamicObject;
}

[[nodiscard]] bool HasStrictBounds(const float *const min_xyz, const float *const max_xyz) {
  return min_xyz[0] < max_xyz[0] && min_xyz[1] < max_xyz[1] && min_xyz[2] < max_xyz[2];
}

[[nodiscard]] float ExtractUniformScaleFromWorldTransform(const RenderMatrix4x4 &world_transform) {
  return std::sqrt(world_transform[0] * world_transform[0] +
                   world_transform[1] * world_transform[1] +
                   world_transform[2] * world_transform[2]);
}

void PopulateWorldSpatialState(ModelSpatialQueryResult *const out) {
  if (out == nullptr) {
    return;
  }

  if (HasStrictBounds(out->local_bounds.data(), out->local_bounds.data() + 3)) {
    openwow::math::row_major_mat4x4::TransformAABBByRowMajorAffine4x4(
        out->world_bounds.data(), out->local_bounds.data(), out->world_transform.data());
  }

  if (out->local_bounding_sphere[3] <= 0.001f) {
    return;
  }

  openwow::math::row_major_mat4x4::TransformPointByRowMajorAffine4x4Unbuffered(
      out->world_bounding_sphere.data(), out->local_bounding_sphere.data(),
      out->world_transform.data());
  out->world_bounding_sphere[3] =
      ExtractUniformScaleFromWorldTransform(out->world_transform) * out->local_bounding_sphere[3];
}

}

ObjectRenderer::~ObjectRenderer() {
  Shutdown();
}

bool ObjectRenderer::Initialize() {
  if (initialized_)
    return true;

  character_appearance_mailbox_ = std::make_shared<CharacterAppearanceMailbox>();
  equipment_texture_mailbox_ = std::make_shared<EquipmentTextureMailbox>();
  equipment_renderer_.Initialize();
  mount_renderer_.Initialize();
  mount_renderer_.BindDisplayInfo(&display_info_);

  initialized_ = true;
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo, "ObjectRenderer: initialized");
  return true;
}

void ObjectRenderer::Shutdown() {
  if (!initialized_)
    return;

  if (character_appearance_workers_.IsInitialized()) {
    character_appearance_workers_.Shutdown();
  }
  if (character_appearance_mailbox_ != nullptr) {
    std::lock_guard lock(character_appearance_mailbox_->mutex);
    character_appearance_mailbox_->completions.clear();
  }
  character_appearance_cache_.clear();
  ++character_appearance_cache_generation_;
  character_appearance_mailbox_.reset();
  equipment_texture_cache_.clear();
  equipment_texture_mailbox_.reset();

  ClearPresentation();

  equipment_renderer_.Shutdown();
  mount_renderer_.Shutdown();

  initialized_ = false;
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo, "ObjectRenderer: shutdown");
}

void ObjectRenderer::ClearPresentation() {
  for (auto &[guid, inst] : instances_) {
    (void)guid;
    ReleaseDestructibleM2StateBindings(inst);
    ClearM2Binding(inst);
    ReleaseModelAttachments(inst);
  }
  instances_.clear();
  instances_by_guid_.clear();
  instances_by_slot_.clear();
  priority_instance_ = {};
  mount_renderer_.Clear();
}

void ObjectRenderer::BindDbc(const openwow::data::dbc::DbcLoader *dbc) {
  if (character_appearance_workers_.IsInitialized()) {
    character_appearance_workers_.Shutdown();
  }
  character_appearance_cache_.clear();
  ++character_appearance_cache_generation_;
  character_appearance_mailbox_ = std::make_shared<CharacterAppearanceMailbox>();
  equipment_texture_cache_.clear();
  equipment_texture_mailbox_ = std::make_shared<EquipmentTextureMailbox>();

  dbc_ = dbc;
  display_info_.BindDbc(dbc);
  equipment_renderer_.BindDbc(dbc);

  for (auto &[guid, inst] : instances_) {
    (void)guid;
    inst.creature_display_overrides_applied = false;
    inst.visible_submeshes_applied = false;
    inst.character_appearance_sources.reset();
    inst.character_appearance_geosets = {};
    inst.character_prebaked_body_texture.clear();
    inst.character_appearance_key.reset();
    inst.character_appearance_declared = false;
    inst.character_appearance_selection_initialized = false;
    inst.character_appearance_applied = false;
    inst.submitted_draw_count = 0u;
    if (inst.type_id == game::TypeID::kGameObject) {
      inst.art_kit_visuals_initialized = false;
    }
    if (inst.type_id == game::TypeID::kDynamicObject) {
      inst.dynamic_object_visual_applied = false;
      inst.needs_display_resolve = true;
    }
  }
}

void ObjectRenderer::SetFileLoader(
    std::function<std::vector<std::uint8_t>(const std::string &)> loader) {
  if (character_appearance_workers_.IsInitialized()) {
    character_appearance_workers_.Shutdown();
  }
  character_appearance_cache_.clear();
  ++character_appearance_cache_generation_;
  character_appearance_mailbox_ = std::make_shared<CharacterAppearanceMailbox>();
  equipment_texture_cache_.clear();
  equipment_texture_mailbox_ = std::make_shared<EquipmentTextureMailbox>();
  for (auto &[guid, inst] : instances_) {
    (void)guid;
    if (inst.character_appearance_declared) {
      inst.character_appearance_applied = false;
      inst.submitted_draw_count = 0u;
    }
  }

  file_loader_ = loader;
  m2_system_.SetFileLoader(file_loader_);
  m2_system_.SetAsyncFileLoader(file_loader_);
  mount_renderer_.SetFileLoader(loader);
}

void ObjectRenderer::ConsumePresentation(ObjectRenderPresentationSnapshot &presentation,
                                         const game::ObjectPresentationSnapshot &objects) {
  if (!initialized_) {
    return;
  }
  priority_instance_ = presentation.local_player;
  const auto forget_slot = [this](const RenderInstance &instance) {
    if (instance.presentation_slot < instances_by_slot_.size() &&
        instances_by_slot_[instance.presentation_slot].instance == &instance) {
      instances_by_slot_[instance.presentation_slot] = {};
    }
  };
  for (const auto retired : presentation.retired) {
    if (const auto it = instances_.find(retired); it != instances_.end()) {
      forget_slot(it->second);
      RemoveInstance(retired);
    }
  }

  consume_instances_scratch_.clear();
  consume_instances_scratch_.reserve(presentation.active.size());
  for (const auto &projection : presentation.active) {
    const bool has_slot = projection.presentation_slot != game::kNoPresentationSlot;
    if (has_slot) {
      if (projection.presentation_slot >= instances_by_slot_.size()) {
        instances_by_slot_.resize(static_cast<std::size_t>(projection.presentation_slot) + 1u);
      }
      const auto &slot = instances_by_slot_[projection.presentation_slot];
      if (slot.instance != nullptr && slot.handle == projection.handle) {
        consume_instances_scratch_.push_back(slot.instance);
        continue;
      }
    }
    const auto found = instances_.find(projection.handle);
    RenderInstance *const instance = found != instances_.end() ? &found->second : nullptr;
    if (instance != nullptr && has_slot) {

      if (instance->presentation_slot != projection.presentation_slot) {
        forget_slot(*instance);
        instance->presentation_slot = projection.presentation_slot;
      }
      instances_by_slot_[projection.presentation_slot] = {.handle = projection.handle,
                                                          .instance = instance};
    }
    consume_instances_scratch_.push_back(instance);
  }

  bool instances_erased = false;
  for (std::size_t index = 0u; index < presentation.active.size(); ++index) {
    if (!instances_erased) {
      memory::PrefetchAheadForRead(consume_instances_scratch_.data(), index,
                                   consume_instances_scratch_.size());
    }
    auto &projection = presentation.active[index];
    RenderInstance *found = consume_instances_scratch_[index];
    if (instances_erased) {
      const auto refound = instances_.find(projection.handle);
      found = refound != instances_.end() ? &refound->second : nullptr;
    }
    if (found == nullptr) {
      if (const auto *const superseded = FindInstance(projection.handle.guid);
          superseded != nullptr) {
        forget_slot(*superseded);
        RemoveInstance(superseded->handle);
        instances_erased = true;
      }
      const auto handle = projection.handle;
      const auto guid = projection.handle.guid;
      const auto presentation_slot = projection.presentation_slot;

      const auto emplaced = instances_.try_emplace(handle);
      RenderInstance &instance = emplaced.first->second;
      InitializeInstance(instance, std::move(projection));
      instances_by_guid_.insert_or_assign(guid, &instance);
      instance.presentation_slot = presentation_slot;
      if (presentation_slot != game::kNoPresentationSlot) {
        instances_by_slot_[presentation_slot] = {.handle = handle, .instance = &instance};
      }
      continue;
    }
    ApplyProjection(*found, std::move(projection));
  }
  mount_renderer_.SyncFromSnapshot(objects);
}

void ObjectRenderer::Update(float dt) {
  if (!initialized_)
    return;

  static_cast<void>(m2_system_.PumpAsyncLoading());

  PumpCharacterAppearanceCompletions();
  CommitCharacterAppearanceUploads();
  PumpEquipmentTextureCompletions();
  CommitEquipmentTextures();

  int loads_this_frame = 0;
  std::size_t equipment_requests_this_frame = 0u;
  const auto update_instance = [this, dt, &loads_this_frame,
                                &equipment_requests_this_frame](RenderInstance &inst) {

    if (inst.needs_display_resolve &&
        (inst.type_id == game::TypeID::kCorpse || inst.type_id == game::TypeID::kDynamicObject ||
         display_info_.IsReady())) {

      if (inst.display_id != 0 || inst.type_id == game::TypeID::kCorpse ||
          inst.type_id == game::TypeID::kDynamicObject) {
        ResolveDisplayId(inst);
        inst.needs_display_resolve = false;
      }
    }

    QueueCharacterAppearance(inst);

    inst.model_retry_seconds = std::max(0.0f, inst.model_retry_seconds - std::max(dt, 0.0f));
    PublishStreamedModelForInstance(inst);
    ApplyDynamicObjectVisualState(inst);

    if (inst.m2_instance_id == 0u && inst.needs_model_load && inst.model_retry_seconds == 0.0f &&
        loads_this_frame < kMaxLoadsPerFrame && RequestModelForInstance(inst)) {
      ++loads_this_frame;
    }
    SynchronizeInactiveDestructibleM2StateBindings(inst, loads_this_frame);
    for (std::size_t index = 0u; index < inst.destructible_m2_state_bindings.size(); ++index) {
      if (index == inst.destructible_area_scene_active_state) {
        continue;
      }
      auto& binding = inst.destructible_m2_state_bindings[index];
      if (binding.m2_instance_id == 0u) {
        continue;
      }
      const auto status = m2_system_.UpdateAnimation(binding.m2_instance_id, dt);
      if (m2::IsTerminalM2ResultStatus(status)) {
        static_cast<void>(m2_system_.DestroyInstance(binding.m2_instance_id));
        binding.m2_instance_id = 0u;
        binding.m2_model_id = 0u;
        binding.request_failed = true;
      }
    }

    const std::size_t remaining_equipment_requests =
        equipment_requests_this_frame < kMaxEquipmentRequestsPerFrame
            ? kMaxEquipmentRequestsPerFrame - equipment_requests_this_frame
            : 0u;
    equipment_requests_this_frame += UpdateModelAttachments(inst, remaining_equipment_requests);

    ApplyCreatureDisplayOverrides(inst);
    ApplyCharacterAppearance(inst);
    ApplyVisibleSubmeshes(inst);

    if (UsesGameObjectM2Animation(inst)) {
      ApplyGameObjectM2AnimationRequest(inst);
      if (inst.m2_instance_id != 0u) {
        const auto animation_status = m2_system_.UpdateAnimation(inst.m2_instance_id, dt);
        if (m2::IsTerminalM2ResultStatus(animation_status)) {
          ClearM2Binding(inst);
        }
      }
      return;
    }

    if (UsesDynamicObjectM2Animation(inst)) {
      if (inst.m2_instance_id != 0u) {
        const auto animation_status = m2_system_.UpdateAnimation(inst.m2_instance_id, dt);
        if (m2::IsTerminalM2ResultStatus(animation_status)) {
          ClearM2Binding(inst);
        }
      }
      return;
    }

    inst.animation_playback_rate =
        ResolveLocomotionPlaybackRate(m2_system_, inst,
                                      inst.animation.current_anim(),
                                      inst.movement_flags,
                                      inst.locomotion_speed);
    const std::uint32_t anim_duration_ms =
        ResolvePresentationAnimationDurationMs(m2_system_, inst);
    inst.animation.Update(dt * inst.animation_playback_rate, anim_duration_ms);

    if (inst.unit_animation.upper_body_only) {
      inst.upper_animation.Update(
          dt, ResolveModelAnimationDurationMs(
                  m2_system_, inst, inst.upper_animation.current_anim()));
    }
    ApplyUpperBodyAnimationChannel(inst);

    const bool request_animation_completed =
        inst.unit_animation.upper_body_only
            ? inst.upper_animation.DidAnimationComplete()
            : inst.animation.DidAnimationComplete();

    if ((inst.type_id == game::TypeID::kUnit || inst.type_id == game::TypeID::kPlayer) &&
        request_animation_completed &&
        (inst.unit_animation.looping ||
         inst.emitted_unit_animation_completion_serial != inst.unit_animation.serial)) {
      inst.emitted_unit_animation_completion_serial = inst.unit_animation.serial;
      if (unit_animation_completion_sink_) {
        unit_animation_completion_sink_({
            .owner = inst.handle,
            .animation_id = inst.unit_animation.animation_id,
            .request_serial = inst.unit_animation.serial,
        });
      }
    }
  };

  if (!priority_instance_.guid.IsEmpty()) {
    if (auto priority = instances_.find(priority_instance_); priority != instances_.end()) {
      update_instance(priority->second);
    }
  }

  for (auto &[handle, inst] : instances_) {
    if (handle == priority_instance_) {
      continue;
    }
    update_instance(inst);
  }

  if (!attachment_animation_batch_.empty()) {
    m2_system_.UpdateAnimations(attachment_animation_batch_, dt, nullptr);
    attachment_animation_batch_.clear();
  }

  mount_renderer_.Update(dt);
}

void ObjectRenderer::PrepareVisibleInstances(
    const std::span<const game::ObjectPresentationRecord> active,
    const FrameVisibilityFilters &filters,
    std::vector<std::uint64_t> &out_entity_ids,
    std::vector<std::array<float, 4>> &out_bounding_spheres) {
  out_entity_ids.clear();
  out_bounding_spheres.clear();
  if (!initialized_) {
    return;
  }
  out_entity_ids.reserve(active.size());
  out_bounding_spheres.reserve(active.size());

  ++animation_sample_frame_;

  static constexpr std::array<float, 4> kNoWorldBoundingSphere{0.0f, 0.0f, 0.0f, 0.0f};

  prepare_admitted_scratch_.clear();
  prepare_admitted_scratch_.reserve(active.size());
  for (const auto &object : active) {
    if (filters.admit && !filters.admit(object)) {
      continue;
    }
    prepare_admitted_scratch_.push_back(&object);
  }

  prepare_instances_scratch_.clear();
  prepare_instances_scratch_.reserve(prepare_admitted_scratch_.size());
  for (const auto *const record : prepare_admitted_scratch_) {
    prepare_instances_scratch_.push_back(FindInstance(record->handle.guid));
  }

  m2::M2InstanceFramePrepareScope prepare(m2_system_);
  for (std::size_t index = 0u; index < prepare_admitted_scratch_.size(); ++index) {
    memory::PrefetchAheadForRead(prepare_instances_scratch_.data(), index,
                                 prepare_instances_scratch_.size());
    const auto &object = *prepare_admitted_scratch_[index];
    auto *const found = prepare_instances_scratch_[index];
    ModelSpatialQueryResult spatial{};
    bool has_spatial = false;
    bool pose_installed = false;
    if (found != nullptr) {
      RenderMatrix4x4 world_transform{};

      const auto prepared = prepare.PrepareSpatial(
          BuildM2FrameSpatialRequest(*found, &world_transform, true));
      pose_installed = prepared.pose_installed;
      if (prepared.spatial_ready) {
        spatial.world_transform = world_transform;
        spatial.local_bounds = prepared.spatial.local_bounds;
        spatial.local_bounding_sphere = prepared.spatial.local_bounding_sphere;
        spatial.world_bounds.fill(0.0f);
        spatial.world_bounding_sphere.fill(0.0f);
        PopulateWorldSpatialState(&spatial);
        has_spatial = true;
      }
    }
    if (filters.admit_bounding_sphere &&
        !filters.admit_bounding_sphere(spatial.world_bounding_sphere, has_spatial)) {
      continue;
    }
    out_entity_ids.push_back(object.handle.guid.GetRawValue());
    out_bounding_spheres.push_back(has_spatial ? spatial.world_bounding_sphere
                                               : kNoWorldBoundingSphere);

    if (found == nullptr) {
      continue;
    }
    auto &inst = *found;

    if (!inst.visible)
      continue;
    if (inst.character_appearance_declared && !inst.character_appearance_applied)
      continue;

    if (inst.m2_instance_id == 0u || !prepare.QueryRenderReady(inst.m2_instance_id))
      continue;
    inst.animation_sample_frame = animation_sample_frame_;

    inst.animation_sample_ready =
        pose_installed &&
        prepare.SamplePresentation(BuildM2FramePresentationRequest(inst));
  }
}

bool ObjectRenderer::ResolveFrameAnimationSample(
    m2::M2InstanceFramePrepareScope &prepare, RenderInstance &inst) {
  if (inst.animation_sample_frame == animation_sample_frame_) {
    return inst.animation_sample_ready;
  }

  inst.animation_sample_frame = animation_sample_frame_;

  prepare.Suspend();
  inst.animation_sample_ready = PrepareM2InstanceQuery(inst);
  prepare.Resume();
  return inst.animation_sample_ready;
}

void ObjectRenderer::Render(std::uint8_t view_id, const float *view_mtx, const float *proj_mtx,
                            float screen_w, float screen_h,
                            const std::span<const std::uint64_t> entity_ids) {
  RenderPass(view_id, view_mtx, proj_mtx, screen_w, screen_h, m2::M2RenderPassScope::kOpaqueOnly,
             entity_ids, nullptr);
}

void ObjectRenderer::RenderTransparent(std::uint8_t view_id, const float *view_mtx,
                                       const float *proj_mtx, float screen_w, float screen_h,
                                       const std::span<const std::uint64_t> entity_ids,
                                       m2::M2TransparentDrawOrder &transparent_draw_order) {
  RenderPass(view_id, view_mtx, proj_mtx, screen_w, screen_h,
             m2::M2RenderPassScope::kTransparentOnly, entity_ids, &transparent_draw_order);
}

void ObjectRenderer::ReserveTransparentDrawOrdinals(m2::M2TransparentDrawOrder &order) {

  const bool bowstrings_enabled =
      bowstring_renderer_ != nullptr && bowstring_renderer_->is_initialized();
  for (RenderInstance *const found : render_pass_instances_scratch_) {
    if (found == nullptr) {
      continue;
    }
    RenderInstance &inst = *found;
    if (!inst.visible) {
      continue;
    }
    if (inst.character_appearance_declared && !inst.character_appearance_applied) {
      continue;
    }
    if (inst.m2_instance_id != 0u) {
      inst.transparent_draw_ordinal = order.Reserve();
      for (ModelAttachmentBinding &binding : inst.model_attachments) {
        if (binding.m2_instance_id == 0u) {
          continue;
        }
        binding.transparent_draw_ordinal = order.Reserve();
        if (bowstrings_enabled && binding.desired.role == ModelAttachmentRole::kRanged &&
            binding.desired.visual != nullptr && binding.desired.visual->requires_bowstring) {
          binding.bowstring_draw_ordinal = order.Reserve();
        }
        for (ItemVisualChildBinding &item_visual : binding.item_visual_children) {
          if (item_visual.m2_instance_id != 0u) {
            item_visual.transparent_draw_ordinal = order.Reserve();
          }
        }
      }
    }
    if (inst.has_destructible_area_scene_states) {
      for (std::size_t index = 0u; index < inst.destructible_m2_state_bindings.size();
           ++index) {
        if (index == inst.destructible_area_scene_active_state ||
            !inst.destructible_m2_state_runtime[index].visible) {
          continue;
        }
        auto &binding = inst.destructible_m2_state_bindings[index];
        if (binding.m2_instance_id != 0u) {
          binding.transparent_draw_ordinal = order.Reserve();
        }
      }
    }
  }
}

void ObjectRenderer::RenderPass(std::uint8_t view_id, const float *view_mtx, const float *proj_mtx,
                                float screen_w, float screen_h,
                                const m2::M2RenderPassScope pass_scope,
                                const std::span<const std::uint64_t> entity_ids,
                                m2::M2TransparentDrawOrder *const transparent_draw_order) {
  if (!initialized_)
    return;
  const bool transparent_pass = pass_scope == m2::M2RenderPassScope::kTransparentOnly;
  assert((!transparent_pass || transparent_draw_order != nullptr) &&
         "ObjectRenderer::RenderPass: the transparent pass needs the view's draw order");

  bgfx::setViewRect(view_id, 0, 0, static_cast<std::uint16_t>(screen_w),
                    static_cast<std::uint16_t>(screen_h));
  bgfx::setViewTransform(view_id, view_mtx, proj_mtx);
  bgfx::setViewClear(view_id, BGFX_CLEAR_NONE);

  m2::M2BatchUniforms world_uniforms;
  ApplyWorldM2SceneState(world_m2_scene_state_, &world_uniforms);

  m2::M2BatchUniforms character_uniforms = world_uniforms;
  {
    const float multiply = openwow::world::CWorld_GetCharacterAmbientMultiply();
    for (std::size_t channel = 0; channel < 3; ++channel) {
      character_uniforms.light_ambient[channel] =
          std::min(world_uniforms.light_ambient[channel] * multiply, 1.0f);
    }
  }

  const m2::M2SharedBatchUniformsLease world_uniforms_lease(m2_system_, world_uniforms);
  const m2::M2SharedBatchUniformsLease character_uniforms_lease(m2_system_,
                                                                 character_uniforms);
  const PassBatchUniforms pass_uniforms{.world_value = &world_uniforms,
                                        .world = world_uniforms_lease.handle(),
                                        .character = character_uniforms_lease.handle()};

  render_pass_instances_scratch_.clear();
  render_pass_instances_scratch_.reserve(entity_ids.size());
  for (const std::uint64_t raw_guid : entity_ids) {
    render_pass_instances_scratch_.push_back(FindInstance(game::ObjectGuid{raw_guid}));
  }

  if (transparent_pass) {
    ReserveTransparentDrawOrdinals(*transparent_draw_order);
  }

  batch_bodies_.clear();
  batch_body_ids_.clear();
  batch_body_draw_ordinals_.clear();
  {

    m2::M2InstanceFramePrepareScope body_prepare(m2_system_);
    for (std::size_t entity_index = 0u;
         entity_index < render_pass_instances_scratch_.size(); ++entity_index) {
      memory::PrefetchAheadForRead(render_pass_instances_scratch_.data(), entity_index,
                                   render_pass_instances_scratch_.size());
      auto *const found = render_pass_instances_scratch_[entity_index];
      if (found == nullptr) {
        continue;
      }
      auto &inst = *found;
      if (!inst.visible)
        continue;
      if (inst.character_appearance_declared && !inst.character_appearance_applied)
        continue;
      if (PrepareInstanceBodyForSubmit(body_prepare, inst, pass_uniforms)) {
        batch_bodies_.push_back(&inst);
        batch_body_ids_.push_back(inst.m2_instance_id);
        if (transparent_pass) {
          batch_body_draw_ordinals_.push_back(inst.transparent_draw_ordinal);
        }
      }
      if (inst.has_destructible_area_scene_states) {

        body_prepare.Suspend();
        SubmitDestructibleM2StateBindings(
            inst, view_id, view_mtx, pass_scope, pass_uniforms);
        body_prepare.Resume();
      }
    }
  }

  batch_body_results_.assign(batch_body_ids_.size(), {});
  {
    const m2::M2TransparentDrawOrdinalScope draw_order_scope(batch_body_draw_ordinals_);
    m2_system_.RenderInstanceBatch(view_id, batch_body_ids_,
                                   RenderMatrix4x4View{view_mtx, 16u}, pass_scope,
                                   m2_system_.frame_job_system(),
                                   kUnitInstanceRenderMicroseconds,
                                   batch_body_results_);
  }

  batch_attachment_owners_.clear();
  for (std::size_t index = 0; index < batch_bodies_.size(); ++index) {
    auto &inst = *batch_bodies_[index];
    if (ApplyBodyRenderResult(inst, batch_body_results_[index])) {
      batch_attachment_owners_.push_back(&inst);
    }
  }
  RenderInstanceAttachments(batch_attachment_owners_, view_id, view_mtx,
                            pass_scope, pass_uniforms);
}

void ObjectRenderer::RenderMounts(std::uint8_t view_id, const float *view_mtx,
                                  const float *proj_mtx,
                                  const game::ObjectPresentationSnapshot &objects,
                                  m2::M2TransparentDrawOrder &transparent_draw_order) {
  if (!initialized_)
    return;
  mount_renderer_.Render(view_id, view_mtx, proj_mtx, objects, transparent_draw_order);
}

bool ObjectRenderer::IsRuntimeRenderAssetReady(game::ObjectGuid guid) const {
  const auto *const it = FindInstance(guid);
  if (it == nullptr) {
    return false;
  }

  const auto &inst = *it;
  RuntimeRenderAssetReadinessState readiness{};
  switch (inst.render_asset_kind) {
  case RenderAssetKind::kM2:
    readiness.kind = RuntimeRenderAssetKind::kM2;
    readiness.m2_payload_bound = !inst.model_path.empty();
    readiness.m2_payload_ready = IsM2RenderReady(inst);
    break;
  case RenderAssetKind::kAreaScene:
    readiness.kind = RuntimeRenderAssetKind::kAreaScene;
    if (area_scene_readiness_resolver_ != nullptr) {
      const auto ready = area_scene_readiness_resolver_(inst);
      if (ready.has_value()) {
        readiness.area_scene_primary_ready = ready->runtime_primary_ready;
        readiness.area_scene_dependencies_ready = ready->runtime_dependencies_ready;
      }
    }
    break;
  case RenderAssetKind::kUnknown:
  default:
    break;
  }

  if (!openwow::render::IsRuntimeRenderAssetReady(readiness)) {
    return false;
  }
  if (inst.character_appearance_declared && !inst.character_appearance_applied) {
    return false;
  }

  return inst.type_id != game::TypeID::kPlayer || inst.submitted_draw_count != 0u;
}

bool ObjectRenderer::IsModelReadyForAnimation(
    const game::ObjectGuid guid) const {
  const auto *const it = FindInstance(guid);
  if (it == nullptr) {
    return false;
  }

  const auto &inst = *it;
  switch (inst.render_asset_kind) {
  case RenderAssetKind::kM2:
    if (inst.m2_instance_id == 0u) {
      return false;
    }
    return m2_system_.QueryInstanceReadiness(inst.m2_instance_id).status ==
           m2::M2ResultStatus::kReady;
  case RenderAssetKind::kAreaScene:
    if (area_scene_readiness_resolver_ == nullptr) {
      return false;
    }
    if (const auto readiness = area_scene_readiness_resolver_(inst);
        readiness.has_value()) {
      return readiness->runtime_primary_ready;
    }
    return false;
  case RenderAssetKind::kUnknown:
  default:
    return false;
  }
}

std::uint32_t
ObjectRenderer::QueryPrimaryM2InstanceId(const game::ObjectHandle handle) const noexcept {
  const auto instance = instances_.find(handle);
  return instance != instances_.end() ? instance->second.m2_instance_id : 0u;
}

bool ObjectRenderer::IsCharacterAppearancePrepared(const game::ObjectGuid guid) const {
  const auto *const instance = FindInstance(guid);
  if (instance == nullptr || !instance->character_appearance_declared ||
      instance->CharacterAppearanceKey().empty()) {
    return false;
  }
  if (instance->character_appearance_applied) {
    return true;
  }

  const auto prepared =
      character_appearance_cache_.find(instance->CharacterAppearanceKey());
  return prepared != character_appearance_cache_.end() &&
         prepared->second.phase == CharacterAppearancePhase::kReady;
}

bool ObjectRenderer::IsLoadingScreenPlayerRenderAssetReady(const game::ObjectGuid guid) const {
  const auto *const it = FindInstance(guid);
  if (it == nullptr) {
    return false;
  }

  const auto &inst = *it;
  return inst.type_id == game::TypeID::kPlayer && inst.visible &&
         inst.render_asset_kind == RenderAssetKind::kM2 && !inst.model_path.empty() &&
         IsM2RenderReady(inst) &&
         (!inst.character_appearance_declared || inst.character_appearance_applied) &&
         inst.visible_submeshes_applied;
}

bool ObjectRenderer::HasSubmittedVisibleDraw(const game::ObjectGuid guid) const {
  const auto *const it = FindInstance(guid);
  return it != nullptr && it->visible && it->submitted_draw_count != 0u;
}

bool ObjectRenderer::IsOpaquePassOcclusionCullable(const game::ObjectGuid guid) {
  if constexpr (!occlusion::kOcclusionCullingEnabled) {
    return false;
  }
  auto *const it = FindInstance(guid);
  if (it == nullptr) {
    return false;
  }
  auto &inst = *it;

  if (!m2_system_.QueryOpaqueDepthTestGuaranteed(inst.m2_instance_id)) {
    return false;
  }

  for (const auto &binding : inst.model_attachments) {
    if (binding.m2_instance_id != 0u) {
      return false;
    }
  }

  if (inst.has_destructible_area_scene_states) {
    return false;
  }

  if (inst.type_id == game::TypeID::kPlayer) {
    return false;
  }

  if (inst.submitted_draw_count == 0u) {
    return false;
  }

  return true;
}

bool ObjectRenderer::IsLoadingScreenTransportRenderAssetReady(game::ObjectGuid guid) const {
  const auto *const it = FindInstance(guid);
  if (it == nullptr) {
    return false;
  }

  const auto &inst = *it;
  openwow::game::LoadingScreenTransportRenderAssetState readiness{};
  switch (inst.render_asset_kind) {
  case RenderAssetKind::kM2:
    readiness.kind = openwow::game::LoadingScreenTransportRenderAssetKind::kM2;
    readiness.is_ready = IsM2RenderReady(inst);
    break;
  case RenderAssetKind::kAreaScene:
    readiness.kind = openwow::game::LoadingScreenTransportRenderAssetKind::kAreaScene;
    if (area_scene_readiness_resolver_ != nullptr) {
      const auto ready = area_scene_readiness_resolver_(inst);
      if (ready.has_value()) {
        readiness.is_ready = ready->loading_screen_ready;
      }
    }
    break;
  case RenderAssetKind::kUnknown:
  default:
    break;
  }

  return openwow::game::IsLoadingScreenTransportRenderAssetReady(readiness);
}

float ObjectRenderer::GetAnimationProgress(const game::ObjectGuid guid) const {
  const auto *const it = FindInstance(guid);
  if (it == nullptr) {
    return 0.0f;
  }

  const auto &instance = *it;
  std::uint32_t time_ms = instance.animation.current_time_ms();
  std::uint32_t duration_ms = 0u;

  if (UsesGameObjectM2Animation(instance) && instance.m2_instance_id != 0u) {
    const auto info = m2_system_.QueryInstanceAnimationInfo(instance.m2_instance_id);
    if (info.status == m2::M2ResultStatus::kReady) {
      time_ms = info.info.time_ms;
      duration_ms = info.info.duration_ms;
    }
  } else if (instance.m2_model_id != 0u) {
    duration_ms =
        ResolvePresentationAnimationDurationMs(m2_system_, instance);
  }

  if (duration_ms == 0u) {
    return 0.0f;
  }

  return std::clamp(static_cast<float>(time_ms) / static_cast<float>(duration_ms), 0.0f, 1.0f);
}

bool ObjectRenderer::QueryModelSpatialState(const game::ObjectGuid guid,
                                            ModelSpatialQueryResult *const out) const {
  if (out == nullptr) {
    return false;
  }

  const auto *const it = FindInstance(guid);
  if (it == nullptr) {
    return false;
  }

  RenderMatrix4x4 world_transform{};
  if (!PrepareM2InstanceSpatialQuery(*it, &world_transform)) {
    return false;
  }

  const auto spatial = m2_system_.QueryInstanceSpatialInfo(it->m2_instance_id);
  if (spatial.status != m2::M2ResultStatus::kReady) {
    return false;
  }

  out->world_transform = world_transform;
  out->local_bounds = spatial.spatial.local_bounds;
  out->local_bounding_sphere = spatial.spatial.local_bounding_sphere;
  out->world_bounds.fill(0.0f);
  out->world_bounding_sphere.fill(0.0f);
  PopulateWorldSpatialState(out);
  return true;
}

bool ObjectRenderer::QueryAreaScenePresentationState(
    const game::ObjectHandle handle, AreaScenePresentationState *const out) const {
  if (out == nullptr) {
    return false;
  }
  const auto it = instances_.find(handle);
  if (it == instances_.end() || it->second.type_id != game::TypeID::kGameObject) {
    return false;
  }

  const RenderInstance &instance = it->second;

  if (!instance.has_destructible_area_scene_states &&
      (instance.render_asset_kind != RenderAssetKind::kAreaScene ||
       instance.model_path.empty())) {
    return false;
  }

  *out = {};
  out->world_transform = BuildM2InstanceModelMatrix(instance, mount_renderer_);
  out->uniform_scale = std::sqrt(
      out->world_transform[0] * out->world_transform[0] +
      out->world_transform[1] * out->world_transform[1] +
      out->world_transform[2] * out->world_transform[2]);
  if (!std::isfinite(out->uniform_scale) || out->uniform_scale <= 0.0f) {
    return false;
  }

  const auto append_wmo_model = [&](const std::uint32_t display_id,
                                    const std::array<std::uint16_t, 3> &doodad_sets,
                                    const std::uint8_t state_index,
                                    const bool visible) {
    if (out->model_count >= out->models.size()) {
      return;
    }
    std::string path = display_info_.ResolveGameObjectModel(display_id);
    if (ClassifyRenderAssetPath(path) != RenderAssetKind::kAreaScene) {
      return;
    }
    out->models[out->model_count++] = {
        .resource_path = std::move(path),
        .destruction_or_init_doodad_set =
            static_cast<std::uint16_t>(
                state_index < instance.destructible_area_scene_states.size()
                    ? instance.destructible_area_scene_states[state_index]
                          .destruction_or_init_doodad_set
                    : 0u),
        .impact_effect_doodad_set =
            static_cast<std::uint16_t>(
                state_index < instance.destructible_area_scene_states.size()
                    ? instance.destructible_area_scene_states[state_index]
                          .impact_effect_doodad_set
                    : 0u),
        .ambient_doodad_set =
            static_cast<std::uint16_t>(
                state_index < instance.destructible_area_scene_states.size()
                    ? instance.destructible_area_scene_states[state_index]
                          .ambient_doodad_set
                    : 0u),
        .additional_doodad_sets = doodad_sets,
        .display_id = display_id,
        .state_index = state_index,
        .visible = visible,
    };
  };

  if (instance.has_destructible_area_scene_states) {
    for (std::size_t index = 0u; index < instance.destructible_area_scene_states.size(); ++index) {
      const auto &state = instance.destructible_area_scene_states[index];
      append_wmo_model(state.display_id, state.additional_doodad_sets,
                       static_cast<std::uint8_t>(index),
                       index == instance.destructible_area_scene_active_state);
    }
  } else {
    append_wmo_model(instance.display_id, instance.area_scene_additional_doodad_sets, 0u, true);
  }
  out->visible = instance.visible;
  out->destructible_active_state = instance.destructible_area_scene_active_state;
  out->destructible_previous_state = instance.destructible_area_scene_previous_state;
  out->destructible_rebuild_effect_display_id =
      instance.destructible_rebuild_effect_display_id;
  if (out->destructible_rebuild_effect_display_id != 0u) {
    const std::string rebuild_effect_path = display_info_.ResolveGameObjectModel(
        out->destructible_rebuild_effect_display_id);
    if (ClassifyRenderAssetPath(rebuild_effect_path) == RenderAssetKind::kAreaScene) {
      out->rebuild_effect_resource_path = rebuild_effect_path;
    }
  }
  if (out->model_count == 0u && out->rebuild_effect_resource_path.empty()) {
    return false;
  }
  if (out->model_count != 0u) {
    const auto active = std::find_if(
        out->models.begin(), out->models.begin() + out->model_count,
        [](const AreaScenePresentationState::Model &model) { return model.visible; });
    const AreaScenePresentationState::Model &primary =
        active != out->models.begin() + out->model_count ? *active : out->models.front();
    out->resource_path = primary.resource_path;
    out->additional_doodad_sets = primary.additional_doodad_sets;
    out->display_id = primary.display_id;
  }
  out->destructible_rebuild_transition_mode =
      instance.destructible_rebuild_transition_mode;
  out->destructible_rebuild_transition_speed =
      instance.destructible_rebuild_transition_speed;
  out->destructible_visual_sync_serial = instance.destructible_visual_sync_serial;
  return true;
}

bool ObjectRenderer::QueryDestructibleM2StateSpatial(
    const game::ObjectHandle handle, const std::uint8_t state_index,
    DestructibleM2StateSpatialQueryResult* const out) const {
  if (out == nullptr) {
    return false;
  }
  *out = {};
  const auto found = instances_.find(handle);
  if (found == instances_.end() || !found->second.has_destructible_area_scene_states ||
      state_index >= found->second.destructible_m2_state_bindings.size()) {
    return false;
  }
  const RenderInstance& instance = found->second;
  const std::uint32_t model_id =
      state_index == instance.destructible_area_scene_active_state &&
              instance.render_asset_kind == RenderAssetKind::kM2
          ? instance.m2_model_id
          : instance.destructible_m2_state_bindings[state_index].m2_model_id;
  if (model_id == 0u) {
    return false;
  }
  const auto spatial = m2_system_.QueryModelSpatialInfo(model_id);
  if (spatial.status != m2::M2ResultStatus::kReady) {
    return false;
  }
  out->local_bounds = spatial.spatial.local_bounds;
  return true;
}

void ObjectRenderer::SetDestructibleM2StatePresentation(
    const game::ObjectHandle handle, const std::uint8_t state_index,
    const bool visible, const float vertical_offset_down) {
  const auto found = instances_.find(handle);
  if (found == instances_.end() || !found->second.has_destructible_area_scene_states ||
      state_index >= found->second.destructible_m2_state_runtime.size() ||
      !std::isfinite(vertical_offset_down)) {
    return;
  }
  RenderInstance& instance = found->second;
  instance.destructible_m2_state_runtime[state_index] = {
      .vertical_offset_down = vertical_offset_down,
      .visible = visible,
  };
  const std::uint32_t m2_instance_id =
      state_index == instance.destructible_area_scene_active_state &&
              instance.render_asset_kind == RenderAssetKind::kM2
          ? instance.m2_instance_id
          : instance.destructible_m2_state_bindings[state_index].m2_instance_id;
  if (m2_instance_id == 0u) {
    return;
  }
  const auto transform = BuildDestructibleM2StateModelMatrix(instance, state_index);

  static_cast<void>(
      m2_system_.SetTransformAndVisibility(m2_instance_id, transform, visible));
}

void ObjectRenderer::SetDestructibleRebuildEffectPresentation(
    const game::ObjectHandle handle, const bool visible) {
  SetDestructibleM2StatePresentation(
      handle, static_cast<std::uint8_t>(RenderInstance::kDestructibleRebuildEffectBindingIndex),
      visible, 0.0f);
}

bool ObjectRenderer::ModelHasSubmeshId(const game::ObjectGuid guid,
                                       const std::uint16_t submesh_id) const {
  const auto *const it = FindInstance(guid);
  if (it == nullptr) {
    return false;
  }

  return m2_system_.ModelHasSubmeshId(it->m2_model_id, submesh_id);
}

bool ObjectRenderer::PrepareM2InstanceSpatialQuery(
    const RenderInstance &inst, RenderMatrix4x4 *const out_world_transform,
    bool *const out_pose_installed) const {

  auto &system = m2_system_;
  const auto model_readiness = inst.m2_model_id != 0u
                                   ? system.QueryModelReadiness(inst.m2_model_id)
                                   : m2::M2ModelReadinessQuery{};
  if (inst.m2_model_id == 0u || inst.m2_instance_id == 0u ||
      model_readiness.status != m2::M2ResultStatus::kReady ||
      !model_readiness.loaded) {
    return false;
  }
  const RenderMatrix4x4 model_matrix =
      inst.has_destructible_area_scene_states
          ? BuildDestructibleM2StateModelMatrix(
                inst, inst.destructible_area_scene_active_state)
          : BuildM2InstanceModelMatrix(inst, mount_renderer_);
  if (system.SetWorldTransformMatrix(inst.m2_instance_id, model_matrix) !=
      m2::M2ResultStatus::kReady) {
    return false;
  }
  if (out_world_transform != nullptr) {
    *out_world_transform = model_matrix;
  }
  if (out_pose_installed != nullptr) {
    *out_pose_installed = true;
  }
  return inst.visible_submeshes_applied;
}

bool ObjectRenderer::PrepareM2InstanceQuery(const RenderInstance &inst) const {

  m2::M2InstanceFramePrepareScope prepare(m2_system_);
  RenderMatrix4x4 world_transform{};
  const auto prepared = prepare.PrepareSpatial(
      BuildM2FrameSpatialRequest(inst, &world_transform, false));
  if (!prepared.pose_installed) {
    return false;
  }
  return prepare.SamplePresentation(BuildM2FramePresentationRequest(inst));
}

m2::M2InstanceFrameSpatialRequest ObjectRenderer::BuildM2FrameSpatialRequest(
    const RenderInstance &inst, RenderMatrix4x4 *const world_transform_storage,
    const bool query_spatial) const {

  const bool has_transform = inst.m2_model_id != 0u && inst.m2_instance_id != 0u;
  if (has_transform) {
    *world_transform_storage =
        inst.has_destructible_area_scene_states
            ? BuildDestructibleM2StateModelMatrix(
                  inst, inst.destructible_area_scene_active_state)
            : BuildM2InstanceModelMatrix(inst, mount_renderer_);
  }
  return {.instance_id = inst.m2_instance_id,
          .model_id = inst.m2_model_id,
          .world_transform = has_transform ? world_transform_storage : nullptr,
          .visible_submeshes_applied = inst.visible_submeshes_applied,
          .query_spatial = query_spatial};
}

m2::M2InstanceFramePresentationRequest
ObjectRenderer::BuildM2FramePresentationRequest(const RenderInstance &inst) {

  const bool visible = inst.has_destructible_area_scene_states &&
                               inst.destructible_area_scene_active_state <
                                   inst.destructible_m2_state_runtime.size()
                           ? inst.destructible_m2_state_runtime[
                                 inst.destructible_area_scene_active_state]
                                 .visible
                           : inst.visible;
  return {
      .instance_id = inst.m2_instance_id,
      .model_id = inst.m2_model_id,

      .sample_animation =
          !UsesGameObjectM2Animation(inst) && !UsesDynamicObjectM2Animation(inst),
      .animation_id = inst.animation.current_anim(),
      .sample_time_ms = inst.animation.current_time_ms(),
      .clamp_to_duration = !inst.animation.is_looping() && inst.m2_model_id != 0u,

      .speed = inst.animation_playback_rate,
      .zero_blend = inst.unit_animation.zero_blend,
      .visible = visible,
      .tint_rgba = inst.tint_color,
      .alpha = inst.alpha,
      .visible_submeshes_applied = inst.visible_submeshes_applied,
  };
}

bool ObjectRenderer::ModelHasAttachment(const game::ObjectGuid guid,
                                        const std::uint32_t attachment_lookup_index) const {
  const auto *const it = FindInstance(guid);
  if (it == nullptr || !PrepareM2InstanceSpatialQuery(*it)) {
    return false;
  }

  const auto query =
      m2_system_.QueryAttachmentInfo(it->m2_instance_id, attachment_lookup_index);
  return query.status == m2::M2ResultStatus::kReady;
}

std::optional<RenderVec3>
ObjectRenderer::QueryModelAttachmentPosition(const game::ObjectGuid guid,
                                             const std::uint32_t attachment_lookup_index,
                                             const std::optional<RenderVec3> &local_offset) const {
  const auto *const it = FindInstance(guid);
  if (it == nullptr || !PrepareM2InstanceSpatialQuery(*it)) {
    return std::nullopt;
  }

  const auto query = m2_system_.QueryAttachmentPosition(it->m2_instance_id,
                                                        attachment_lookup_index, local_offset);
  if (query.status != m2::M2ResultStatus::kReady) {
    return std::nullopt;
  }

  return query.position;
}

std::optional<RenderVec3>
ObjectRenderer::QueryModelAttachmentPosition(const game::ObjectHandle handle,
                                             const std::uint32_t attachment_lookup_index,
                                             const std::optional<RenderVec3> &local_offset) const {
  const auto found = instances_.find(handle);
  if (found == instances_.end() || found->second.handle != handle) {
    return std::nullopt;
  }
  return QueryModelAttachmentPosition(handle.guid, attachment_lookup_index, local_offset);
}

bool ObjectRenderer::QueryModelAttachmentTransform(const game::ObjectGuid guid,
                                                   const std::uint32_t attachment_lookup_index,
                                                   float *const out_matrix) const {
  const auto *const it = FindInstance(guid);
  if (it == nullptr || !PrepareM2InstanceSpatialQuery(*it)) {
    return false;
  }

  const auto query =
      m2_system_.QueryAttachmentTransformMatrix(it->m2_instance_id, attachment_lookup_index);
  if (query.status != m2::M2ResultStatus::kReady || out_matrix == nullptr) {
    return false;
  }

  std::copy(query.matrix.begin(), query.matrix.end(), out_matrix);
  return true;
}

bool ObjectRenderer::QueryModelWorldPoint(const game::ObjectGuid guid,
                                          float *const out_position) const {
  const auto *const it = FindInstance(guid);
  if (it == nullptr || !PrepareM2InstanceSpatialQuery(*it)) {
    return false;
  }

  const auto query = m2_system_.QueryModelWorldPoint(it->m2_instance_id);
  if (query.status != m2::M2ResultStatus::kReady || out_position == nullptr) {
    return false;
  }

  out_position[0] = query.position[0];
  out_position[1] = query.position[1];
  out_position[2] = query.position[2];
  return true;
}

bool ObjectRenderer::QueryModelRootBoneWorldMatrix(const game::ObjectGuid guid,
                                                   float *const out_matrix) const {
  const auto *const it = FindInstance(guid);
  if (it == nullptr || out_matrix == nullptr || !PrepareM2InstanceSpatialQuery(*it)) {
    return false;
  }

  const auto query = m2_system_.QueryRootBoneWorldMatrix(it->m2_instance_id);
  if (query.status != m2::M2ResultStatus::kReady) {
    return false;
  }

  std::copy(query.matrix.begin(), query.matrix.end(), out_matrix);
  return true;
}

std::optional<ObjectRayIntersection>
ObjectRenderer::FindClosestSegmentIntersectionForGuid(const game::ObjectGuid guid,
                                                      const RenderVec3 &segment_start,
                                                      const RenderVec3 &segment_end) const {
  const auto *const it = FindInstance(guid);
  if (it == nullptr) {
    return std::nullopt;
  }

  if (!PrepareM2InstanceSpatialQuery(*it)) {
    return std::nullopt;
  }

  const auto hit = m2_system_.QueryClosestSegmentIntersection(it->m2_instance_id,
                                                              segment_start, segment_end);
  if (hit.status != m2::M2ResultStatus::kReady || !hit.has_intersection) {
    return std::nullopt;
  }

  return ObjectRayIntersection{
      .guid = it->guid,
      .point = hit.intersection.point,
      .distance = hit.intersection.distance,
  };
}

bool ObjectRenderer::HasAnySegmentIntersection(const RenderVec3 &segment_start,
                                               const RenderVec3 &segment_end) const {
  for (const auto &[guid, instance] : instances_) {
    (void)guid;
    if (!PrepareM2InstanceSpatialQuery(instance)) {
      continue;
    }

    const auto hit = m2_system_.QueryClosestSegmentIntersection(instance.m2_instance_id,
                                                                segment_start, segment_end);
    if (hit.status == m2::M2ResultStatus::kReady && hit.has_intersection) {
      return true;
    }
  }

  return false;
}

void ObjectRenderer::VisitGameObjectCollisionTriangles(
    const std::array<float, 6> &world_bounds,
    const GameObjectCollisionTriangleVisitor &visitor) const {
  if (!visitor) {
    return;
  }

  for (const auto &[handle, instance] : instances_) {
    (void)handle;

    if (instance.type_id != game::TypeID::kGameObject) {
      continue;
    }

    RenderMatrix4x4 world{};
    if (!PrepareM2InstanceSpatialQuery(instance, &world)) {
      continue;
    }

    const auto collision = m2_system_.QueryModelCollisionGeometry(instance.m2_model_id);
    if (collision.status != m2::M2ResultStatus::kReady || !collision.geometry) {
      continue;
    }
    const auto &geometry = *collision.geometry;
    if (geometry.vertices.empty() || geometry.triangles.size() < 3u) {
      continue;
    }

    const RenderMatrix4x4View transform{world};

    for (std::size_t index = 0u; index + 2u < geometry.triangles.size(); index += 3u) {
      const std::uint16_t i0 = geometry.triangles[index];
      const std::uint16_t i1 = geometry.triangles[index + 1u];
      const std::uint16_t i2 = geometry.triangles[index + 2u];
      if (i0 >= geometry.vertices.size() || i1 >= geometry.vertices.size() ||
          i2 >= geometry.vertices.size()) {
        continue;
      }

      GameObjectCollisionTriangle triangle;
      triangle.vertices[0] =
          TransformAffinePoint4x4(RenderVec3View{geometry.vertices[i0]}, transform);
      triangle.vertices[1] =
          TransformAffinePoint4x4(RenderVec3View{geometry.vertices[i1]}, transform);
      triangle.vertices[2] =
          TransformAffinePoint4x4(RenderVec3View{geometry.vertices[i2]}, transform);

      const float min_x = std::min({triangle.vertices[0][0], triangle.vertices[1][0],
                                    triangle.vertices[2][0]});
      const float max_x = std::max({triangle.vertices[0][0], triangle.vertices[1][0],
                                    triangle.vertices[2][0]});
      const float min_y = std::min({triangle.vertices[0][1], triangle.vertices[1][1],
                                    triangle.vertices[2][1]});
      const float max_y = std::max({triangle.vertices[0][1], triangle.vertices[1][1],
                                    triangle.vertices[2][1]});
      const float min_z = std::min({triangle.vertices[0][2], triangle.vertices[1][2],
                                    triangle.vertices[2][2]});
      const float max_z = std::max({triangle.vertices[0][2], triangle.vertices[1][2],
                                    triangle.vertices[2][2]});
      if (min_x > world_bounds[3] || max_x < world_bounds[0] || min_y > world_bounds[4] ||
          max_y < world_bounds[1] || min_z > world_bounds[5] || max_z < world_bounds[2]) {
        continue;
      }

      triangle.guid = instance.guid;
      triangle.owner_id = instance.guid.GetRawValue();
      triangle.facet_id = index / 3u;
      visitor(triangle);
    }
  }
}

std::optional<std::uint32_t>
ObjectRenderer::QueryGameObjectAnimationDurationMs(const game::ObjectGuid guid) const {
  const auto *const it = FindInstance(guid);
  if (it == nullptr || it->type_id != game::TypeID::kGameObject ||
      it->m2_instance_id == 0u) {
    return std::nullopt;
  }
  const auto &state = it->game_object_m2_animation;
  if (!state.active || state.applied_sync_serial != state.sync_serial) {

    return std::nullopt;
  }
  if (state.suppress_completion_schedule) {

    return std::nullopt;
  }

  if (!(state.resolved_playback_speed > 0.0f)) {
    return 0u;
  }
  const auto info = m2_system_.QueryInstanceAnimationInfo(it->m2_instance_id);
  if (info.status != m2::M2ResultStatus::kReady) {
    return std::nullopt;
  }

  return static_cast<std::uint32_t>(
      static_cast<float>(info.info.duration_ms) / state.resolved_playback_speed);
}

void ObjectRenderer::InitializeInstance(RenderInstance &inst,
                                        ObjectProjection &&projection) {

  inst.handle = projection.handle;
  inst.guid = projection.handle.guid;
  inst.type_id = projection.type_id;
  std::copy(std::begin(projection.position), std::end(projection.position),
            std::begin(inst.position));
  inst.orientation = projection.orientation;
  inst.scale = projection.scale;
  inst.world_transform = projection.world_transform;
  inst.has_explicit_world_transform = projection.has_explicit_world_transform;

  inst.display_id = projection.display_id;
  inst.visible = projection.visible;
  inst.needs_model_load = projection.needs_model_load;
  inst.needs_display_resolve = projection.needs_display_resolve;
  inst.alpha = projection.alpha;
  inst.tint_color = projection.tint_color;

  inst.is_mounted = projection.is_mounted;
  inst.movement_flags = projection.movement_flags;
  inst.locomotion_speed = projection.locomotion_speed;
  inst.unit_animation = projection.unit_animation;

  inst.game_object_m2_animation = projection.game_object_m2_animation;
  inst.game_object_collision_state = projection.game_object_collision_state;
  inst.art_kit = projection.art_kit;
  inst.art_kit_sync_serial = projection.art_kit_sync_serial;
  inst.art_kit_visuals_initialized = projection.art_kit_visuals_initialized;
  inst.art_kit_texture_paths = std::move(projection.art_kit_texture_paths);

  inst.destructible_area_scene_states = projection.destructible_area_scene_states;
  inst.area_scene_additional_doodad_sets = projection.area_scene_additional_doodad_sets;
  inst.destructible_area_scene_active_state = projection.destructible_area_scene_active_state;
  inst.destructible_area_scene_previous_state = projection.destructible_area_scene_previous_state;
  inst.destructible_rebuild_effect_display_id = projection.destructible_rebuild_effect_display_id;
  inst.destructible_rebuild_transition_mode = projection.destructible_rebuild_transition_mode;
  inst.destructible_rebuild_transition_speed = projection.destructible_rebuild_transition_speed;
  inst.destructible_visual_sync_serial = projection.destructible_visual_sync_serial;
  inst.has_destructible_area_scene_states = projection.has_destructible_area_scene_states;

  inst.corpse_visual_sync_serial = projection.corpse_visual_sync_serial;
  inst.corpse_render_flags = projection.corpse_render_flags;
  inst.corpse_model_path = std::move(projection.corpse_model_path);
  inst.corpse_creature_texture_replacement = projection.corpse_creature_texture_replacement;

  inst.dynamic_object_visual = std::move(projection.dynamic_object_visual);
  inst.dynamic_object_type = projection.dynamic_object_type;
  inst.dynamic_object_radius = projection.dynamic_object_radius;
  inst.dynamic_object_static_model = projection.dynamic_object_static_model;

  inst.character_appearance_sources = std::move(projection.character_appearance_sources);
  inst.character_appearance_key = std::move(projection.character_appearance_key);
  inst.character_appearance_geosets = projection.character_appearance_geosets;
  inst.character_prebaked_body_texture = std::move(projection.character_prebaked_body_texture);
  inst.character_appearance_declared = projection.character_appearance_declared;
  inst.character_appearance_selection_initialized =
      projection.character_appearance_selection_initialized;

  inst.equipment_sync_serial = projection.equipment_sync_serial;
  inst.model_attachments.reserve(projection.model_attachments.size());
  for (auto &spec : projection.model_attachments) {
    inst.model_attachments.push_back({.desired = std::move(spec)});
  }

  if (inst.type_id == game::TypeID::kUnit || inst.type_id == game::TypeID::kPlayer) {
    inst.animation.SetAnimation(inst.unit_animation.resolved_animation_id,
                                inst.unit_animation.looping);
  } else if (inst.type_id == game::TypeID::kCorpse) {
    inst.animation.SetAnimation(projection.corpse_death_animation_id, false);
  }
}

void ObjectRenderer::ApplyProjection(RenderInstance &inst, ObjectProjection &&projection) {

  if (inst.type_id == game::TypeID::kGameObject ||
      projection.type_id == game::TypeID::kGameObject) {
    if (inst.type_id != projection.type_id || inst.display_id != projection.display_id ||
        inst.game_object_collision_state != projection.game_object_collision_state ||
        inst.position[0] != projection.position[0] ||
        inst.position[1] != projection.position[1] ||
        inst.position[2] != projection.position[2] ||
        inst.orientation != projection.orientation || inst.scale != projection.scale ||
        inst.has_explicit_world_transform != projection.has_explicit_world_transform ||
        inst.world_transform != projection.world_transform) {
      ++game_object_collision_revision_;
    }
  }

  const bool visual_identity_changed =
      inst.display_id != projection.display_id ||
      inst.corpse_model_path != projection.corpse_model_path ||
      inst.dynamic_object_visual.model_path != projection.dynamic_object_visual.model_path;
  const bool destructible_state_changed =
      inst.has_destructible_area_scene_states && projection.has_destructible_area_scene_states &&
      inst.destructible_area_scene_active_state != projection.destructible_area_scene_active_state;
  const bool retain_primary_destructible_m2 =
      inst.type_id == game::TypeID::kGameObject && projection.type_id == game::TypeID::kGameObject &&
      inst.has_destructible_area_scene_states && projection.has_destructible_area_scene_states &&
      (visual_identity_changed || destructible_state_changed);
  const bool reset_destructible_state_runtime =
      inst.has_destructible_area_scene_states != projection.has_destructible_area_scene_states ||
      visual_identity_changed || destructible_state_changed;

  const bool dynamic_state_changed =
      (inst.type_id == game::TypeID::kDynamicObject ||
       projection.type_id == game::TypeID::kDynamicObject) &&
      (inst.dynamic_object_visual != projection.dynamic_object_visual ||
       inst.dynamic_object_type != projection.dynamic_object_type ||
       inst.dynamic_object_radius != projection.dynamic_object_radius ||
       inst.dynamic_object_static_model != projection.dynamic_object_static_model);

  const bool destructible_block_relevant =
      inst.has_destructible_area_scene_states ||
      projection.has_destructible_area_scene_states;
  const bool game_object_block_relevant =
      inst.type_id == game::TypeID::kGameObject ||
      projection.type_id == game::TypeID::kGameObject;

  const auto carries_character_block = [](const game::TypeID type_id) {
    return type_id == game::TypeID::kUnit || type_id == game::TypeID::kPlayer ||
           type_id == game::TypeID::kCorpse;
  };
  const bool character_block_relevant = carries_character_block(inst.type_id) ||
                                        carries_character_block(projection.type_id);

  if (visual_identity_changed || destructible_state_changed) {
    if (retain_primary_destructible_m2) {
      StashPrimaryDestructibleM2StateBinding(inst);
    }
    ClearM2Binding(inst);
    inst.model_path.clear();
    inst.render_asset_kind = RenderAssetKind::kUnknown;
    inst.needs_display_resolve = projection.display_id != 0u ||
                                 projection.type_id == game::TypeID::kCorpse ||
                                 projection.type_id == game::TypeID::kDynamicObject;
    inst.needs_model_load = inst.needs_display_resolve;
  }
  if (inst.has_destructible_area_scene_states && !projection.has_destructible_area_scene_states) {
    ReleaseDestructibleM2StateBindings(inst);
  }

  if (inst.unit_animation.serial != projection.unit_animation.serial ||
      inst.unit_animation.resolved_animation_id !=
          projection.unit_animation.resolved_animation_id) {

    const std::uint32_t locomotion_phase_ms = ResolveLocomotionPhaseStartMs(
        m2_system_, inst, projection.unit_animation.resolved_base_animation_id,
        projection.movement_flags);
    inst.animation.SetAnimationAtPhase(
        projection.unit_animation.resolved_base_animation_id,
        projection.unit_animation.base_looping, locomotion_phase_ms);

    if (projection.unit_animation.upper_body_only) {
      inst.upper_animation.Restart(
          projection.unit_animation.resolved_animation_id,
          projection.unit_animation.looping);
    }
    inst.unit_animation = projection.unit_animation;
    inst.hand_pose_body_instance_id = 0u;
  }

  inst.handle = projection.handle;
  inst.type_id = projection.type_id;
  std::copy(std::begin(projection.position), std::end(projection.position),
            std::begin(inst.position));
  inst.orientation = projection.orientation;
  inst.scale = projection.scale;
  inst.world_transform = projection.world_transform;
  inst.has_explicit_world_transform = projection.has_explicit_world_transform;
  inst.display_id = projection.display_id;
  if (destructible_block_relevant) {
    inst.area_scene_additional_doodad_sets = projection.area_scene_additional_doodad_sets;
    inst.destructible_area_scene_states = projection.destructible_area_scene_states;
    inst.destructible_area_scene_active_state = projection.destructible_area_scene_active_state;
    inst.destructible_area_scene_previous_state = projection.destructible_area_scene_previous_state;
    inst.destructible_rebuild_effect_display_id = projection.destructible_rebuild_effect_display_id;
    inst.destructible_rebuild_transition_mode = projection.destructible_rebuild_transition_mode;
    inst.destructible_rebuild_transition_speed = projection.destructible_rebuild_transition_speed;
    inst.destructible_visual_sync_serial = projection.destructible_visual_sync_serial;
    inst.has_destructible_area_scene_states = projection.has_destructible_area_scene_states;
  }
  if (reset_destructible_state_runtime) {
    inst.destructible_m2_state_runtime = {};
    if (inst.has_destructible_area_scene_states &&
        inst.destructible_area_scene_active_state <
            inst.destructible_m2_state_runtime.size()) {
      inst.destructible_m2_state_runtime[
          inst.destructible_area_scene_active_state].visible = true;
    }
  }
  if (retain_primary_destructible_m2) {
    RestorePrimaryDestructibleM2StateBinding(inst);
  }
  inst.visible = projection.visible;
  inst.alpha = projection.alpha;
  inst.tint_color = projection.tint_color;
  inst.is_mounted = projection.is_mounted;
  inst.movement_flags = projection.movement_flags;

  inst.locomotion_speed = projection.locomotion_speed;

  if (game_object_block_relevant) {
    const auto applied_game_object_animation_serial =
        inst.game_object_m2_animation.applied_sync_serial;
    const auto applied_game_object_animation_id =
        inst.game_object_m2_animation.resolved_animation_id;
    const auto applied_game_object_animation_speed =
        inst.game_object_m2_animation.resolved_playback_speed;
    const auto applied_game_object_animation_suppression =
        inst.game_object_m2_animation.suppress_completion_schedule;
    const bool same_game_object_animation =
        inst.game_object_m2_animation.sync_serial ==
        projection.game_object_m2_animation.sync_serial;
    inst.game_object_m2_animation = projection.game_object_m2_animation;
    if (same_game_object_animation) {
      inst.game_object_m2_animation.applied_sync_serial = applied_game_object_animation_serial;
      inst.game_object_m2_animation.resolved_animation_id = applied_game_object_animation_id;
      inst.game_object_m2_animation.resolved_playback_speed = applied_game_object_animation_speed;
      inst.game_object_m2_animation.suppress_completion_schedule =
          applied_game_object_animation_suppression;
    }

    if (inst.art_kit_sync_serial != projection.art_kit_sync_serial) {
      inst.art_kit = projection.art_kit;
      inst.art_kit_sync_serial = projection.art_kit_sync_serial;
      inst.art_kit_texture_paths = std::move(projection.art_kit_texture_paths);
      inst.art_kit_visuals_initialized = false;
    }
  }

  const bool corpse_changed =
      inst.corpse_visual_sync_serial != projection.corpse_visual_sync_serial;
  if (corpse_changed) {
    inst.corpse_visual_sync_serial = projection.corpse_visual_sync_serial;
    inst.corpse_render_flags = projection.corpse_render_flags;
    inst.corpse_model_path = std::move(projection.corpse_model_path);
    inst.corpse_creature_texture_replacement = projection.corpse_creature_texture_replacement;
  }

  if (dynamic_state_changed) {
    inst.dynamic_object_visual = std::move(projection.dynamic_object_visual);
    inst.dynamic_object_type = projection.dynamic_object_type;
    inst.dynamic_object_radius = projection.dynamic_object_radius;
    inst.dynamic_object_static_model = projection.dynamic_object_static_model;
  }
  if (visual_identity_changed || dynamic_state_changed) {
    inst.dynamic_object_visual_applied = false;
  }

  const bool appearance_changed =
      (inst.character_appearance_key != projection.character_appearance_key &&
       inst.CharacterAppearanceKey() != projection.CharacterAppearanceKey()) ||
      corpse_changed;

  const bool character_geosets_changed =
      character_block_relevant &&
      (inst.character_appearance_declared != projection.character_appearance_declared ||
       !(inst.character_appearance_geosets == projection.character_appearance_geosets));
  if (character_block_relevant) {
    inst.character_appearance_sources = std::move(projection.character_appearance_sources);
    inst.character_appearance_geosets = projection.character_appearance_geosets;
    inst.character_prebaked_body_texture = std::move(projection.character_prebaked_body_texture);
    inst.character_appearance_key = std::move(projection.character_appearance_key);
    inst.character_appearance_declared = projection.character_appearance_declared;
    inst.character_appearance_selection_initialized =
        projection.character_appearance_selection_initialized;
  }
  if (character_geosets_changed) {
    inst.visible_submeshes_applied = false;
  }
  if (appearance_changed) {
    inst.character_appearance_applied = false;
  }

  const bool equipment_changed =
      inst.equipment_sync_serial != projection.equipment_sync_serial ||
      corpse_changed;
  bool attachments_changed =
      inst.model_attachments.size() != projection.model_attachments.size();
  for (std::size_t index = 0;
       index < inst.model_attachments.size() && !attachments_changed; ++index) {
    attachments_changed = !AttachmentSpecsEqual(
        inst.model_attachments[index].desired, projection.model_attachments[index]);
  }
  inst.equipment_sync_serial = projection.equipment_sync_serial;
  if (equipment_changed || attachments_changed) {
    ReconcileModelAttachments(inst, std::move(projection.model_attachments),
                              equipment_changed);
    inst.applied_hand_pose_mask = 0u;
  }
}

void ObjectRenderer::RemoveInstance(const game::ObjectHandle handle) {
  if (auto it = instances_.find(handle); it != instances_.end()) {
    if (it->second.type_id == game::TypeID::kGameObject) {
      ++game_object_collision_revision_;
    }
    if (const auto indexed = instances_by_guid_.find(it->second.guid);
        indexed != instances_by_guid_.end() && indexed->second == &it->second) {
      instances_by_guid_.erase(indexed);
    }
    ReleaseDestructibleM2StateBindings(it->second);
    ClearM2Binding(it->second);
    ReleaseModelAttachments(it->second);
  }
  instances_.erase(handle);
}

void ObjectRenderer::ResolveDisplayId(RenderInstance &inst) {
  if (inst.display_id == 0 && inst.type_id != game::TypeID::kCorpse &&
      inst.type_id != game::TypeID::kDynamicObject) {
    inst.model_path.clear();
    ClearM2Binding(inst);
    inst.render_asset_kind = RenderAssetKind::kUnknown;
    inst.needs_display_resolve = false;
    inst.needs_model_load = false;
    return;
  }

  std::string path;

  switch (inst.type_id) {
  case game::TypeID::kUnit:
  case game::TypeID::kPlayer:
    inst.creature_render_state_key = {};
    path = display_info_.ResolveCreatureModel(inst.display_id);
    break;
  case game::TypeID::kGameObject:
    path = display_info_.ResolveGameObjectModel(inst.display_id);
    break;
  case game::TypeID::kCorpse:
    path = openwow::data::m2::NormalizeModelPath(inst.corpse_model_path);
    break;
  case game::TypeID::kDynamicObject:
    path = openwow::data::m2::NormalizeModelPath(inst.dynamic_object_visual.model_path);
    break;
  default:
    break;
  }

  const RenderAssetKind resolved_kind = ClassifyRenderAssetPath(path);

  if (path != inst.model_path) {
    inst.model_path = std::move(path);
    inst.render_asset_kind = resolved_kind;
    inst.needs_model_load = !inst.model_path.empty();
    ClearM2Binding(inst);
  } else {
    inst.render_asset_kind = resolved_kind;
  }

  inst.needs_display_resolve = false;
}

ObjectRenderer::GameObjectM2AnimationSelection
ObjectRenderer::ResolveGameObjectM2AnimationSubstitution(
    const std::uint32_t model_id, const std::uint32_t requested_animation_id,
    const float requested_speed) const {

  constexpr std::uint32_t kAnimStand = 0u;
  constexpr std::uint32_t kAnimClose = 146u;
  constexpr std::uint32_t kAnimClosed = 147u;
  constexpr std::uint32_t kAnimOpen = 148u;
  constexpr std::uint32_t kAnimOpened = 149u;
  constexpr std::uint32_t kAnimDestroyed = 151u;
  constexpr std::uint32_t kAnimRebuild = 152u;

  const auto has = [this, model_id](const std::uint32_t id) {

    return m2_system_.ModelContainsAnimation(model_id, id);
  };

  GameObjectM2AnimationSelection selection{.animation_id = requested_animation_id,
                                           .speed = requested_speed,
                                           .substituted = false};

  const auto readiness = m2_system_.QueryModelReadiness(model_id);
  if (!readiness.loaded || has(requested_animation_id)) {
    return selection;
  }

  selection.substituted = true;
  switch (requested_animation_id) {
  case kAnimClosed:
    if (has(kAnimClose)) {
      break;
    }
    if (has(kAnimOpen)) {
      selection.animation_id = kAnimOpen;
      selection.speed = 0.0f;
      break;
    }
    selection.animation_id = kAnimStand;
    break;
  case kAnimClose:
    if (has(kAnimOpen)) {
      break;
    }
    selection.animation_id = kAnimClosed;
    break;
  case kAnimOpen:
    if (has(kAnimClose)) {
      break;
    }
    selection.animation_id = has(kAnimDestroyed) ? kAnimDestroyed : kAnimOpened;
    break;
  case kAnimOpened:
    if (has(kAnimOpen)) {
      break;
    }
    if (has(kAnimClose)) {
      selection.animation_id = kAnimClose;
      selection.speed = 0.0f;
      break;
    }
    selection.animation_id = kAnimRebuild;
    break;
  default:

    break;
  }
  return selection;
}

void ObjectRenderer::ApplyGameObjectM2AnimationRequest(RenderInstance &inst) {
  auto &state = inst.game_object_m2_animation;
  if (inst.m2_instance_id == 0u) {
    return;
  }

  ApplyGameObjectM2AnimationRequestCallback(inst);
  if (inst.m2_instance_id == 0u || !state.active ||
      state.applied_sync_serial == state.sync_serial) {
    return;
  }

  const GameObjectM2AnimationSelection selection =
      state.uses_direct_animation_id
          ? GameObjectM2AnimationSelection{.animation_id = state.direct_animation_id,
                                           .speed = state.playback_speed,
                                           .substituted = false}
          : ResolveGameObjectM2AnimationSubstitution(
                inst.m2_model_id, static_cast<std::uint32_t>(state.animation_id),
                state.playback_speed);

  if (selection.substituted) {

    const auto info = m2_system_.QueryInstanceAnimationInfo(inst.m2_instance_id);
    if (info.status == m2::M2ResultStatus::kReady &&
        info.info.resolved_animation_id == selection.animation_id) {
      state.applied_sync_serial = state.sync_serial;
      state.resolved_animation_id = selection.animation_id;
      state.resolved_playback_speed = selection.speed;
      state.suppress_completion_schedule = true;
      return;
    }
  }

  const auto status = m2_system_.SetAnimationRequest(
      inst.m2_instance_id,
      {
          .animation_lookup_id = -1,
          .animation_id = selection.animation_id,
          .sub_animation_index = -1,
          .loop_count = state.use_sequence_repeat_count ? kGameObjectM2SequenceRepeatCount
                                                        : kGameObjectM2DefaultRepeatCount,
          .speed = selection.speed,
      });
  if (status != m2::M2ResultStatus::kReady) {
    return;
  }
  state.applied_sync_serial = state.sync_serial;
  state.resolved_animation_id = selection.animation_id;
  state.resolved_playback_speed = selection.speed;
  state.suppress_completion_schedule = false;
}

void ObjectRenderer::ApplyGameObjectM2AnimationRequestCallback(
    RenderInstance &inst) {
  if (inst.type_id != game::TypeID::kGameObject || inst.m2_instance_id == 0u ||
      inst.game_object_m2_animation_callback_installed) {
    return;
  }

  const auto readiness = m2_system_.QueryInstanceReadiness(inst.m2_instance_id);
  if (readiness.status != m2::M2ResultStatus::kReady) {
    return;
  }

  const auto status = m2_system_.SetAnimationRequestCallback(
      inst.m2_instance_id,
      [this, instance_id = inst.m2_instance_id](
          const m2::M2AnimationRequestEvent &event) {

        if (event.resolved_sub_animation_index != 0) {
          return;
        }
        const auto mapped = ResolveGameObjectAnimationRequestTransition(
            event.requested_animation_id);
        if (!mapped.has_value()) {
          return;
        }
        static_cast<void>(m2_system_.SetAnimation(instance_id, *mapped, 1.0f));
      });
  if (status == m2::M2ResultStatus::kReady) {
    inst.game_object_m2_animation_callback_installed = true;
  } else if (m2::IsTerminalM2ResultStatus(status)) {
    ClearM2Binding(inst);
  }
}

void ObjectRenderer::ApplyGameObjectM2EventCallback(RenderInstance &inst) {
  if (inst.type_id != game::TypeID::kGameObject || inst.m2_instance_id == 0u ||
      game_object_m2_event_sink_ == nullptr) {
    return;
  }

  const auto status = m2_system_.SetTriggeredEventCallback(
      inst.m2_instance_id,
      [this, owner = inst.handle](const m2::M2TriggeredEvent &event) {
        if (game_object_m2_event_sink_ != nullptr) {
          game_object_m2_event_sink_({.owner = owner, .event = event});
        }
      });
  if (m2::IsTerminalM2ResultStatus(status)) {
    ClearM2Binding(inst);
  }
}

void ObjectRenderer::ApplyCreatureDisplayOverrides(RenderInstance &inst) {
  const bool uses_creature_display =
      inst.type_id == game::TypeID::kUnit || inst.type_id == game::TypeID::kPlayer ||
      (inst.type_id == game::TypeID::kCorpse && inst.corpse_creature_texture_replacement);
  if (inst.creature_display_overrides_applied || inst.m2_instance_id == 0u ||
      !uses_creature_display) {
    return;
  }

  std::array<std::string, 3> texture_paths{};
  std::optional<m2::M2ParticleColorRecord> particle_colors;
  if (dbc_ != nullptr) {
    if (const auto *display = dbc_->creature_display_info().LookupEntry(inst.display_id);
        display != nullptr) {
      for (std::size_t index = 0u; index < texture_paths.size(); ++index) {
        texture_paths[index] =
            BuildCreatureDisplayTexturePath(inst.model_path, display->texture_variation[index]);
      }
      if (display->particle_color_id != 0u) {
        if (const auto *record = dbc_->particle_color().LookupEntry(display->particle_color_id);
            record != nullptr) {
          particle_colors = m2::M2ParticleColorRecord{
              .start = record->start,
              .mid = record->mid,
              .end = record->end,
          };
        }
      }
    }
  }
  const auto status = m2_system_.ApplyCreatureDisplayRecordOverrides(
      inst.m2_instance_id, texture_paths, std::move(particle_colors));
  if (status == m2::M2ResultStatus::kReady) {
    inst.creature_display_overrides_applied = true;
  } else if (m2::IsTerminalM2ResultStatus(status)) {
    ClearM2Binding(inst);
  }
}

void ObjectRenderer::ApplyVisibleSubmeshes(RenderInstance &inst) {
  if (inst.visible_submeshes_applied || inst.m2_instance_id == 0u || inst.m2_model_id == 0u) {
    return;
  }

  auto &system = m2_system_;
  m2::M2ResultStatus status = m2::M2ResultStatus::kReady;
  if (inst.type_id == game::TypeID::kPlayer || inst.type_id == game::TypeID::kUnit ||
      inst.type_id == game::TypeID::kCorpse) {
    std::vector<std::size_t> visible_indices;
    if (inst.character_appearance_declared) {
      const auto sections = system.QueryModelSubmeshSectionIds(inst.m2_model_id);
      if (sections.status != m2::M2ResultStatus::kReady) {
        if (m2::IsTerminalM2ResultStatus(sections.status)) {
          ClearM2Binding(inst);
        }
        return;
      }
      visible_indices.reserve(sections.section_ids.size());
      for (std::size_t index = 0u; index < sections.section_ids.size(); ++index) {
        if (inst.character_appearance_geosets.IsVisible(sections.section_ids[index])) {
          visible_indices.push_back(index);
        }
      }
    } else {

      std::uint32_t creature_geoset_data = 0u;
      if (dbc_ != nullptr) {
        if (const auto *display = dbc_->creature_display_info().LookupEntry(inst.display_id);
            display != nullptr) {
          creature_geoset_data = display->creature_geoset_data;
        }
      }
      const auto sections = system.QueryModelSubmeshSectionIds(inst.m2_model_id);
      if (sections.status != m2::M2ResultStatus::kReady) {
        if (m2::IsTerminalM2ResultStatus(sections.status)) {
          ClearM2Binding(inst);
        }
        return;
      }
      visible_indices.reserve(sections.section_ids.size());
      for (std::size_t index = 0u; index < sections.section_ids.size(); ++index) {
        if (IsCreatureGeosetSectionVisible(sections.section_ids[index],
                                          creature_geoset_data)) {
          visible_indices.push_back(index);
        }
      }
    }
    status = system.SetVisibleSubmeshIndices(inst.m2_instance_id, std::move(visible_indices));
  } else {
    status = system.ClearVisibleSubmeshIndices(inst.m2_instance_id);
  }

  if (status == m2::M2ResultStatus::kReady) {
    inst.visible_submeshes_applied = true;
  } else if (m2::IsTerminalM2ResultStatus(status)) {
    ClearM2Binding(inst);
  }
}

bool ObjectRenderer::HasEvictedCharacterComposite(
    const CharacterAppearanceRecord &record, const std::string &key) const {
  if (record.phase != CharacterAppearancePhase::kReady) {
    return false;
  }

  const auto body = record.replaceable_paths.find(kCharacterBodyReplaceableTextureType);
  if (body == record.replaceable_paths.end() || body->second != key) {
    return false;
  }
  return !texture_manager_.HasResidentTexture(key);
}

void ObjectRenderer::QueueCharacterAppearance(RenderInstance &inst) {
  const std::string &appearance_key = inst.CharacterAppearanceKey();
  if (!inst.character_appearance_declared || appearance_key.empty()) {
    return;
  }

  const std::uint64_t eviction_generation = texture_manager_.EvictionGeneration();
  if (inst.character_appearance_settled_key == inst.character_appearance_key &&
      inst.character_appearance_settled_cache_generation ==
          character_appearance_cache_generation_ &&
      inst.character_appearance_settled_eviction_generation == eviction_generation) {
    return;
  }
  const auto cached = character_appearance_cache_.find(appearance_key);
  if (cached != character_appearance_cache_.end()) {

    if (!HasEvictedCharacterComposite(cached->second, appearance_key)) {
      inst.character_appearance_settled_key = inst.character_appearance_key;
      inst.character_appearance_settled_cache_generation =
          character_appearance_cache_generation_;
      inst.character_appearance_settled_eviction_generation = eviction_generation;
      return;
    }
    character_appearance_cache_.erase(cached);
    ++character_appearance_cache_generation_;
  }

  static const CharacterAppearanceTextureSources kNoAppearanceSources{};
  const CharacterAppearanceTextureSources &sources =
      inst.character_appearance_sources != nullptr ? *inst.character_appearance_sources
                                                   : kNoAppearanceSources;

  CharacterAppearanceRecord record;
  if (!inst.character_prebaked_body_texture.empty()) {
    record.phase = CharacterAppearancePhase::kReady;
    record.replaceable_paths.emplace(kCharacterBodyReplaceableTextureType,
                                     inst.character_prebaked_body_texture);
    if (!sources.cape.empty()) {
      record.replaceable_paths.emplace(kCharacterCapeReplaceableTextureType,
                                       sources.cape);
    }
    if (!sources.hair.empty()) {
      record.replaceable_paths.emplace(kCharacterHairReplaceableTextureType,
                                       sources.hair);
    }
    if (!sources.extra_skin.empty()) {
      record.replaceable_paths.emplace(kCharacterExtraSkinReplaceableTextureType,
                                       sources.extra_skin);
    }
    character_appearance_cache_.emplace(appearance_key, std::move(record));
    return;
  }
  if (dbc_ == nullptr || !file_loader_) {
    record.phase = CharacterAppearancePhase::kFailed;
    character_appearance_cache_.emplace(appearance_key, std::move(record));
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "ObjectRenderer: cannot prepare character appearance without DBC and VFS loader: " +
            appearance_key);
    return;
  }

  if (!character_appearance_workers_.IsInitialized()) {
    if (character_appearance_mailbox_ == nullptr) {
      character_appearance_mailbox_ = std::make_shared<CharacterAppearanceMailbox>();
    }
    character_appearance_workers_.Initialize(kCharacterAppearanceWorkerCount);
  }

  const std::string key = appearance_key;

  const auto worker_sources =
      inst.character_appearance_sources != nullptr
          ? inst.character_appearance_sources
          : std::make_shared<const CharacterAppearanceTextureSources>();
  const auto load_file = file_loader_;
  const auto *item_display_info = &dbc_->item_display_info();
  const auto mailbox = character_appearance_mailbox_;
  auto [record_it, inserted] = character_appearance_cache_.emplace(key, std::move(record));
  if (!inserted || mailbox == nullptr) {
    return;
  }

  try {
    (void)character_appearance_workers_.Submit(
        "world-character-appearance:" + key,
        inst.handle == priority_instance_ ? openwow::core::TaskPriority::Critical
                                          : openwow::core::TaskPriority::High,
        [key, worker_sources, item_display_info, load_file, mailbox]() mutable {
          PreparedCharacterAppearanceTextures prepared;
          try {
            prepared = PrepareCharacterAppearanceTextures(*worker_sources,
                                                          item_display_info, load_file);
          } catch (const std::exception &error) {
            prepared.error = error.what();
          } catch (...) {
            prepared.error = "unknown character appearance preparation exception";
          }

          std::lock_guard lock(mailbox->mutex);
          mailbox->completions.push_back({
              .key = key,
              .prepared = std::move(prepared),
          });
        });
  } catch (const std::exception &error) {
    record_it->second.phase = CharacterAppearancePhase::kFailed;
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                              "ObjectRenderer: failed to schedule character appearance " + key +
                                  ": " + error.what());
  }
}

void ObjectRenderer::PumpCharacterAppearanceCompletions() {
  if (character_appearance_mailbox_ == nullptr) {
    return;
  }

  std::deque<CharacterAppearanceCompletion> completions;
  {
    std::lock_guard lock(character_appearance_mailbox_->mutex);
    completions.swap(character_appearance_mailbox_->completions);
  }

  for (auto &completion : completions) {
    const auto record_it = character_appearance_cache_.find(completion.key);
    if (record_it == character_appearance_cache_.end() ||
        record_it->second.phase != CharacterAppearancePhase::kPreparing) {
      continue;
    }

    auto &record = record_it->second;
    if (!completion.prepared.valid || !completion.prepared.body.valid) {
      record.phase = CharacterAppearancePhase::kFailed;
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kWarn,
          "ObjectRenderer: async character appearance preparation failed: " + completion.key +
              (completion.prepared.error.empty() ? std::string{}
                                                 : ": " + completion.prepared.error));
      continue;
    }

    record.pending_uploads.clear();
    record.pending_uploads.reserve(1u + completion.prepared.direct_textures.size());
    record.pending_uploads.push_back(std::move(completion.prepared.body));
    for (auto &texture : completion.prepared.direct_textures) {
      record.pending_uploads.push_back(std::move(texture));
    }
    record.next_upload = 0u;
    record.replaceable_paths = std::move(completion.prepared.replaceable_paths);
    record.phase = record.pending_uploads.empty() ? CharacterAppearancePhase::kReady
                                                  : CharacterAppearancePhase::kCommit;
  }
}

void ObjectRenderer::CommitCharacterAppearanceUploads() {
  std::uint32_t committed = 0u;
  const auto commit_record = [this, &committed](const std::string &key,
                                                CharacterAppearanceRecord &record) {
    while (record.phase == CharacterAppearancePhase::kCommit &&
           committed < kMaxCharacterAppearanceCommitsPerFrame) {
      if (record.next_upload >= record.pending_uploads.size()) {
        record.pending_uploads.clear();
        record.phase = CharacterAppearancePhase::kReady;
        break;
      }

      const std::string path = record.pending_uploads[record.next_upload].path;
      auto upload = std::move(record.pending_uploads[record.next_upload]);
      ++committed;
      if (!bgfx::isValid(texture_manager_.CommitPreparedTexture(std::move(upload)))) {
        record.pending_uploads.clear();
        record.replaceable_paths.clear();
        record.phase = CharacterAppearancePhase::kFailed;
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                                  "ObjectRenderer: prepared character texture commit failed: " +
                                      path + " appearance=" + key);
        break;
      }

      ++record.next_upload;
      if (record.next_upload == record.pending_uploads.size()) {
        record.pending_uploads.clear();
        record.phase = CharacterAppearancePhase::kReady;
      }
    }
  };

  std::string priority_key;
  if (!priority_instance_.guid.IsEmpty()) {
    if (const auto priority = instances_.find(priority_instance_); priority != instances_.end()) {
      priority_key = priority->second.CharacterAppearanceKey();
    }
  }

  if (!priority_key.empty()) {
    if (auto priority = character_appearance_cache_.find(priority_key);
        priority != character_appearance_cache_.end()) {
      commit_record(priority->first, priority->second);
    }
  }

  for (auto &[key, record] : character_appearance_cache_) {
    if (key == priority_key) {
      continue;
    }
    commit_record(key, record);

    if (committed >= kMaxCharacterAppearanceCommitsPerFrame) {
      break;
    }
  }
}

void ObjectRenderer::ApplyCharacterAppearance(RenderInstance &inst) {
  if (!inst.character_appearance_declared || inst.character_appearance_applied ||
      inst.m2_instance_id == 0u || inst.CharacterAppearanceKey().empty()) {
    return;
  }

  const auto record_it =
      character_appearance_cache_.find(inst.CharacterAppearanceKey());
  if (record_it == character_appearance_cache_.end() ||
      (record_it->second.phase != CharacterAppearancePhase::kReady &&
       record_it->second.phase != CharacterAppearancePhase::kFailed)) {
    return;
  }

  auto &system = m2_system_;
  m2::M2ResultStatus status = m2::M2ResultStatus::kReady;
  const auto merge = [&status](const m2::M2ResultStatus next) {
    status = m2::MergeM2ResultStatus(status, next);
  };
  for (const std::uint32_t type : kCharacterReplaceableTextureTypes) {
    merge(system.ClearReplaceableTexturePath(inst.m2_instance_id, type));
  }
  for (const auto &[type, path] : record_it->second.replaceable_paths) {
    if (!path.empty()) {
      merge(system.SetReplaceableTexturePath(inst.m2_instance_id, type, path));
    }
  }

  if (status == m2::M2ResultStatus::kReady) {
    inst.character_appearance_applied = true;
  } else if (m2::IsTerminalM2ResultStatus(status)) {
    ClearM2Binding(inst);
  }
}

void ObjectRenderer::QueueEquipmentTexture(const std::string &path) {
  if (texture_manager_.AcquireCachedTexture(path)) {
    return;
  }
  if (path.empty() || equipment_texture_cache_.find(path) != equipment_texture_cache_.end()) {
    return;
  }

  constexpr std::size_t kMaximumPendingOrNegativeEquipmentTextures = 4096u;
  if (equipment_texture_cache_.size() >= kMaximumPendingOrNegativeEquipmentTextures) {
    const auto failed = std::find_if(
        equipment_texture_cache_.begin(), equipment_texture_cache_.end(),
        [](const auto &entry) { return entry.second.phase == CharacterAppearancePhase::kFailed; });
    if (failed != equipment_texture_cache_.end()) {
      equipment_texture_cache_.erase(failed);
    }
  }
  if (equipment_texture_cache_.size() >= kMaximumPendingOrNegativeEquipmentTextures) {
    return;
  }

  EquipmentTextureRecord record;
  if (!file_loader_) {
    record.phase = CharacterAppearancePhase::kFailed;
    equipment_texture_cache_.emplace(path, std::move(record));
    return;
  }
  equipment_texture_cache_.emplace(path, std::move(record));

  if (equipment_texture_mailbox_ == nullptr) {
    equipment_texture_mailbox_ = std::make_shared<EquipmentTextureMailbox>();
  }
  if (!character_appearance_workers_.IsInitialized()) {
    character_appearance_workers_.Initialize(kCharacterAppearanceWorkerCount);
  }

  const auto loader = file_loader_;
  const auto mailbox = equipment_texture_mailbox_;
  try {
    static_cast<void>(character_appearance_workers_.Submit(
        "equipment-texture:" + path, openwow::core::TaskPriority::High, [loader, mailbox, path] {
          PreparedTextureUpload prepared =
              TextureManager::PrepareTextureUploadFromLoader(path, loader);
          std::lock_guard lock(mailbox->mutex);
          mailbox->completions.push_back({.path = path, .prepared = std::move(prepared)});
        }));
  } catch (...) {
    equipment_texture_cache_[path].phase = CharacterAppearancePhase::kFailed;
  }
}

void ObjectRenderer::PumpEquipmentTextureCompletions() {
  if (equipment_texture_mailbox_ == nullptr) {
    return;
  }
  std::deque<EquipmentTextureCompletion> completed;
  {
    std::lock_guard lock(equipment_texture_mailbox_->mutex);
    completed.swap(equipment_texture_mailbox_->completions);
  }
  for (auto &completion : completed) {
    const auto found = equipment_texture_cache_.find(completion.path);
    if (found == equipment_texture_cache_.end() ||
        found->second.phase != CharacterAppearancePhase::kPreparing) {
      continue;
    }
    found->second.prepared = std::move(completion.prepared);
    found->second.phase = found->second.prepared.valid ? CharacterAppearancePhase::kCommit
                                                       : CharacterAppearancePhase::kFailed;
  }
}

void ObjectRenderer::CommitEquipmentTextures() {
  std::uint32_t committed = 0u;
  for (auto it = equipment_texture_cache_.begin(); it != equipment_texture_cache_.end();) {
    auto &record = it->second;
    if (record.phase != CharacterAppearancePhase::kCommit) {
      ++it;
      continue;
    }
    const auto handle = texture_manager_.CommitPreparedTexture(record.prepared);
    record.prepared = {};
    if (bgfx::isValid(handle)) {
      it = equipment_texture_cache_.erase(it);
    } else {
      record.phase = CharacterAppearancePhase::kFailed;
      ++it;
    }
    if (++committed >= kMaxEquipmentTextureCommitsPerFrame) {
      break;
    }
  }
}

bool ObjectRenderer::IsEquipmentTextureReady(const std::string &path) const {
  if (path.empty()) {
    return true;
  }
  if (texture_manager_.AcquireCachedTexture(path)) {
    return true;
  }
  const auto found = equipment_texture_cache_.find(path);
  return found != equipment_texture_cache_.end() &&
         found->second.phase == CharacterAppearancePhase::kReady;
}

void ObjectRenderer::ApplyDynamicObjectVisualState(RenderInstance &inst) {
  if (inst.type_id != game::TypeID::kDynamicObject || inst.dynamic_object_visual_applied ||
      inst.m2_instance_id == 0u) {
    return;
  }

  const auto triggered_event_status = m2_system_.SetTriggeredEventCallback(
      inst.m2_instance_id, [this](const m2::M2TriggeredEvent &event) {
        if (!dynamic_object_event_sink_ || event.data == 0u) {
          return;
        }
        constexpr std::uint32_t kSoundEvent = 0x24444E53u;
        constexpr std::uint32_t kShakeEvent = 0x4B485324u;
        if (event.identifier != kSoundEvent && event.identifier != kShakeEvent) {
          return;
        }
        dynamic_object_event_sink_(DynamicObjectPresentationEvent{
            .kind = event.identifier == kSoundEvent
                        ? DynamicObjectPresentationEventKind::kSound
                        : DynamicObjectPresentationEventKind::kCameraShake,
            .visual_id = event.data,
            .position = event.world_position,
        });
      });
  if (triggered_event_status != m2::M2ResultStatus::kReady) {
    if (m2::IsTerminalM2ResultStatus(triggered_event_status)) {
      ClearM2Binding(inst);
    }
    return;
  }

  const auto animation_callback_status = m2_system_.SetAnimationCompletionCallback(
      inst.m2_instance_id, [this, instance_id = inst.m2_instance_id](const std::uint32_t) {
        constexpr std::uint32_t kDirectedCastAnimation = 0x9Eu;
        const std::uint32_t next_animation =
            m2_system_.InstanceModelHasAnimation(instance_id, kDirectedCastAnimation)
                ? kDirectedCastAnimation
                : 0u;
        if (m2_system_.InstanceModelHasAnimation(instance_id, next_animation)) {
          (void)m2_system_.SetAnimation(instance_id, next_animation);
        }
      });
  if (animation_callback_status != m2::M2ResultStatus::kReady) {
    if (m2::IsTerminalM2ResultStatus(animation_callback_status)) {
      ClearM2Binding(inst);
    }
    return;
  }

  m2::M2InstanceEffectContext effect_context;
  effect_context.spell_attributes_ex5_bit30 = inst.dynamic_object_visual.spell_attributes_ex5_bit30;
  if (m2::IsTerminalM2ResultStatus(
          m2_system_.SetInstanceEffectContext(inst.m2_instance_id, effect_context))) {
    ClearM2Binding(inst);
    return;
  }

  const auto emitter_status =
      m2_system_.SetEffectEmittersEnabled(inst.m2_instance_id, !inst.dynamic_object_static_model);
  if (emitter_status != m2::M2ResultStatus::kReady) {
    if (m2::IsTerminalM2ResultStatus(emitter_status)) {
      ClearM2Binding(inst);
    }
    return;
  }

  constexpr std::uint32_t kBirthAnimation = 0x7Fu;
  if (m2_system_.InstanceModelHasAnimation(inst.m2_instance_id, kBirthAnimation) &&
      m2::IsTerminalM2ResultStatus(m2_system_.SetAnimation(inst.m2_instance_id, kBirthAnimation))) {
    ClearM2Binding(inst);
    return;
  }
  float visual_scale = 1.0f;
  if (inst.dynamic_object_type != game::DynamicObjectType::AreaSpell &&
      inst.dynamic_object_type != game::DynamicObjectType::FarsightFocus) {
    constexpr float kMinimumBoundingRadius = 0.001f;
    const auto sphere = m2_system_.QueryInstanceModelBoundingSphere(inst.m2_instance_id);
    if (sphere.status == m2::M2ResultStatus::kReady && sphere.sphere[3] > kMinimumBoundingRadius) {
      visual_scale = inst.dynamic_object_radius / sphere.sphere[3];
    } else if (inst.dynamic_object_visual.effect_id != 0u &&
               inst.dynamic_object_visual.area_effect_size > 0.0f) {
      visual_scale = inst.dynamic_object_radius / inst.dynamic_object_visual.area_effect_size;
    }
  }
  if (inst.dynamic_object_visual.effect_id != 0u &&
      inst.dynamic_object_visual.effect_scale > 0.0f) {
    visual_scale *= inst.dynamic_object_visual.effect_scale;
    inst.alpha = 1.0f;
  }
  inst.scale = std::max(visual_scale, 0.0f);
  inst.world_transform =
      ScaleMatrix4x4BasisRows(inst.world_transform, RenderVec3{inst.scale, inst.scale, inst.scale});
  inst.dynamic_object_visual_applied = true;
}

void ObjectRenderer::ClearM2Binding(RenderInstance &inst) {
  if (inst.m2_instance_id != 0u) {
    static_cast<void>(m2_system_.ClearAnimationRequestCallback(inst.m2_instance_id));
    const auto destroy_status = m2_system_.DestroyInstance(inst.m2_instance_id);
    if (m2::IsTerminalM2ResultStatus(destroy_status)) {
      inst.game_object_m2_animation.applied_sync_serial = 0u;
    }
  }

  for (auto &binding : inst.model_attachments) {
    ResetItemVisualChildInstances(binding);
    binding.m2_model_id = 0u;
    binding.m2_instance_id = 0u;
    binding.bound_model_path.clear();
    binding.bound_texture_path.clear();
    binding.bound_attachment_id = 0u;
  }

  if (inst.m2_stream_ticket) {
    m2_system_.ReleaseModelAsync(inst.m2_stream_ticket);
    inst.m2_stream_ticket = {};
  }

  inst.m2_model_id = 0u;
  inst.m2_instance_id = 0u;
  inst.game_object_m2_animation_callback_installed = false;
  inst.dynamic_object_visual_applied = false;
  inst.requested_model_path.clear();
  inst.game_object_m2_animation.applied_sync_serial = 0u;
  inst.game_object_m2_animation.resolved_animation_id = 0u;
  inst.game_object_m2_animation.resolved_playback_speed = 1.0f;
  inst.game_object_m2_animation.suppress_completion_schedule = false;
  inst.creature_display_overrides_applied = false;
  inst.visible_submeshes_applied = false;
  inst.character_appearance_applied = false;
  inst.submitted_draw_count = 0u;
  inst.applied_hand_pose_mask = 0u;
  inst.hand_pose_body_instance_id = 0u;
  inst.needs_model_load =
      !inst.model_path.empty() && inst.render_asset_kind == RenderAssetKind::kM2;
}

void ObjectRenderer::ReleaseDestructibleM2StateBindings(RenderInstance &inst) {
  for (auto &binding : inst.destructible_m2_state_bindings) {
    if (binding.m2_instance_id != 0u) {
      static_cast<void>(m2_system_.DestroyInstance(binding.m2_instance_id));
    }
    if (binding.stream_ticket) {
      m2_system_.ReleaseModelAsync(binding.stream_ticket);
    }
    binding = {};
  }
}

void ObjectRenderer::SynchronizeInactiveDestructibleM2StateBindings(
    RenderInstance &inst, int &loads_this_frame) {
  if (!inst.has_destructible_area_scene_states || !display_info_.IsReady()) {
    return;
  }
  if (inst.destructible_area_scene_active_state >= inst.destructible_area_scene_states.size()) {
    ReleaseDestructibleM2StateBindings(inst);
    return;
  }

  const auto release_binding = [this](RenderInstance::DestructibleM2StateBinding &binding) {
    if (binding.m2_instance_id != 0u) {
      static_cast<void>(m2_system_.DestroyInstance(binding.m2_instance_id));
    }
    if (binding.stream_ticket) {
      m2_system_.ReleaseModelAsync(binding.stream_ticket);
    }
    binding = {};
  };
  const auto synchronize_binding =
      [this, &inst, &loads_this_frame, &release_binding](
          RenderInstance::DestructibleM2StateBinding &binding,
          const std::uint32_t display_id, const std::uint8_t state_index) {
        const auto& runtime = inst.destructible_m2_state_runtime[state_index];
        const RenderMatrix4x4 model_matrix =
            BuildDestructibleM2StateModelMatrix(inst, state_index);
        const std::string model_path = display_info_.ResolveGameObjectModel(display_id);
        if (ClassifyRenderAssetPath(model_path) != RenderAssetKind::kM2) {
          release_binding(binding);
          return;
        }

        if (binding.display_id != display_id || binding.model_path != model_path) {
          release_binding(binding);
          binding.display_id = display_id;
          binding.model_path = model_path;
        }

        if (binding.m2_instance_id != 0u) {
          const auto transform_visibility = m2_system_.SetTransformAndVisibility(
              binding.m2_instance_id, model_matrix, runtime.visible);
          if (m2::IsTerminalM2ResultStatus(transform_visibility.transform_status) ||
              m2::IsTerminalM2ResultStatus(transform_visibility.visibility_status)) {
            release_binding(binding);
          }
          return;
        }

        if (!binding.stream_ticket && !binding.request_failed &&
            loads_this_frame < kMaxLoadsPerFrame) {
          binding.stream_ticket = m2_system_.AcquireModelAsync(
              binding.model_path, m2::M2StreamPriority::kVisibleUnit);
          binding.requested_model_path = binding.model_path;
          binding.request_failed = !binding.stream_ticket;
          if (binding.stream_ticket) {
            ++loads_this_frame;
          }
        }
        if (!binding.stream_ticket || binding.request_failed) {
          return;
        }

        const auto streamed = m2_system_.QueryModelAsync(binding.stream_ticket);
        if (streamed.state == m2::M2StreamState::kFailed) {
          m2_system_.ReleaseModelAsync(binding.stream_ticket);
          binding.stream_ticket = {};
          binding.request_failed = true;
          return;
        }
        if (streamed.state != m2::M2StreamState::kReady || streamed.model_id == 0u) {
          return;
        }

        const auto created = m2_system_.CreateInstanceAsync(binding.stream_ticket);
        if (created.status != m2::M2ResultStatus::kReady || created.instance_id == 0u) {
          binding.request_failed = m2::IsTerminalM2ResultStatus(created.status);
          return;
        }

        const auto transform_visibility = m2_system_.SetTransformAndVisibility(
            created.instance_id, model_matrix, runtime.visible);
        if (transform_visibility.transform_status != m2::M2ResultStatus::kReady ||
            transform_visibility.visibility_status != m2::M2ResultStatus::kReady) {
          static_cast<void>(m2_system_.DestroyInstance(created.instance_id));
          binding.request_failed =
              m2::IsTerminalM2ResultStatus(transform_visibility.transform_status) ||
              m2::IsTerminalM2ResultStatus(transform_visibility.visibility_status);
          return;
        }

        binding.m2_model_id = streamed.model_id;
        binding.m2_instance_id = created.instance_id;
      };

  for (std::size_t index = 0u; index < inst.destructible_area_scene_states.size(); ++index) {
    if (index == inst.destructible_area_scene_active_state) {
      continue;
    }
    synchronize_binding(inst.destructible_m2_state_bindings[index],
                        inst.destructible_area_scene_states[index].display_id,
                        static_cast<std::uint8_t>(index));
  }
  const std::size_t rebuild_effect_index =
      RenderInstance::kDestructibleRebuildEffectBindingIndex;
  if (inst.destructible_rebuild_effect_display_id == 0u) {
    release_binding(inst.destructible_m2_state_bindings[rebuild_effect_index]);
  } else {
    synchronize_binding(inst.destructible_m2_state_bindings[rebuild_effect_index],
                        inst.destructible_rebuild_effect_display_id,
                        static_cast<std::uint8_t>(rebuild_effect_index));
  }
}

RenderMatrix4x4 ObjectRenderer::BuildDestructibleM2StateModelMatrix(
    const RenderInstance& inst, const std::uint8_t state_index) const {
  RenderMatrix4x4 matrix = BuildM2InstanceModelMatrix(inst, mount_renderer_);
  if (state_index < inst.destructible_m2_state_runtime.size()) {
    matrix[14] -= inst.destructible_m2_state_runtime[state_index].vertical_offset_down;
  }
  return matrix;
}

void ObjectRenderer::SubmitDestructibleM2StateBindings(
    RenderInstance& inst, const std::uint8_t view_id, const float* const view_mtx,
    const m2::M2RenderPassScope pass_scope, const PassBatchUniforms& pass_uniforms) {
  if (!inst.has_destructible_area_scene_states) {
    return;
  }
  for (std::size_t index = 0u; index < inst.destructible_m2_state_bindings.size(); ++index) {
    if (index == inst.destructible_area_scene_active_state ||
        !inst.destructible_m2_state_runtime[index].visible) {
      continue;
    }
    auto& binding = inst.destructible_m2_state_bindings[index];
    if (binding.m2_instance_id == 0u) {
      continue;
    }
    const auto readiness = m2_system_.QueryInstanceReadiness(binding.m2_instance_id);
    if (readiness.status != m2::M2ResultStatus::kReady || !readiness.render_ready) {
      continue;
    }
    const auto batch_status =
        m2_system_.SetSharedBatchUniforms(binding.m2_instance_id, pass_uniforms.world);
    if (batch_status != m2::M2ResultStatus::kReady) {
      continue;
    }

    const std::span<const std::uint32_t> draw_ordinal =
        pass_scope == m2::M2RenderPassScope::kTransparentOnly
            ? std::span<const std::uint32_t>{&binding.transparent_draw_ordinal, 1u}
            : std::span<const std::uint32_t>{};
    const m2::M2TransparentDrawOrdinalScope draw_order_scope(draw_ordinal);
    const auto render_result = m2_system_.RenderInstance(
        view_id, binding.m2_instance_id, RenderMatrix4x4View{view_mtx, 16u}, pass_scope);
    inst.submitted_draw_count += render_result.submitted_draw_count;
    if (m2::IsTerminalM2ResultStatus(render_result.status)) {
      static_cast<void>(m2_system_.DestroyInstance(binding.m2_instance_id));
      binding.m2_instance_id = 0u;
      binding.m2_model_id = 0u;
      binding.request_failed = true;
    }
  }
}

void ObjectRenderer::StashPrimaryDestructibleM2StateBinding(RenderInstance &inst) {
  if (inst.destructible_area_scene_active_state >= inst.destructible_m2_state_bindings.size() ||
      (inst.render_asset_kind != RenderAssetKind::kM2 && !inst.m2_stream_ticket &&
       inst.m2_instance_id == 0u)) {
    return;
  }

  auto &binding = inst.destructible_m2_state_bindings[inst.destructible_area_scene_active_state];
  if (binding.m2_instance_id != 0u) {
    static_cast<void>(m2_system_.DestroyInstance(binding.m2_instance_id));
  }
  if (binding.stream_ticket) {
    m2_system_.ReleaseModelAsync(binding.stream_ticket);
  }
  binding = {
      .display_id = inst.display_id,
      .model_path = std::move(inst.model_path),
      .requested_model_path = std::move(inst.requested_model_path),
      .stream_ticket = inst.m2_stream_ticket,
      .m2_model_id = inst.m2_model_id,
      .m2_instance_id = inst.m2_instance_id,
  };
  if (binding.m2_instance_id != 0u) {
    static_cast<void>(m2_system_.ClearTriggeredEventCallback(binding.m2_instance_id));
  }

  inst.m2_stream_ticket = {};
  inst.m2_model_id = 0u;
  inst.m2_instance_id = 0u;
  inst.requested_model_path.clear();
  inst.needs_model_load = false;
  inst.model_retry_seconds = 0.0f;
}

void ObjectRenderer::RestorePrimaryDestructibleM2StateBinding(RenderInstance &inst) {
  if (inst.destructible_area_scene_active_state >= inst.destructible_m2_state_bindings.size()) {
    return;
  }

  auto &binding = inst.destructible_m2_state_bindings[inst.destructible_area_scene_active_state];
  const auto expected_display =
      inst.destructible_area_scene_states[inst.destructible_area_scene_active_state].display_id;
  if (binding.display_id != expected_display || binding.model_path.empty()) {
    if (binding.m2_instance_id != 0u) {
      static_cast<void>(m2_system_.DestroyInstance(binding.m2_instance_id));
    }
    if (binding.stream_ticket) {
      m2_system_.ReleaseModelAsync(binding.stream_ticket);
    }
    binding = {};
    return;
  }

  inst.model_path = std::move(binding.model_path);
  inst.render_asset_kind = RenderAssetKind::kM2;
  inst.requested_model_path = std::move(binding.requested_model_path);
  inst.m2_stream_ticket = binding.stream_ticket;
  inst.m2_model_id = binding.m2_model_id;
  inst.m2_instance_id = binding.m2_instance_id;
  inst.needs_display_resolve = false;
  inst.needs_model_load = inst.m2_instance_id == 0u && !inst.m2_stream_ticket &&
                          !binding.request_failed;
  binding = {};
  ApplyGameObjectM2EventCallback(inst);
  ApplyGameObjectM2AnimationRequestCallback(inst);
}

void ObjectRenderer::ReleaseModelAttachments(RenderInstance &inst) {
  auto &system = m2_system_;
  for (auto &binding : inst.model_attachments) {
    ReleaseItemVisualChildren(m2_system_, binding, true);
    if (binding.m2_instance_id != 0u) {
      static_cast<void>(system.DestroyInstance(binding.m2_instance_id));
    }
    if (binding.stream_ticket) {
      m2_system_.ReleaseModelAsync(binding.stream_ticket);
    }
  }
  inst.model_attachments.clear();
}

void ObjectRenderer::ReconcileModelAttachments(
    RenderInstance &inst, std::vector<ModelAttachmentSpec> projected,
    const bool rebuild_equipment) {
  std::vector<ModelAttachmentBinding> reconciled;
  reconciled.reserve(projected.size());

  for (auto &next : projected) {
    const bool may_preserve =
        !rebuild_equipment || !IsEquipmentAttachmentRole(next.role);
    const auto current = may_preserve
                             ? std::find_if(
                                   inst.model_attachments.begin(),
                                   inst.model_attachments.end(),
                                   [&next](const ModelAttachmentBinding &binding) {
                                     return AttachmentSpecsEqual(binding.desired, next);
                                   })
                             : inst.model_attachments.end();
    if (current != inst.model_attachments.end()) {
      reconciled.push_back(std::move(*current));
      inst.model_attachments.erase(current);
    } else {

      reconciled.push_back({.desired = std::move(next)});
    }
  }

  ReleaseModelAttachments(inst);
  inst.model_attachments = std::move(reconciled);
}

bool ObjectRenderer::RequestModelForInstance(RenderInstance &inst) {
  if (inst.model_path.empty() || inst.render_asset_kind != RenderAssetKind::kM2) {
    inst.needs_model_load = false;
    return false;
  }
  if (inst.m2_stream_ticket || inst.m2_instance_id != 0u) {
    return false;
  }

  const auto priority = inst.handle == priority_instance_ ? m2::M2StreamPriority::kCritical
                                                          : m2::M2StreamPriority::kVisibleUnit;
  inst.m2_stream_ticket = m2_system_.AcquireModelAsync(inst.model_path, priority);
  inst.requested_model_path = inst.model_path;
  inst.needs_model_load = false;
  if (!inst.m2_stream_ticket) {
    inst.model_retry_seconds = kM2NegativeRetrySeconds;
    inst.needs_model_load = true;
    return false;
  }
  return true;
}

void ObjectRenderer::PublishStreamedModelForInstance(RenderInstance &inst) {
  if (!inst.m2_stream_ticket || inst.m2_instance_id != 0u) {
    return;
  }

  const auto streamed = m2_system_.QueryModelAsync(inst.m2_stream_ticket);
  if (streamed.state == m2::M2StreamState::kPreparing ||
      streamed.state == m2::M2StreamState::kCommitPending) {
    return;
  }

  if (streamed.state != m2::M2StreamState::kReady || streamed.model_id == 0u) {
    if (streamed.state == m2::M2StreamState::kFailed) {
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kWarn,
          "ObjectRenderer: asynchronous M2 preparation failed: " + inst.requested_model_path +
              (streamed.detail.empty() ? std::string() : " detail=" + streamed.detail));
      m2_system_.ReleaseModelAsync(inst.m2_stream_ticket);
      inst.m2_stream_ticket = {};
      inst.requested_model_path.clear();
      inst.model_retry_seconds = kM2NegativeRetrySeconds;
      inst.needs_model_load = true;
    }
    return;
  }

  const auto created = m2_system_.CreateInstanceAsync(inst.m2_stream_ticket);
  if (created.status != m2::M2ResultStatus::kReady || created.instance_id == 0u) {
    if (m2::IsTerminalM2ResultStatus(created.status)) {
      m2_system_.ReleaseModelAsync(inst.m2_stream_ticket);
      inst.m2_stream_ticket = {};
      inst.requested_model_path.clear();
      inst.model_retry_seconds = kM2NegativeRetrySeconds;
      inst.needs_model_load = true;
    }
    return;
  }

  inst.m2_model_id = streamed.model_id;
  inst.m2_instance_id = created.instance_id;
  inst.creature_display_overrides_applied = false;
  inst.character_appearance_applied = false;
  inst.submitted_draw_count = 0u;
  inst.needs_model_load = false;
  inst.model_retry_seconds = 0.0f;

  ApplyDynamicObjectVisualState(inst);
  ApplyGameObjectM2AnimationRequestCallback(inst);
  ApplyGameObjectM2EventCallback(inst);
}

std::size_t ObjectRenderer::UpdateModelAttachments(RenderInstance &inst,
                                                   const std::size_t request_budget) {
  auto &system = m2_system_;
  std::size_t requests = 0u;

  for (auto &binding : inst.model_attachments) {
    ReconcileItemVisualChildren(m2_system_, binding);
    static const WeaponAttachmentVisual kAbsentVisual{};
    const auto &desired = binding.desired.visual != nullptr ? *binding.desired.visual
                                                            : kAbsentVisual;
    if (desired.model_path.empty()) {
      continue;
    }

    const bool attachment_texture_changed =
        binding.bound_texture_path != desired.texture_path;
    const bool attachment_fully_bound =
        binding.m2_instance_id != 0u && inst.m2_instance_id != 0u &&
        binding.bound_model_path == desired.model_path &&
        !attachment_texture_changed;
    if (!desired.texture_path.empty() && !attachment_fully_bound) {
      QueueEquipmentTexture(desired.texture_path);
    }

    if (binding.m2_instance_id != 0u && inst.m2_instance_id != 0u &&
        binding.bound_model_path == desired.model_path) {
      if (attachment_texture_changed &&
          !IsEquipmentTextureReady(desired.texture_path)) {
        attachment_animation_batch_.push_back(binding.m2_instance_id);
        continue;
      }
      m2::M2ResultStatus status = m2::M2ResultStatus::kReady;
      if (binding.bound_attachment_id != desired.attachment_id) {
        status = system.AttachChildInstance(inst.m2_instance_id, binding.m2_instance_id,
                                            static_cast<std::int32_t>(desired.attachment_id),
                                            m2::M2ChildDestroyPolicy::kDestroyWithParent);
      }
      if (status == m2::M2ResultStatus::kReady && attachment_texture_changed) {
        status = system.ClearReplaceableTexturePath(binding.m2_instance_id,
                                                    desired.replaceable_texture_type);
        if (status == m2::M2ResultStatus::kReady && !desired.texture_path.empty()) {
          status = system.SetReplaceableTexturePath(
              binding.m2_instance_id, desired.replaceable_texture_type, desired.texture_path);
        }
      }
      if (status == m2::M2ResultStatus::kReady) {
        binding.bound_attachment_id = desired.attachment_id;
        binding.bound_texture_path = desired.texture_path;
        attachment_animation_batch_.push_back(binding.m2_instance_id);
        continue;
      }
      if (m2::IsTerminalM2ResultStatus(status)) {
        DestroyItemVisualChildInstances(m2_system_, binding);
        static_cast<void>(system.DestroyInstance(binding.m2_instance_id));
        binding.m2_instance_id = 0u;
        binding.m2_model_id = 0u;
        binding.bound_model_path.clear();
      }
    }

    if (binding.requested_model_path != desired.model_path) {
      if (binding.stream_ticket) {
        m2_system_.ReleaseModelAsync(binding.stream_ticket);
      }
      binding.stream_ticket = {};
      binding.requested_model_path.clear();
      binding.request_failed = false;
    }

    if (!binding.stream_ticket && !binding.request_failed && requests < request_budget) {
      binding.stream_ticket =
          m2_system_.AcquireModelAsync(desired.model_path, m2::M2StreamPriority::kVisibleUnit);
      binding.requested_model_path = desired.model_path;
      binding.request_failed = !binding.stream_ticket;
      ++requests;
    }
    if (!binding.stream_ticket || binding.request_failed || inst.m2_instance_id == 0u ||
        !IsEquipmentTextureReady(desired.texture_path)) {
      continue;
    }

    const auto streamed = m2_system_.QueryModelAsync(binding.stream_ticket);
    if (streamed.state == m2::M2StreamState::kFailed) {
      binding.request_failed = true;
      continue;
    }
    if (streamed.state != m2::M2StreamState::kReady || streamed.model_id == 0u) {
      continue;
    }

    const auto created = m2_system_.CreateInstanceAsync(binding.stream_ticket);
    if (created.status != m2::M2ResultStatus::kReady || created.instance_id == 0u) {
      binding.request_failed = m2::IsTerminalM2ResultStatus(created.status);
      continue;
    }

    const std::uint32_t replacement_instance_id = created.instance_id;
    m2::M2ResultStatus status = m2::M2ResultStatus::kReady;
    if (!desired.texture_path.empty()) {
      status = system.SetReplaceableTexturePath(
          replacement_instance_id, desired.replaceable_texture_type, desired.texture_path);
    }
    if (status == m2::M2ResultStatus::kReady) {
      if (binding.desired.role == ModelAttachmentRole::kQuestOverlay) {
        status = system.SetAnimation(replacement_instance_id,
                                     binding.desired.animation_id);
      } else if (desired.attachment_id == 1u || desired.attachment_id == 2u) {

        static_cast<void>(
            system.SetAnimation(replacement_instance_id, desired.attachment_id == 1u ? 0u : 1u));
      }
      if (status == m2::M2ResultStatus::kReady) {
        status = system.AttachChildInstance(
            inst.m2_instance_id, replacement_instance_id,
            static_cast<std::int32_t>(desired.attachment_id),
            m2::M2ChildDestroyPolicy::kDestroyWithParent);
      }
    }
    if (status != m2::M2ResultStatus::kReady) {
      static_cast<void>(system.DestroyInstance(replacement_instance_id));
      binding.request_failed = m2::IsTerminalM2ResultStatus(status);
      continue;
    }

    const std::uint32_t previous_instance_id = binding.m2_instance_id;
    binding.m2_model_id = streamed.model_id;
    binding.m2_instance_id = replacement_instance_id;
    binding.bound_model_path = desired.model_path;
    binding.bound_texture_path = desired.texture_path;
    binding.bound_attachment_id = desired.attachment_id;
    binding.request_failed = false;
    if (previous_instance_id != 0u && previous_instance_id != replacement_instance_id) {
      DestroyItemVisualChildInstances(m2_system_, binding);
      static_cast<void>(system.DestroyInstance(previous_instance_id));
    }
  }

  for (auto &binding : inst.model_attachments) {
    if (binding.m2_instance_id == 0u) {
      continue;
    }
    for (auto &child : binding.item_visual_children) {
      const auto &desired = child.desired;
      if (desired.model_path.empty()) {
        continue;
      }

      if (child.m2_instance_id != 0u && child.bound_model_path == desired.model_path) {
        attachment_animation_batch_.push_back(child.m2_instance_id);
        continue;
      }

      if (child.requested_model_path != desired.model_path) {
        if (child.stream_ticket) {
          m2_system_.ReleaseModelAsync(child.stream_ticket);
        }
        child.stream_ticket = {};
        child.requested_model_path.clear();
        child.request_failed = false;
      }

      if (!child.stream_ticket && !child.request_failed && requests < request_budget) {
        child.stream_ticket =
            m2_system_.AcquireModelAsync(desired.model_path, m2::M2StreamPriority::kVisibleUnit);
        child.requested_model_path = desired.model_path;
        child.request_failed = !child.stream_ticket;
        ++requests;
      }
      if (!child.stream_ticket || child.request_failed) {
        continue;
      }

      const auto streamed = m2_system_.QueryModelAsync(child.stream_ticket);
      if (streamed.state == m2::M2StreamState::kFailed) {
        child.request_failed = true;
        continue;
      }
      if (streamed.state != m2::M2StreamState::kReady || streamed.model_id == 0u) {
        continue;
      }

      const auto created = m2_system_.CreateInstanceAsync(child.stream_ticket);
      if (created.status != m2::M2ResultStatus::kReady || created.instance_id == 0u) {
        child.request_failed = m2::IsTerminalM2ResultStatus(created.status);
        continue;
      }

      const std::uint32_t replacement_instance_id = created.instance_id;
      const auto attach_status =
          system.AttachChildInstance(binding.m2_instance_id, replacement_instance_id,
                                     static_cast<std::int32_t>(desired.attachment_id),
                                     m2::M2ChildDestroyPolicy::kDestroyWithParent);
      if (attach_status != m2::M2ResultStatus::kReady) {
        static_cast<void>(system.DestroyInstance(replacement_instance_id));
        child.request_failed = m2::IsTerminalM2ResultStatus(attach_status);
        continue;
      }

      const std::uint32_t previous_instance_id = child.m2_instance_id;
      child.m2_model_id = streamed.model_id;
      child.m2_instance_id = replacement_instance_id;
      child.bound_model_path = desired.model_path;
      child.request_failed = false;
      if (previous_instance_id != 0u && previous_instance_id != replacement_instance_id) {
        static_cast<void>(system.DestroyInstance(previous_instance_id));
      }
    }
  }

  ApplyEquipmentHandPose(inst);
  return requests;
}

void ObjectRenderer::ApplyUpperBodyAnimationChannel(RenderInstance &inst) {
  if (inst.m2_instance_id == 0u || inst.m2_model_id == 0u) {
    inst.upper_body_slot_resolved = false;
    inst.upper_body_slot_active = false;
    inst.upper_body_animation_slot = kNoKeyBoneAnimationSlot;
    return;
  }
  if (!inst.upper_body_slot_resolved) {
    const auto primary =
        m2_system_.QueryKeyBone(inst.m2_instance_id, kUpperBodyPrimaryKeyBoneSlot);
    if (primary.status != m2::M2ResultStatus::kReady) {
      return;
    }
    if (primary.present) {
      inst.upper_body_animation_slot = kUpperBodyPrimaryKeyBoneSlot;
    } else {
      const auto fallback = m2_system_.QueryKeyBone(
          inst.m2_instance_id, kUpperBodyFallbackKeyBoneSlot);
      if (fallback.status != m2::M2ResultStatus::kReady) {
        return;
      }
      inst.upper_body_animation_slot = fallback.present
                                           ? kUpperBodyFallbackKeyBoneSlot
                                           : kNoKeyBoneAnimationSlot;
    }
    inst.upper_body_slot_resolved = true;
  }
  if (inst.upper_body_animation_slot == kNoKeyBoneAnimationSlot) {
    return;
  }

  const bool want_active =
      inst.unit_animation.upper_body_only &&
      (inst.unit_animation.looping ||
       !inst.upper_animation.DidAnimationComplete());
  if (!want_active) {
    if (inst.upper_body_slot_active) {
      if (m2_system_.ClearAnimationSlot(inst.m2_instance_id,
                                        inst.upper_body_animation_slot) ==
          m2::M2ResultStatus::kReady) {
        inst.upper_body_slot_active = false;
      }
    }
    return;
  }

  if (m2_system_.SetAnimationSlotSample(
          inst.m2_instance_id, inst.upper_body_animation_slot,
          inst.upper_animation.current_anim(),
          inst.upper_animation.current_time_ms(), 1.0f,
          inst.unit_animation.zero_blend) ==
      m2::M2ResultStatus::kReady) {
    inst.upper_body_slot_active = true;
  }
}

void ObjectRenderer::ApplyEquipmentHandPose(RenderInstance &inst) {
  if (inst.m2_instance_id == 0u) {
    inst.applied_hand_pose_mask = 0u;
    inst.hand_pose_body_instance_id = 0u;
    return;
  }

  constexpr std::uint8_t kRightHand = 0x1u;
  constexpr std::uint8_t kLeftHand = 0x2u;
  constexpr std::uint32_t kRightFingerAnimationSlot = 8u;
  constexpr std::uint32_t kLeftFingerAnimationSlot = 13u;
  std::uint8_t desired_mask = 0u;
  for (const auto &binding : inst.model_attachments) {
    if (binding.m2_instance_id == 0u) {
      continue;
    }
    if (binding.bound_attachment_id == 1u) {
      desired_mask |= kRightHand;
    } else if (binding.bound_attachment_id == 0u || binding.bound_attachment_id == 2u) {
      desired_mask |= kLeftHand;
    }
  }

  if (inst.hand_pose_body_instance_id == inst.m2_instance_id &&
      inst.applied_hand_pose_mask == desired_mask) {
    return;
  }

  auto &system = m2_system_;
  const auto apply_slot = [&](const std::uint8_t bit, const std::uint32_t slot) {
    return (desired_mask & bit) != 0u
               ? system.SetAnimationSlotSample(inst.m2_instance_id, slot, AnimId::kHandsClosed, 0u)
               : system.ClearAnimationSlot(inst.m2_instance_id, slot);
  };
  const auto right = apply_slot(kRightHand, kRightFingerAnimationSlot);
  const auto left = apply_slot(kLeftHand, kLeftFingerAnimationSlot);
  if (right == m2::M2ResultStatus::kReady && left == m2::M2ResultStatus::kReady) {
    inst.applied_hand_pose_mask = desired_mask;
    inst.hand_pose_body_instance_id = inst.m2_instance_id;
  }
}

RenderAssetKind ObjectRenderer::ClassifyRenderAssetPath(const std::string &path) {
  if (path.empty()) {
    return RenderAssetKind::kUnknown;
  }
  if (HasPathSuffix(path, ".m2")) {
    return RenderAssetKind::kM2;
  }
  if (HasPathSuffix(path, ".wmo")) {
    return RenderAssetKind::kAreaScene;
  }
  return RenderAssetKind::kUnknown;
}

bool ObjectRenderer::IsM2RenderReady(const RenderInstance &inst) const {
  if (inst.m2_instance_id == 0u) {
    return false;
  }
  const auto readiness = m2_system_.QueryInstanceReadiness(inst.m2_instance_id);
  return readiness.status == m2::M2ResultStatus::kReady && readiness.render_ready;
}

bool ObjectRenderer::PrepareInstanceBodyForSubmit(
    m2::M2InstanceFramePrepareScope &prepare, RenderInstance &inst,
    const PassBatchUniforms &pass_uniforms) {

  if (inst.m2_instance_id == 0u || !prepare.QueryRenderReady(inst.m2_instance_id) ||
      !ResolveFrameAnimationSample(prepare, inst)) {
    return false;
  }

  auto &system = m2_system_;

  const auto clear_binding_unlocked = [&prepare, &inst, this] {
    prepare.Suspend();
    ClearM2Binding(inst);
    prepare.Resume();
  };

  const m2::M2ResultStatus batch_status = prepare.SetSharedBatchUniforms(
      inst.m2_instance_id, inst.type_id == game::TypeID::kPlayer
                               ? pass_uniforms.character
                               : pass_uniforms.world);
  if (batch_status != m2::M2ResultStatus::kReady) {
    if (m2::IsTerminalM2ResultStatus(batch_status)) {
      clear_binding_unlocked();
    }
    return false;
  }

  if (inst.type_id == game::TypeID::kGameObject) {
    for (std::size_t slot = 0; slot < kGameObjectArtKitTextureSlotCount; ++slot) {
      const std::uint32_t texture_type =
          kGameObjectArtKitTextureTypeBase + static_cast<std::uint32_t>(slot);
      const auto clear_texture_status =
          prepare.ClearReplaceableTexturePath(inst.m2_instance_id, texture_type);
      if (clear_texture_status != m2::M2ResultStatus::kReady) {
        if (m2::IsTerminalM2ResultStatus(clear_texture_status)) {
          clear_binding_unlocked();
        }
        return false;
      }
      const auto &path = inst.art_kit_texture_paths[slot];
      if (!path.empty()) {

        prepare.Suspend();
        const auto set_texture_status =
            system.SetReplaceableTexturePath(inst.m2_instance_id, texture_type, path);
        prepare.Resume();
        if (set_texture_status != m2::M2ResultStatus::kReady) {
          if (m2::IsTerminalM2ResultStatus(set_texture_status)) {
            clear_binding_unlocked();
          }
          return false;
        }
      }
    }
  }

  return true;
}

bool ObjectRenderer::ApplyBodyRenderResult(
    RenderInstance &inst, const m2::M2RenderInstanceResult &result) {
  inst.submitted_draw_count += result.submitted_draw_count;
  if (m2::IsTerminalM2ResultStatus(result.status)) {
    ClearM2Binding(inst);
    return false;
  }
  return true;
}

void ObjectRenderer::RenderInstanceAttachments(
    const std::span<RenderInstance *const> owners, const std::uint8_t view_id,
    const float *view_mtx, const m2::M2RenderPassScope pass_scope,
    const PassBatchUniforms &pass_uniforms) {
  auto &system = m2_system_;
  const m2::M2BatchUniforms &world_uniforms = *pass_uniforms.world_value;

  attachment_placement_targets_scratch_.clear();
  attachment_placement_requests_scratch_.clear();
  attachment_frame_work_scratch_.clear();
  attachment_frame_requests_scratch_.clear();
  attachment_frame_statuses_scratch_.clear();

  std::size_t binding_upper_bound = 0u;
  for (RenderInstance *const owner : owners) {
    binding_upper_bound += owner->model_attachments.size();
  }
  attachment_placement_targets_scratch_.reserve(binding_upper_bound);
  attachment_placement_requests_scratch_.reserve(binding_upper_bound);
  attachment_frame_work_scratch_.reserve(binding_upper_bound);
  attachment_frame_requests_scratch_.reserve(binding_upper_bound);

  for (RenderInstance *const owner : owners) {
    RenderInstance &inst = *owner;
    for (auto &binding : inst.model_attachments) {
      if (binding.m2_instance_id == 0u) {
        continue;
      }
      attachment_placement_targets_scratch_.push_back(
          {.owner = &inst, .binding = &binding});
      attachment_placement_requests_scratch_.push_back(
          {.child_instance_id = binding.m2_instance_id,
           .parent_instance_id = inst.m2_instance_id,
           .attachment_lookup_index = binding.bound_attachment_id});
    }
  }

  if (!attachment_placement_requests_scratch_.empty()) {
    attachment_placement_results_scratch_.resize(
        attachment_placement_requests_scratch_.size());
    system.QueryAttachmentPlacements(attachment_placement_requests_scratch_,
                                     attachment_placement_results_scratch_);
  }

  for (std::size_t candidate = 0u;
       candidate < attachment_placement_targets_scratch_.size(); ++candidate) {
    RenderInstance &inst = *attachment_placement_targets_scratch_[candidate].owner;
    ModelAttachmentBinding &binding =
        *attachment_placement_targets_scratch_[candidate].binding;
    const m2::M2AttachmentPlacementQuery &placement =
        attachment_placement_results_scratch_[candidate];
    const auto &readiness = placement.readiness;
    if (readiness.status != m2::M2ResultStatus::kReady || !readiness.render_ready) {
      continue;
    }

    const auto &attachment = placement.transform;
    if (attachment.status != m2::M2ResultStatus::kReady) {
      continue;
    }

    float inherited_scale = 1.0f;
    if (binding.desired.role == ModelAttachmentRole::kQuestOverlay) {
      const auto &m = attachment.matrix;
      const float row_length_squared = m[0] * m[0] + m[1] * m[1] + m[2] * m[2];
      if (row_length_squared > 0.0f) {
        inherited_scale = std::sqrt(row_length_squared);
      }
    }
    const float applied_scale = binding.desired.scale / inherited_scale;
    const RenderVec3 attachment_scale{applied_scale, applied_scale,
                                      applied_scale};

    const bool overlay_owns_its_shading =
        binding.desired.role == ModelAttachmentRole::kQuestOverlay;
    constexpr RenderVec4 kOverlayUntintedColor{1.0f, 1.0f, 1.0f, 1.0f};
    constexpr float kOverlayOpacity = 1.0f;
    const PendingAttachmentFrameState &pending =
        attachment_frame_work_scratch_.emplace_back(PendingAttachmentFrameState{
            .owner = &inst,
            .binding = &binding,
            .child_world = ScaleMatrix4x4BasisRows(attachment.matrix,
                                                   attachment_scale)});
    attachment_frame_requests_scratch_.push_back(
        {.instance_id = binding.m2_instance_id,
         .world_transform = &pending.child_world,
         .shared_uniforms = pass_uniforms.world,
         .visible = inst.visible,
         .tint_rgba = overlay_owns_its_shading ? kOverlayUntintedColor
                                               : inst.tint_color,
         .alpha = overlay_owns_its_shading ? kOverlayOpacity : inst.alpha});
  }

  if (!attachment_frame_requests_scratch_.empty()) {
    attachment_frame_statuses_scratch_.resize(
        attachment_frame_requests_scratch_.size());
    system.SetAttachmentFrameRenderStates(attachment_frame_requests_scratch_,
                                          attachment_frame_statuses_scratch_);
  }

  const bool transparent_pass = pass_scope == m2::M2RenderPassScope::kTransparentOnly;

  attachment_batch_rows_scratch_.clear();
  attachment_batch_ids_scratch_.clear();
  attachment_batch_draw_ordinals_scratch_.clear();
  for (std::size_t row = 0u; row < attachment_frame_work_scratch_.size(); ++row) {
    const PendingAttachmentFrameState &pending = attachment_frame_work_scratch_[row];
    RenderInstance &inst = *pending.owner;
    ModelAttachmentBinding &binding = *pending.binding;
    const m2::M2ResultStatus child_setup = attachment_frame_statuses_scratch_[row];
    if (child_setup != m2::M2ResultStatus::kReady) {
      if (m2::IsTerminalM2ResultStatus(child_setup)) {
        DestroyItemVisualChildInstances(m2_system_, binding);
        static_cast<void>(system.DestroyInstance(binding.m2_instance_id));
        binding.m2_instance_id = 0u;
        binding.m2_model_id = 0u;
        binding.bound_model_path.clear();
        inst.hand_pose_body_instance_id = 0u;
      }
      continue;
    }
    attachment_batch_rows_scratch_.push_back(row);
    attachment_batch_ids_scratch_.push_back(binding.m2_instance_id);
    if (transparent_pass) {
      attachment_batch_draw_ordinals_scratch_.push_back(binding.transparent_draw_ordinal);
    }
  }

  attachment_batch_results_scratch_.assign(attachment_batch_ids_scratch_.size(), {});
  if (!attachment_batch_ids_scratch_.empty()) {
    const m2::M2TransparentDrawOrdinalScope draw_order_scope(
        attachment_batch_draw_ordinals_scratch_);
    system.RenderInstanceBatch(view_id, attachment_batch_ids_scratch_,
                               RenderMatrix4x4View{view_mtx, 16u}, pass_scope,
                               system.frame_job_system(),
                               kAttachmentInstanceRenderMicroseconds,
                               attachment_batch_results_scratch_);
  }

  for (std::size_t entry = 0u; entry < attachment_batch_rows_scratch_.size(); ++entry) {
    const PendingAttachmentFrameState &pending =
        attachment_frame_work_scratch_[attachment_batch_rows_scratch_[entry]];
    RenderInstance &inst = *pending.owner;
    ModelAttachmentBinding &binding = *pending.binding;
    const RenderMatrix4x4 &child_world = pending.child_world;
    const m2::M2RenderInstanceResult &child_render = attachment_batch_results_scratch_[entry];
    inst.submitted_draw_count += child_render.submitted_draw_count;
    if (m2::IsTerminalM2ResultStatus(child_render.status)) {
      DestroyItemVisualChildInstances(m2_system_, binding);
      static_cast<void>(system.DestroyInstance(binding.m2_instance_id));
      binding.m2_instance_id = 0u;
      binding.m2_model_id = 0u;
      binding.bound_model_path.clear();
      inst.hand_pose_body_instance_id = 0u;
      continue;
    }

    if (binding.desired.role == ModelAttachmentRole::kRanged &&
        binding.desired.visual != nullptr &&
        binding.desired.visual->requires_bowstring &&
        bowstring_renderer_ != nullptr && bowstring_renderer_->is_initialized()) {
      const auto bow_bones = system.QueryInstanceSampleBoneMatrices(binding.m2_instance_id);
      if (bow_bones.status == m2::M2ResultStatus::kReady) {
        const std::size_t bone_count = bow_bones.bone_matrices.size() / 16u;
        if (const auto string_points = system.BuildSelectionTriangleVerticesForSample(
                binding.m2_model_id, bow_bones.bone_matrices, bone_count, child_world);
            string_points.has_value()) {
          const std::array<float, 3> camera_right{view_mtx[0], view_mtx[1], view_mtx[2]};
          const std::uint32_t bowstring_sort_depth =
              transparent_pass
                  ? m2::M2TransparentDrawDepth::Encode(binding.bowstring_draw_ordinal, 0u)
                  : 0u;
          bowstring_renderer_->Render(view_id, string_points->positions[0],
                                      string_points->positions[1], camera_right,
                                      bowstring_sort_depth);
        }
      }
    }

    for (auto &item_visual : binding.item_visual_children) {
      if (item_visual.m2_instance_id == 0u) {
        continue;
      }
      const auto visual_readiness = system.QueryInstanceReadiness(item_visual.m2_instance_id);
      if (visual_readiness.status != m2::M2ResultStatus::kReady || !visual_readiness.render_ready) {
        continue;
      }

      const auto visual_transform = system.QueryAttachmentTransformMatrix(
          binding.m2_instance_id, item_visual.desired.attachment_id);
      if (visual_transform.status != m2::M2ResultStatus::kReady) {
        continue;
      }

      const m2::M2ResultStatus visual_setup = system.SetAttachmentFrameRenderState(
          item_visual.m2_instance_id, visual_transform.matrix, world_uniforms,
          inst.visible, inst.tint_color, inst.alpha);
      if (visual_setup != m2::M2ResultStatus::kReady) {
        if (m2::IsTerminalM2ResultStatus(visual_setup)) {
          static_cast<void>(system.DestroyInstance(item_visual.m2_instance_id));
          item_visual.m2_instance_id = 0u;
          item_visual.m2_model_id = 0u;
          item_visual.bound_model_path.clear();
        }
        continue;
      }

      const std::span<const std::uint32_t> visual_ordinal =
          transparent_pass
              ? std::span<const std::uint32_t>{&item_visual.transparent_draw_ordinal, 1u}
              : std::span<const std::uint32_t>{};
      const m2::M2TransparentDrawOrdinalScope visual_order_scope(visual_ordinal);
      const auto visual_render = system.RenderInstance(
          view_id, item_visual.m2_instance_id, RenderMatrix4x4View{view_mtx, 16u}, pass_scope);
      inst.submitted_draw_count += visual_render.submitted_draw_count;
      if (m2::IsTerminalM2ResultStatus(visual_render.status)) {
        static_cast<void>(system.DestroyInstance(item_visual.m2_instance_id));
        item_visual.m2_instance_id = 0u;
        item_visual.m2_model_id = 0u;
        item_visual.bound_model_path.clear();
      }
    }
  }
}

}

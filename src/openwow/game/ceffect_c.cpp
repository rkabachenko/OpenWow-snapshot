#include "openwow/game/ceffect_c.h"

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/game/missile_node.h"
#include "openwow/game/spell_visual_m2_event.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgobject.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/player_control_runtime.h"
#include "openwow/game/world_session.h"
#include "openwow/render/m2/m2_system.h"
#include "openwow/render/m2/m2_transparent_draw_order.h"
#include "openwow/render/scene/m2_instance_render_cost.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

namespace openwow::game {
namespace {

struct EffectRegistry {
  std::vector<std::unique_ptr<CMissileNode_C>> effects;
  std::uint64_t next_effect_id{1};

  std::vector<std::uint32_t> render_batch_ids;
  std::vector<std::uint32_t> render_batch_draw_ordinals;
  std::vector<render::m2::M2RenderInstanceResult> render_batch_results;
};

EffectRegistry& Registry() {
  static EffectRegistry registry;
  return registry;
}

[[nodiscard]] CEffect_C* FindEffect(const std::uint64_t effect_id) {
  for (const auto& effect : Registry().effects) {
    if (effect->Snapshot().effect_id == effect_id) {
      return effect.get();
    }
  }
  return nullptr;
}

[[nodiscard]] bool TickStrictlyAfter(const std::uint32_t now,
                                     const std::uint32_t deadline) noexcept {
  return deadline != 0u && now != deadline &&
         static_cast<std::int32_t>(now - deadline) >= 0;
}

[[nodiscard]] bool TickEligible(const std::uint32_t now,
                                const std::uint32_t activation) noexcept {
  return activation == 0u || TickStrictlyAfter(now, activation);
}

[[nodiscard]] bool TickAtOrAfter(const std::uint32_t now,
                                 const std::uint32_t deadline) noexcept {
  return deadline != 0u &&
         static_cast<std::int32_t>(now - deadline) >= 0;
}

[[nodiscard]] CGObject_C* ResolveObject(ObjectManager& objects,
                                        const ObjectGuid guid) {
  if (guid.IsEmpty()) {
    return nullptr;
  }
  return dynamic_cast<CGObject_C*>(objects.GetMutable(guid));
}

[[nodiscard]] CGUnit_C* ResolveUnit(ObjectManager& objects,
                                    const ObjectGuid guid) {
  if (guid.IsEmpty()) {
    return nullptr;
  }
  return objects.GetMutableUnit(guid);
}

[[nodiscard]] float ResolveRetailOwnerScale(
    const CGObject_C& owner,
    render::m2::M2System& m2_system) {
  const float descriptor_scale = owner.GetScale();

  const auto instance_id = owner.GetPrimaryM2InstanceId();
  if (instance_id == 0u) {
    return descriptor_scale;
  }

  const auto bounds = m2_system.QueryWorldBoundingBox(instance_id);
  if (bounds.status != render::m2::M2ResultStatus::kReady) {
    return descriptor_scale;
  }

  const float x_span = std::fabs(bounds.box[3] - bounds.box[0]);
  const float y_span = std::fabs(bounds.box[4] - bounds.box[1]);
  const float absolute_scale = std::fabs(descriptor_scale);
  if (!(absolute_scale > 0.0f)) {
    return descriptor_scale;
  }
  const float unscaled_span = std::min(x_span, y_span) / absolute_scale;
  return descriptor_scale * std::max(1.0f, unscaled_span * 0.3f);
}

[[nodiscard]] float ResolveEffectScale(
    const CGObject_C* const owner,
    const std::uint32_t flags,
    const float effect_scale,
    const float minimum_scale,
    const float maximum_scale) {
  float scale = 1.0f;
  if ((flags & CEffectFlags::kScaleFromOwner) != 0u && owner != nullptr) {
    auto* const m2_system = owner->m2_system();
    if (m2_system == nullptr) {
      return 1.0f;
    }
    scale = ResolveRetailOwnerScale(*owner, *m2_system);
  }

  scale *= effect_scale;

  const bool below_minimum = scale < minimum_scale;
  if (maximum_scale <= scale) {
    scale = maximum_scale;
  }
  if (below_minimum) {
    scale = minimum_scale;
  }
  return scale > 0.0f ? scale : 1.0f;
}

[[nodiscard]] float ResolveEffectScale(const CEffectCreateInfo& info) {
  if (info.effect_name == nullptr) {
    return 1.0f;
  }
  return ResolveEffectScale(info.owner, info.flags, info.effect_name->scale,
                            info.effect_name->min_allowed_scale,
                            info.effect_name->max_allowed_scale);
}

void LogEffectModelFailure(
    const data::dbc::SpellVisualEffectNameEntry& effect,
    const render::m2::M2ModelInstanceLoadResult& result) {
  diagnostics::Log(diagnostics::LogLevel::kWarn,
            "CEffect_C: model load/create failed effect_name_id=" +
                std::to_string(effect.id) + " status=" +
                render::m2::M2ResultStatusName(result.status) + " reason=" +
                render::m2::M2ResultReasonName(result.reason) +
                (result.detail.empty() ? std::string()
                                       : " detail=" + result.detail));
}

}

CMissileNode_C* CEffect_C::UnlinkFromObjectList() noexcept {
  auto* const next = owner_list_next_node_;
  if (owner_list_prev_slot_ != nullptr) {
    *owner_list_prev_slot_ = next;
  }
  if (next != nullptr) {
    next->owner_list_prev_slot_ = owner_list_prev_slot_;
  }
  owner_list_prev_slot_ = nullptr;
  owner_list_next_node_ = nullptr;
  return next;
}

void CEffect_C::LinkToObject(CMissileNode_C** const target_list_head) noexcept {
  (void)UnlinkFromObjectList();
  if (target_list_head == nullptr) {
    return;
  }

  auto* const self = static_cast<CMissileNode_C*>(this);
  owner_list_prev_slot_ = target_list_head;
  owner_list_next_node_ = *target_list_head;
  *target_list_head = self;
  if (owner_list_next_node_ != nullptr) {
    owner_list_next_node_->owner_list_prev_slot_ = &owner_list_next_node_;
  }
}

std::uint32_t CEffect_C::GetVisualKitId() const noexcept {
  return visual_kit_id_;
}

std::uint32_t CEffect_C::GetEffectNameId() const noexcept {
  return effect_name_id_;
}

void CEffect_C::SetPosition(
    const std::array<float, 3>& position) noexcept {
  effect_position_ = position;
  resolved_position_ = position;
  impact_pos_[0] = position[0];
  impact_pos_[1] = position[1];
  impact_pos_[2] = position[2];
}

CEffectSnapshot CEffect_C::Snapshot() const noexcept {
  return {
      .effect_id = effect_id_,
      .owner_guid = attachment_owner_guid_,
      .source_guid = ObjectGuid(GetSourceGuid()),
      .spell_id = spell_id_,
      .flags = flags_,
      .effect_name_id = effect_name_id_,
      .visual_kit_id = visual_kit_id_,
      .resource_id = resource_id_,
      .attachment_point = attachment_point_,
      .visual_kit_param = visual_kit_param_,
      .transform_key = transform_key_,
      .lifecycle = lifecycle_,
  };
}

bool CEffect_C::MatchesReplacement(
    const std::uint32_t effect_name_id,
    const std::int32_t attachment_point,
    const std::uint32_t spell_id,
    const std::uint32_t visual_kit_id,
    const std::uintptr_t transform_key) const noexcept {
  if (attachment_point == -1 || effect_name_id_ == 0u ||
      effect_name_id_ != effect_name_id) {
    return false;
  }
  if (visual_kit_id_ != 0u && visual_kit_id_ != visual_kit_id) {
    return false;
  }
  if (transform_key_ != 0u && transform_key_ != transform_key) {
    return false;
  }
  if (attachment_point_ != attachment_point) {
    return false;
  }
  return spell_id == 0u || spell_id_ == spell_id;
}

void CEffect_C::EnableAttachedModelSelector() {
  flags_ |= CEffectFlags::kAttachedModelSelector;
  if (!HasModel()) {
    return;
  }

  const auto status = m2_system().SetAttachedModelVisualSelectorFlag(
      primary_model_instance_id_, true);
  if (render::m2::IsTerminalM2ResultStatus(status)) {
    BeginTeardown();
  }
}

void CEffect_C::RefreshOwnerScale() {
  if (!HasModel() ||
      (flags_ & CEffectFlags::kScaleFromOwner) == 0u) {
    return;
  }

  auto* const owner = ResolveObject(object_manager_, attachment_owner_guid_);
  if (owner == nullptr) {
    return;
  }

  primary_model_scale_ = ResolveEffectScale(
      owner, flags_, effect_record_scale_, effect_record_min_scale_,
      effect_record_max_scale_);
  const auto status = m2_system().SetScale(
      primary_model_instance_id_, primary_model_scale_);
  if (render::m2::IsTerminalM2ResultStatus(status)) {
    BeginTeardown();
  }
}

void CEffect_C::RefreshOwnerAttachmentTransform() {
  TrackOwnerTransform();
}

CMissileNode_C* CEffect_C::AddEffect(const WorldSession& session,
                                     const CEffectCreateInfo& info) {
  if (info.owner == nullptr) {
    return nullptr;
  }

  const std::uint32_t effect_name_id =
      info.effect_name != nullptr ? info.effect_name->id : info.effect_name_id;
  const std::uint32_t visual_kit_id =
      info.visual_kit != nullptr ? info.visual_kit->id : info.visual_kit_id;

  if (info.attachment_point != -1) {
    for (auto* current = *info.owner->GetEffectNodeListHeadSlot();
         current != nullptr;) {
      auto* const next = current->GetNextAttachedEffect();
      if (current->MatchesReplacement(
              effect_name_id, info.attachment_point, info.spell_id,
              visual_kit_id, info.transform_key)) {
        current->BeginTeardown();
      }
      current = next;
    }
  } else {
    diagnostics::Log(diagnostics::LogLevel::kDebug,
              "CEffect_C::AddEffect called with ATTACH_NONE");
  }

  auto* const owner_m2_system = info.owner->m2_system();
  auto* const owner_objects = info.owner->object_manager();
  if (owner_m2_system == nullptr || owner_objects == nullptr) {
    return nullptr;
  }
  auto node = std::make_unique<CMissileNode_C>(*owner_m2_system, *owner_objects,
                                               session);
  auto* const result = node.get();
  auto& registry = Registry();

  result->effect_id_ = registry.next_effect_id++;
  result->attachment_owner_guid_ = info.owner->GetGuid();
  result->visual_kit_id_ = visual_kit_id;
  result->effect_name_id_ = effect_name_id;
  result->resource_id_ = info.resource_id;
  result->attachment_point_ = info.attachment_point;
  result->visual_kit_param_ = info.visual_kit_param;
  result->transform_key_ = info.transform_key;
  result->cleanup_tick_ = info.cleanup_tick;
  result->local_offset_ = info.local_offset;
  result->local_rotation_degrees_ = info.local_rotation_degrees;
  result->world_space_ = info.world_space;
  result->flags_ = info.flags;
  result->spell_id_ = info.spell_id;
  result->sound_kit_id_ =
      info.visual_kit != nullptr ? info.visual_kit->sound_id : 0u;
  if (info.effect_name != nullptr) {
    result->effect_record_scale_ = info.effect_name->scale;
    result->effect_record_min_scale_ = info.effect_name->min_allowed_scale;
    result->effect_record_max_scale_ = info.effect_name->max_allowed_scale;
  }

  const ObjectGuid source_guid =
      !info.source_guid.IsEmpty() ? info.source_guid : info.owner->GetGuid();
  result->source_guid_low_ = source_guid.GetLowPart();
  result->source_guid_high_ = source_guid.GetHighPart();
  result->triggered_event_guid_low_ = info.owner->GetGuid().GetLowPart();
  result->triggered_event_guid_high_ = info.owner->GetGuid().GetHighPart();

  if (info.position != nullptr) {
    result->SetPosition(*info.position);
    result->flags_ |= CEffectFlags::kHasExplicitLocalPosition;
  }

  if (info.effect_name != nullptr && !info.effect_name->file_path.empty()) {
    auto& m2 = result->m2_system();
    const auto loaded =
        m2.LoadModelInstance(std::string(info.effect_name->file_path));
    if (loaded.status == render::m2::M2ResultStatus::kReady &&
        loaded.instance_id != 0u) {
      result->primary_model_instance_id_ = loaded.instance_id;
      result->primary_model_scale_ = ResolveEffectScale(info);

      render::m2::M2ResultStatus setup =
          m2.SetScale(loaded.instance_id, result->primary_model_scale_);
      setup = render::m2::MergeM2ResultStatus(
          setup,
          m2.SetTriggeredEventCallback(
              loaded.instance_id,
              [effect_id = result->effect_id_, owner_objects](
                  const render::m2::M2TriggeredEvent& event) {
                auto* const effect = FindEffect(effect_id);
                if (effect == nullptr || effect->IsTornDown()) {
                  return;
                }
                (void)DispatchSpellVisualM2Event(
                    event,
                    {.sound = [effect](const auto& sound_event) {
                       std::uint32_t handle = 0u;
                       auto& sound = effect->session_.sound_runtime();
                       if (sound.PlaySoundKit(sound_event.data,
                                              sound_event.world_position.data(),
                                              &handle) == 0 &&
                           (effect->flags_ &
                            CEffectFlags::kDoNotBindSoundToOwner) == 0u) {
                         (void)sound.BindSoundHandleToObjectGuid(
                             handle,
                             effect->attachment_owner_guid_.GetRawValue());
                       }
                     },
                     .hit = [effect, owner_objects] {
                       auto* const unit = ResolveUnit(
                           *owner_objects, effect->attachment_owner_guid_);
                       if (unit != nullptr) {
                         RefreshSpellVisualM2HitReaction(*unit,
                                                        effect->session_);
                       }
                     }});
              }));
      if ((result->flags_ & CEffectFlags::kPendingDestroy) != 0u) {
        setup = render::m2::MergeM2ResultStatus(
            setup,
            m2.SetAnimationCompletionCallback(
                loaded.instance_id,
                [effect_id = result->effect_id_](const std::uint32_t) {
                  if (auto* const effect = FindEffect(effect_id);
                      effect != nullptr && !effect->IsTornDown()) {
                    effect->BeginTeardown();
                  }
                }));
      }
      if ((result->flags_ & CEffectFlags::kAttachedModelSelector) != 0u) {
        setup = render::m2::MergeM2ResultStatus(
            setup, m2.SetAttachedModelVisualSelectorFlag(loaded.instance_id,
                                                         true));
      }
      if (render::m2::IsTerminalM2ResultStatus(setup)) {
        result->DestroyPrimaryModelInstance();
      }
    } else {
      LogEffectModelFailure(*info.effect_name, loaded);
    }
  }

  result->LinkToObject(info.owner->GetEffectNodeListHeadSlot());
  registry.effects.push_back(std::move(node));
  return result;
}

CMissileNode_C* CEffect_C::AddLogicalEffect(
    const WorldSession& session, CGObject_C& owner,
    const CEffectSnapshot& state) {
  CEffectCreateInfo info;
  info.owner = &owner;
  info.source_guid = !state.source_guid.IsEmpty() ? state.source_guid
                                                  : owner.GetGuid();
  info.spell_id = state.spell_id;
  info.effect_name_id = state.effect_name_id;
  info.resource_id = state.resource_id;
  info.visual_kit_id = state.visual_kit_id;
  info.attachment_point = state.attachment_point;
  info.flags = state.flags;
  info.visual_kit_param = state.visual_kit_param;
  info.transform_key = state.transform_key;

  return AddEffect(session, info);
}

bool CEffect_C::ComputeAttachmentPosition(
    std::array<float, 3>& out_position,
    std::array<float, 3>& out_orientation) {
  out_position = effect_position_;
  out_orientation = local_rotation_degrees_;

  if (world_space_) {
    for (std::size_t axis = 0; axis < out_position.size(); ++axis) {
      out_position[axis] += local_offset_[axis];
    }
    resolved_position_ = out_position;
    impact_pos_[0] = out_position[0];
    impact_pos_[1] = out_position[1];
    impact_pos_[2] = out_position[2];
    return true;
  }

  auto* const owner = ResolveObject(object_manager_, attachment_owner_guid_);
  if (owner == nullptr) {
    return attachment_owner_guid_.IsEmpty();
  }

  const std::uint32_t owner_instance = owner->GetPrimaryM2InstanceId();
  if (owner_instance == 0u) {
    return false;
  }

  auto& m2 = m2_system();
  if (attachment_point_ >= 0) {
    const auto attachment = m2.QueryAttachmentPosition(
        owner_instance, static_cast<std::uint32_t>(attachment_point_));
    if (attachment.status != render::m2::M2ResultStatus::kReady) {
      return false;
    }
    out_position = attachment.position;
  } else {
    const auto point = m2.QueryModelWorldPoint(owner_instance);
    if (point.status != render::m2::M2ResultStatus::kReady) {
      return false;
    }
    out_position = point.position;
  }

  const float owner_facing =
      (flags_ & CEffectFlags::kUseExplicitFacing) != 0u
          ? explicit_facing_radians_
          : owner->GetOrientation();
  const float cosine = std::cos(owner_facing);
  const float sine = std::sin(owner_facing);
  out_position[0] +=
      local_offset_[0] * cosine - local_offset_[1] * sine;
  out_position[1] +=
      local_offset_[0] * sine + local_offset_[1] * cosine;
  out_position[2] += local_offset_[2];
  out_orientation[2] +=
      owner_facing * (180.0f / 3.14159265358979323846f);
  resolved_position_ = out_position;
  impact_pos_[0] = out_position[0];
  impact_pos_[1] = out_position[1];
  impact_pos_[2] = out_position[2];
  return true;
}

void CEffect_C::PlayEffectSound() {

  if (sound_kit_id_ == 0u || sound_kit_id_ == 0xFFFFFFFFu ||
      (flags_ & CEffectFlags::kSuppressKitSound) != 0u ||
      impact_sound_handle_id_ != 0u) {
    return;
  }

  std::uint32_t handle = 0u;
  auto& sound = session_.sound_runtime();
  audio::SoundKitPlaybackOptions options;
  if ((flags_ & CEffectFlags::kSoundModeOne) != 0u) {
    options.loop_mode = audio::SoundLoopMode::kForceLoop;
  } else {
    options.loop_mode = audio::SoundLoopMode::kForceOneShot;
    if (attachment_owner_guid_ ==
        object_manager_.player_control().ActiveMoverGuid()) {
      options.playback_priority = 110u;
    }
  }
  if (sound.PlaySoundKit(sound_kit_id_, resolved_position_.data(), &handle,
                         options) != 0) {
    return;
  }

  impact_sound_handle_id_ = handle;
  if ((flags_ & CEffectFlags::kDoNotBindSoundToOwner) == 0u &&
      !attachment_owner_guid_.IsEmpty()) {
    (void)sound.BindSoundHandleToObjectGuid(
        handle, attachment_owner_guid_.GetRawValue());
  }
}

void CEffect_C::RunFirstFrameSoundModelUpdate() {
  flags_ |= CEffectFlags::kInitialized;
  lifecycle_ = CEffectLifecycle::kActive;

  if (HasModel() && !attachment_owner_guid_.IsEmpty()) {
    TrackOwnerTransform();
    if (IsTornDown()) {
      return;
    }
  }
  PlayEffectSound();
}

void CEffect_C::TrackOwnerTransform() {
  if (!HasModel()) {
    return;
  }

  std::array<float, 3> position{};
  std::array<float, 3> orientation{};
  if (!ComputeAttachmentPosition(position, orientation)) {

    return;
  }

  auto& m2 = m2_system();
  render::m2::M2ResultStatus status =
      m2.SetPosition(primary_model_instance_id_, position);
  status = render::m2::MergeM2ResultStatus(
      status,
      m2.SetRotationDegrees(primary_model_instance_id_, orientation));
  if (render::m2::IsTerminalM2ResultStatus(status)) {
    BeginTeardown();
  }
}

void CEffect_C::SetEffectEmittersEnabled(const bool enabled) {
  if (!HasModel()) {
    return;
  }
  (void)m2_system().SetEffectEmittersEnabled(
      primary_model_instance_id_, enabled);
}

void CEffect_C::DisableEffectEmittersImmediately() {
  emitter_countdown_ = 0;
  SetEffectEmittersEnabled(false);
}

void CEffect_C::EnableEffectEmittersForTwoUpdates() {
  if (emitter_countdown_ == 0) {
    SetEffectEmittersEnabled(true);
  }
  emitter_countdown_ = 2;
}

void CEffect_C::StopEffectSound() {
  if (impact_sound_handle_id_ == 0u) {
    return;
  }

  auto& sound = session_.sound_runtime();
  if (sound.IsSoundHandlePlaying(impact_sound_handle_id_)) {
    (void)sound.StopActiveSoundHandle(impact_sound_handle_id_, false, 0.15f,
                                      true);
  } else {
    sound.FreeSoundHandle(impact_sound_handle_id_);
  }
  impact_sound_handle_id_ = 0u;
}

bool CEffect_C::RestoreOwnerAlpha() {
  if (auto* const owner = ResolveObject(object_manager_, attachment_owner_guid_);
      owner != nullptr) {
    owner->SetOpacityTarget(owner->GetModelOpacity(),
                            owner_alpha_restore_duration_ms_);
    return true;
  }
  return false;
}

bool CEffect_C::Update(const std::uint32_t frame_tick_ms) {
  if (lifecycle_ == CEffectLifecycle::kTearingDown) {
    return false;
  }

  bool initialized_this_update = false;
  if ((flags_ & CEffectFlags::kInitializationGate) == 0u) {
    RunFirstFrameSoundModelUpdate();
    initialized_this_update = true;
    if (IsTornDown()) {
      return false;
    }
  } else if (lifecycle_ == CEffectLifecycle::kPendingInitialization) {
    lifecycle_ = CEffectLifecycle::kActive;
  }

  if ((flags_ & CEffectFlags::kEmitterCountdown) != 0u &&
      emitter_countdown_ > 0) {
    --emitter_countdown_;
    if (emitter_countdown_ == 0) {
      SetEffectEmittersEnabled(false);
    }
  }

  const bool emulate_retail_parent_attachment =
      HasModel() && attachment_point_ != -1 && !world_space_;
  if ((flags_ & CEffectFlags::kContinuouslyTrackOwner) != 0u ||
      (emulate_retail_parent_attachment && !initialized_this_update)) {
    TrackOwnerTransform();
  }

  const bool cleanup_due = TickStrictlyAfter(frame_tick_ms, cleanup_tick_);
  if ((flags_ & CEffectFlags::kRestoreOwnerAnimationOnEnd) != 0u &&
      cleanup_due) {

    if (ResolveObject(object_manager_, attachment_owner_guid_) == nullptr) {
      return true;
    }
    BeginTeardown();
  } else if (owner_alpha_restore_tick_ != 0u &&
             (owner_alpha_restore_inclusive_
                  ? TickAtOrAfter(frame_tick_ms, owner_alpha_restore_tick_)
                  : TickStrictlyAfter(frame_tick_ms,
                                      owner_alpha_restore_tick_))) {

    if (!RestoreOwnerAlpha()) {
      return true;
    }
    BeginTeardown();
  } else if ((flags_ & CEffectFlags::kPendingDestroy) == 0u && cleanup_due) {
    BeginTeardown();
  }

  if (!IsTornDown() &&
      (flags_ & CEffectFlags::kTrackSoundLifetime) != 0u &&
      !session_.sound_runtime().IsSoundHandlePlaying(
          impact_sound_handle_id_)) {
    BeginTeardown();
  }
  return !IsTornDown();
}

void CEffect_C::ConfigureOwnerAlphaRestore(
    const std::uint32_t tick,
    const std::uint32_t duration_ms) noexcept {
  owner_alpha_restore_tick_ = tick;
  owner_alpha_restore_duration_ms_ = duration_ms;
  owner_alpha_restore_inclusive_ = true;
}

void CEffect_C::CancelOwnerAlphaRestore(const ObjectGuid owner_guid) {
  for (const auto& effect : Registry().effects) {
    if (effect->attachment_owner_guid_ == owner_guid &&
        effect->owner_alpha_restore_tick_ != 0u) {
      effect->BeginTeardown();
    }
  }
  ProcessTeardownList();
}

void CEffect_C::BeginTeardown() {
  if (lifecycle_ == CEffectLifecycle::kTearingDown) {
    return;
  }

  lifecycle_ = CEffectLifecycle::kTearingDown;
  flags_ |= CEffectFlags::kInitializationGate;

  if ((flags_ & CEffectFlags::kRestoreOwnerAnimationOnEnd) != 0u) {
    if (auto* const unit = ResolveUnit(object_manager_, attachment_owner_guid_);
        unit != nullptr) {
      unit->Animation().RestoreStandAnimationAfterEffect(session_);
    }
  }

  (void)UnlinkFromObjectList();
  StopEffectSound();

  if (HasModel()) {
    auto& m2 = m2_system();
    (void)m2.ClearAnimationChangedCallback(primary_model_instance_id_);
    (void)m2.ClearAnimationCompletionCallback(primary_model_instance_id_);
    (void)m2.ClearTriggeredEventCallback(primary_model_instance_id_);
    (void)m2.SetEffectEmittersEnabled(primary_model_instance_id_, false);
    (void)m2.SetVisible(primary_model_instance_id_, false);

    (void)m2.DestroyInstance(primary_model_instance_id_);
    primary_model_instance_id_ = 0u;
  }
}

void CEffect_C::ReleaseOwnerReference() {
  --owner_reference_count_;
  if (owner_reference_count_ == 0u) {
    BeginTeardown();
  }
}

void CEffect_C::UpdateAll(const std::uint32_t frame_tick_ms) {
  auto& effects = Registry().effects;
  for (const auto& effect : effects) {
    if (effect->GetLifecycle() != CEffectLifecycle::kTearingDown &&
        TickEligible(frame_tick_ms, effect->activation_tick_)) {
      (void)effect->Update(frame_tick_ms);
    }
  }
  ProcessTeardownList();
}

render::m2::M2RenderFrameResult CEffect_C::RenderAll(
    const std::uint16_t view_id, const float* const view_matrix,
    const render::m2::M2RenderPassScope pass_scope,
    render::m2::M2TransparentDrawOrder* const transparent_draw_order) {
  render::m2::M2RenderFrameResult result;
  if (view_matrix == nullptr) {
    result.status = render::m2::M2ResultStatus::kFailed;
    result.reason = render::m2::M2ResultReason::kInvalidTransform;
    result.detail = "CEffect_C render view matrix is null";
    return result;
  }

  EffectRegistry& registry = Registry();
  std::vector<std::uint32_t>& ids = registry.render_batch_ids;
  std::vector<std::uint32_t>& ordinals = registry.render_batch_draw_ordinals;
  std::vector<render::m2::M2RenderInstanceResult>& results = registry.render_batch_results;
  render::m2::M2System* run_system = nullptr;
  ids.clear();
  const auto flush_run = [&] {
    if (ids.empty() || run_system == nullptr) {
      return;
    }
    ordinals.clear();
    if (render::m2::M2RenderPassScopeIncludesTransparent(pass_scope) &&
        transparent_draw_order != nullptr) {
      const std::uint32_t first_ordinal =
          transparent_draw_order->Reserve(static_cast<std::uint32_t>(ids.size()));
      ordinals.resize(ids.size());
      std::iota(ordinals.begin(), ordinals.end(), first_ordinal);
    }
    results.assign(ids.size(), {});
    {
      const render::m2::M2TransparentDrawOrdinalScope draw_order_scope(ordinals);
      run_system->RenderInstanceBatch(view_id, ids,
                                      render::RenderMatrix4x4View{view_matrix, 16u},
                                      pass_scope, run_system->frame_job_system(),
                                      render::kCEffectInstanceRenderMicroseconds, results);
    }
    for (const auto& instance_result : results) {
      result.AddInstanceResult(instance_result);
    }
    ids.clear();
  };
  for (const auto& effect : registry.effects) {
    if (effect->IsTornDown() || !effect->HasModel()) {
      continue;
    }
    render::m2::M2System* const system = &effect->m2_system();
    if (system != run_system) {
      flush_run();
      run_system = system;
    }
    ids.push_back(effect->primary_model_instance_id_);
  }
  flush_run();
  return result;
}

void CEffect_C::ProcessTeardownList() {
  auto& effects = Registry().effects;
  for (auto it = effects.begin(); it != effects.end();) {
    const auto& effect = *it;
    if (effect->GetLifecycle() == CEffectLifecycle::kTearingDown &&
        effect->owner_reference_count_ == 0u) {
      it = effects.erase(it);
    } else {
      ++it;
    }
  }
}

void CEffect_C::DetachAllFromOwner(const ObjectGuid owner_guid) {
  for (const auto& effect : Registry().effects) {
    if (effect->attachment_owner_guid_ == owner_guid) {
      effect->BeginTeardown();
    }
  }
  ProcessTeardownList();
}

void CEffect_C::Shutdown() {
  auto& effects = Registry().effects;
  for (const auto& effect : effects) {
    effect->BeginTeardown();
  }
  effects.clear();
}

std::size_t CEffect_C::CountAttached(
    const CGObject_C& owner, const bool include_tearing_down) noexcept {
  std::size_t count = 0u;
  for (auto* current = *owner.GetEffectNodeListHeadSlot(); current != nullptr;
       current = current->GetNextAttachedEffect()) {
    if (include_tearing_down || !current->IsTornDown()) {
      ++count;
    }
  }
  return count;
}

void CEffect_C::BuildWorldTransform(
    float* const out_matrix4x4, const CGUnit_C* const parent_unit) const {
  if (out_matrix4x4 == nullptr) {
    return;
  }

  std::fill_n(out_matrix4x4, 16u, 0.0f);
  out_matrix4x4[0] = 1.0f;
  out_matrix4x4[5] = 1.0f;
  out_matrix4x4[10] = 1.0f;
  out_matrix4x4[15] = 1.0f;

  const float facing =
      (flags_ & CEffectFlags::kUseExplicitFacing) != 0u
          ? explicit_facing_radians_
          : (parent_unit != nullptr ? parent_unit->GetOrientation() : 0.0f);
  if (facing != 0.0f) {
    const float cosine = std::cos(facing);
    const float sine = std::sin(facing);
    out_matrix4x4[0] = cosine;
    out_matrix4x4[1] = -sine;
    out_matrix4x4[4] = sine;
    out_matrix4x4[5] = cosine;
  }

  const float scale = primary_model_scale_ > 0.0f ? primary_model_scale_ : 1.0f;
  for (std::size_t row = 0; row < 3u; ++row) {
    for (std::size_t column = 0; column < 3u; ++column) {
      out_matrix4x4[row * 4u + column] *= scale;
    }
  }
  out_matrix4x4[12] = resolved_position_[0];
  out_matrix4x4[13] = resolved_position_[1];
  out_matrix4x4[14] = resolved_position_[2];
}

}

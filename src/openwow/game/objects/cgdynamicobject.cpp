
#include "openwow/game/objects/cgdynamicobject.h"

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/spell_c_internals.h"
#include "openwow/game/spell_visual.h"
#include "openwow/game/spell_visual_system.h"
#include "openwow/game/violence_level.h"
#include "openwow/game/world_session.h"
#include "openwow/render/api/math/render_matrix_math.h"
#include "openwow/render/m2/m2_system.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/world/camera/world_camera.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_set>
#include <utility>

namespace openwow::game {

namespace {

constexpr std::uint32_t kDynamicObjectBirthAnimationId = 0x7Fu;
constexpr std::uint32_t kDynamicObjectDirectedCastAnimationId = 0x9Eu;
constexpr std::uint32_t kDynamicObjectDestroyAnimationId = 0x9Fu;
constexpr std::uint32_t kSpellAttributesEx5SetsM2EffectContext = 0x40000000u;
constexpr float kDynamicObjectMinBoundingSphereRadius = 0.001f;
constexpr std::uint32_t kDynamicObjectSoundEvent = 0x444E5324u;
constexpr std::uint32_t kDynamicObjectShakeEvent = 0x4B485324u;

}

CGDynamicObject_C::CGDynamicObject_C() : CGObject_C(TypeID::kDynamicObject) {}

CGDynamicObject_C::CGDynamicObject_C(ObjectGuid guid)
    : CGObject_C(guid, TypeID::kDynamicObject) {}

CGDynamicObject_C::CGDynamicObject_C(ObjectManager& objects)
    : CGObject_C(objects, TypeID::kDynamicObject) {}

CGDynamicObject_C::CGDynamicObject_C(ObjectManager& objects, ObjectGuid guid)
    : CGObject_C(objects, guid, TypeID::kDynamicObject) {}

CGDynamicObject_C::~CGDynamicObject_C() {
  ReleaseTransientRuntimeState();
}

std::vector<std::uint16_t> CGDynamicObject_C::ApplyCreateUpdate(
    const CreateObjectUpdate& upd) {
  auto updated_fields = CGObject_C::ApplyCreateUpdate(upd);
  if (!upd.defer_post_init) {
    OnCreate();
  }
  return updated_fields;
}

void CGDynamicObject_C::FinalizeCreateUpdate(const CreateObjectUpdate& upd) {
  CGObject_C::FinalizeCreateUpdate(upd);
  OnCreate();
}

void CGDynamicObject_C::FinalizeWorldPublication() {

  SetupSpellVisualKit();
  CGObject_C::FinalizeWorldPublication();
}

ObjectGuid CGDynamicObject_C::GetCaster() const {
  return GetGuidField(DYNAMICOBJECT_CASTER);
}

std::uint32_t CGDynamicObject_C::GetBytes() const {
  return GetUInt32(DYNAMICOBJECT_BYTES);
}

std::uint32_t CGDynamicObject_C::GetSpellId() const {
  return GetUInt32(DYNAMICOBJECT_SPELLID);
}

float CGDynamicObject_C::GetRadius() const {
  return GetFloat(DYNAMICOBJECT_RADIUS);
}

std::uint32_t CGDynamicObject_C::GetCastTime() const {
  return GetUInt32(DYNAMICOBJECT_CASTTIME);
}

DynamicObjectType CGDynamicObject_C::GetDynObjType() const {
  return static_cast<DynamicObjectType>(GetBytes() & 0xFF);
}

bool CGDynamicObject_C::IsPortal() const {
  return GetDynObjType() == DynamicObjectType::Portal;
}

bool CGDynamicObject_C::IsAreaSpell() const {
  return GetDynObjType() == DynamicObjectType::AreaSpell;
}

bool CGDynamicObject_C::IsFarsightFocus() const {
  return GetDynObjType() == DynamicObjectType::FarsightFocus;
}

bool CGDynamicObject_C::IsRaidMarker() const {
  return GetDynObjType() == DynamicObjectType::RaidMarker;
}

void CGDynamicObject_C::PrepareForWorldRemoval() {
  CGObject_C::PrepareForWorldRemoval();
  ReleaseTransientRuntimeState();
}

void CGDynamicObject_C::RegisterTransientVisualCleanup(std::function<void()> cleanup) {
  if (transient_visual_cleanup_) {
    auto previous_cleanup = std::move(transient_visual_cleanup_);
    transient_visual_cleanup_ = {};
    previous_cleanup();
  }
  transient_visual_cleanup_ = std::move(cleanup);
}

void CGDynamicObject_C::TrackTransientSoundHandle(const std::uint32_t handle_id) {
  if (transient_sound_handle_id_ != 0u &&
      transient_sound_handle_id_ != handle_id) {
    (void)sound_runtime().StopActiveSoundHandle(
        transient_sound_handle_id_, false, 3.0f, true);
  }
  transient_sound_handle_id_ = handle_id;
}

bool CGDynamicObject_C::HasTransientRuntimeState() const {
  return static_cast<bool>(transient_visual_cleanup_) || transient_sound_handle_id_ != 0u;
}

void CGDynamicObject_C::ReleaseTransientRuntimeState() {
  if (transient_visual_cleanup_) {
    auto cleanup = std::move(transient_visual_cleanup_);
    transient_visual_cleanup_ = {};
    cleanup();
  }
  area_model_handle_ = 0u;

  if (transient_sound_handle_id_ != 0u) {
    (void)sound_runtime().StopActiveSoundHandle(
        transient_sound_handle_id_, false, 3.0f, true);
    transient_sound_handle_id_ = 0u;
  }
}

void CGDynamicObject_C::ApplyVisualScaleToNativeScale(const float scale) {
  const float safe_previous =
      std::isfinite(applied_visual_scale_) && applied_visual_scale_ > 0.0f
          ? applied_visual_scale_
          : 1.0f;
  const float safe_next = std::isfinite(scale) && scale > 0.0f ? scale : 1.0f;
  SetNativeScale((GetNativeScale() / safe_previous) * safe_next);
  applied_visual_scale_ = safe_next;
}

void CGDynamicObject_C::QueryModelRebuildFlags(
    std::uint8_t flags,
    std::uint32_t& out_needs_construct,
    std::uint32_t& out_needs_refresh) {
  CGObject_C::QueryModelRebuildFlags(flags, out_needs_construct,
                                     out_needs_refresh);
  if (static_model_flag_) {
    out_needs_refresh = 1;
  }
}

DynamicObjectVisualState CGDynamicObject_C::ResolveVisualState(
    const openwow::data::dbc::DbcLoader& dbc,
    const std::int32_t violence_level) const {
  return ResolveVisualStateImpl(dbc, violence_level, true);
}

DynamicObjectVisualState CGDynamicObject_C::ResolveVisualStateImpl(
    const openwow::data::dbc::DbcLoader& dbc,
    const std::int32_t violence_level,
    const bool emit_diagnostics) const {
  DynamicObjectVisualState state;
  state.spell_id = GetSpellId();
  state.violence_level = violence_level;

  const auto* const spell = state.spell_id != 0u
      ? dbc.spell().LookupEntry(state.spell_id)
      : nullptr;
  if (spell == nullptr) {

    if (emit_diagnostics && state.spell_id != 0u) {
      static std::mutex mutex;
      static std::unordered_set<std::uint32_t> reported;
      std::lock_guard lock(mutex);
      if (reported.insert(state.spell_id).second) {
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                           "NOSPELLIDFOUND|" + std::to_string(state.spell_id));
      }
    }
    return state;
  }

  state.has_spell_record = true;
  state.spell_attributes_ex5_bit30 =
      (spell->attributes_ex5 & kSpellAttributesEx5SetsM2EffectContext) != 0u;

  const auto resolved =
      ResolveSpellVisualEffectRecords(dbc, *spell, violence_level,
                                      emit_diagnostics);
  state.spell_visual_id = resolved.visual_id;
  if (resolved.visual != nullptr) {
    state.has_spell_visual_record = true;
    state.spell_visual_kit_id = resolved.visual->persistent_area_kit;
  }

  if (resolved.kit != nullptr) {
    state.has_spell_visual_kit_record = true;
    state.sound_kit_id = resolved.kit->sound_id;
    for (std::size_t index = 0; index < 4u; ++index) {
      if (resolved.kit->proc_type[index] != 9u) {
        continue;
      }

      const double raw_model_id = resolved.kit->proc_param_zero[index];
      const double rounded_model_id = std::nearbyint(raw_model_id);
      if (std::isfinite(rounded_model_id) &&
          rounded_model_id >= std::numeric_limits<std::int32_t>::min() &&
          rounded_model_id <= std::numeric_limits<std::int32_t>::max()) {
        state.area_model.model_id =
            static_cast<std::int32_t>(rounded_model_id);
        state.area_model.rate = resolved.kit->proc_param_one[index];
        state.has_area_model = true;
      }
      break;
    }
  }

  if (resolved.effect != nullptr) {
    state.effect_id = resolved.effect->id;
    state.model_path = std::string(resolved.effect->file_path);
    state.area_effect_size = resolved.effect->area_effect_size;
    state.effect_scale = resolved.effect->scale;
  }
  return state;
}

void CGDynamicObject_C::SetupSpellVisualKit() {
  if (static_model_flag_) {
    return;
  }

  const std::uint32_t spell_id = GetSpellId();
  if (spell_id == 0) {
    return;
  }

  const auto* const objects = object_manager();
  if (objects == nullptr) {
    return;
  }
  const auto* const dbc = &objects->dbc_loader();

  const auto visual = ResolveVisualStateImpl(
      *dbc, GetClientViolenceLevel(), false);
  if (!visual.has_spell_visual_kit_record) {
    return;
  }

  ReleaseTransientRuntimeState();

  if (visual.has_area_model) {

    const Position pos = GetRawPosition();
    const float position[3] = {pos.x, pos.y, pos.z};
    const std::uintptr_t area_model = BlizzardObject_Create(
        position, GetRadius(), visual.area_model.model_id,
        visual.area_model.rate, dbc);
    if (area_model != 0u) {
      area_model_handle_ = area_model;
      RegisterTransientVisualCleanup([area_model]() {
        SpellVisualKit_AreaModel_Cleanup(area_model);
      });
    }
  }

  if (visual.sound_kit_id != 0u) {
    const Position pos = GetPosition();
    const float position[3] = {pos.x, pos.y, pos.z};

    openwow::audio::SoundKitPlaybackOptions options{};
    options.volume_scale = 2.0f;
    options.loop_mode = openwow::audio::SoundLoopMode::kForceLoop;

    std::uint32_t handle_out = 0;
    (void)sound_runtime().PlaySoundKit(
        visual.sound_kit_id, position, &handle_out, options);

    if (handle_out != 0) {
      TrackTransientSoundHandle(handle_out);
    }
  }
}

void CGDynamicObject_C::Cleanup(int reason) {
  if (reason != 1) {
    return;
  }

  const std::uint32_t instance_id = GetPrimaryM2InstanceId();
  if (instance_id == 0) {
    return;
  }

  auto& m2_system = *this->m2_system();
  if (!m2_system.InstanceModelHasAnimation(instance_id,
                                           kDynamicObjectDestroyAnimationId)) {
    return;
  }

  auto* const objects = object_manager();
  if (objects == nullptr) {
    return;
  }

  bool has_lifetime_hold = objects->AcquireObjectLifetimeHold(GetGuid());
  if (has_lifetime_hold) {
    const auto callback_status = m2_system.SetAnimationCompletionCallback(
        instance_id, [objects, guid = GetGuid()](std::uint32_t) {
          const auto release_status = objects->ReleaseObjectLifetimeHold(guid);
          if (!release_status) {
            openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                               "CGDynamicObject_C::Cleanup completion callback failed to release "
                               "object lifetime hold");
          }
        });
    if (openwow::render::m2::IsTerminalM2ResultStatus(callback_status)) {
      const auto release_status = objects->ReleaseObjectLifetimeHold(GetGuid());
      if (!release_status) {
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                           "CGDynamicObject_C::Cleanup failed to release object lifetime hold "
                           "after callback registration failure");
      }
      has_lifetime_hold = false;
    }
  }

  const auto animation_status =
      m2_system.SetAnimation(instance_id, kDynamicObjectDestroyAnimationId);
  if (openwow::render::m2::IsTerminalM2ResultStatus(animation_status)) {
    if (has_lifetime_hold) {
      const auto clear_status = m2_system.ClearAnimationCompletionCallback(instance_id);
      if (openwow::render::m2::IsTerminalM2ResultStatus(clear_status)) {
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                           "CGDynamicObject_C::Cleanup failed to clear M2 completion callback");
      }
      const auto release_status = objects->ReleaseObjectLifetimeHold(GetGuid());
      if (!release_status) {
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                           "CGDynamicObject_C::Cleanup failed to release object lifetime hold "
                           "after destroy animation request failure");
      }
    }
  }
}

void CGDynamicObject_C::OnCreate() {

  {
    const ObjectGuid caster = GetCaster();
    const auto raw = caster.GetRawValue();
    const auto caster_low  = static_cast<std::uint32_t>(raw);
    const auto caster_high = static_cast<std::uint32_t>(raw >> 32);
    const bool pending = PendingDynObjVisualList::Get().HasMatchingEntry(
        caster_low, caster_high, GetSpellId(), GetCastTime());
    SetStaticModelFlag(pending);
  }

  SetupSpellVisualKit();

  if (!IsFarsightFocus()) {
    return;
  }

  auto* const objects = object_manager();
  if (objects == nullptr) {
    return;
  }

  const auto local_player_guid = objects->GetLocalPlayerGuid();
  if (local_player_guid.IsEmpty() || GetCaster() != local_player_guid) {
    return;
  }

  auto* const local_player = objects->GetMutablePlayer(local_player_guid);
  if (local_player == nullptr) {
    return;
  }

  if (local_player->GetFarsightTarget() != GetGuid()) {
    return;
  }

}

bool CGDynamicObject_C::ActivatePendingVisualIfReady() {
  if (!static_model_flag_) {
    return false;
  }

  const auto raw_caster = GetCaster().GetRawValue();
  if (PendingDynObjVisualList::Get().HasMatchingEntry(
          static_cast<std::uint32_t>(raw_caster),
          static_cast<std::uint32_t>(raw_caster >> 32), GetSpellId(),
          GetCastTime())) {
    return false;
  }

  static_model_flag_ = false;
  const auto instance_id = GetPrimaryM2InstanceId();
  if (instance_id == 0u) {
    return true;
  }

  auto& m2_system = *this->m2_system();
  (void)m2_system.SetEffectEmittersEnabled(instance_id, true);
  if (m2_system.InstanceModelHasAnimation(instance_id,
                                          kDynamicObjectBirthAnimationId)) {
    (void)m2_system.SetAnimation(instance_id,
                                 kDynamicObjectBirthAnimationId);
  }
  SetupSpellVisualKit();
  return true;
}

void CGDynamicObject_C::GetWorldMatrix(float* out_matrix) const {
  if (!out_matrix) return;

  auto world_matrix = render::BuildRotationMatrix4x4Z(GetFacing());
  const Position pos = GetRawPosition();
  world_matrix[12] = pos.x;
  world_matrix[13] = pos.y;
  world_matrix[14] = pos.z;

  const ObjectGuid transport_guid = GetTransportGUID();
  if (!transport_guid.IsEmpty()) {

    const auto* const objects = object_manager();
    const CGObject_C* transport =
        objects != nullptr ? objects->Get(transport_guid) : nullptr;
    if (transport) {
      render::RenderMatrix4x4 transport_matrix{};
      transport->GetWorldMatrix(transport_matrix.data());
      world_matrix = render::MultiplyMatrix4x4(world_matrix, transport_matrix);
    }
  }
  std::copy(world_matrix.begin(), world_matrix.end(), out_matrix);
}

void CGDynamicObject_C::ApplyModelParentTransform(
    const float* const parent_matrix) {
  CGObject_C::ApplyModelParentTransform(parent_matrix);
  if (area_model_handle_ != 0u) {
    (void)SpellVisualKit_AreaModel_SetTransformMatrix(area_model_handle_,
                                                       parent_matrix);
  }
}

void CGDynamicObject_C::OnRenderUpdate(
    openwow::world::WorldCamera* const camera,
    const std::uint32_t event_type,
    std::uint32_t visual_id,
    float* position,
    std::uint32_t flags) {
  if (position == nullptr || visual_id == 0u) {
    return;
  }

  if (event_type == kDynamicObjectSoundEvent) {
    (void)sound_runtime().PlaySoundKit(
        visual_id, position);
  } else if (event_type == kDynamicObjectShakeEvent) {
    if (camera != nullptr) {
      camera->TriggerSpellEffectCameraShakes(
          visual_id, {position[0], position[1], position[2]});
    }
  }
  (void)flags;
}

void CGDynamicObject_C::SetupAnimation() {
  const std::uint32_t instance_id = GetPrimaryM2InstanceId();
  if (instance_id == 0) {
    return;
  }

  const std::uint32_t animation_id =
      has_directed_cast_anim_ ? kDynamicObjectDirectedCastAnimationId : 0u;
  auto& m2_system = *this->m2_system();
  if (m2_system.InstanceModelHasAnimation(instance_id, animation_id)) {
    (void)m2_system.SetAnimation(instance_id, animation_id);
  }
}

bool CGDynamicObject_C::OnModelLoaded(
    const std::uint32_t instance_id,
    const DynamicObjectVisualState& visual) {
  if (instance_id == 0u) {
    return false;
  }

  auto& m2_system = *this->m2_system();

  has_directed_cast_anim_ = m2_system.InstanceModelHasAnimation(
      instance_id, kDynamicObjectDirectedCastAnimationId);

  openwow::render::m2::M2InstanceEffectContext effect_context;
  effect_context.spell_attributes_ex5_bit30 =
      visual.spell_attributes_ex5_bit30;
  const auto context_status =
      m2_system.SetInstanceEffectContext(instance_id, effect_context);
  if (openwow::render::m2::IsTerminalM2ResultStatus(context_status)) {
    return false;
  }

  object_scale_ = 1.0f;
  const DynamicObjectType type = GetDynObjType();
  if (type != DynamicObjectType::AreaSpell &&
      type != DynamicObjectType::FarsightFocus) {
    const float descriptor_radius = GetRadius();

    float bounding_radius = 0.0f;
    if (const auto model_sphere = m2_system.QueryInstanceModelBoundingSphere(instance_id);
        model_sphere.status == render::m2::M2ResultStatus::kReady) {
      bounding_radius = model_sphere.sphere[3];
    }

    if (bounding_radius > kDynamicObjectMinBoundingSphereRadius) {
      object_scale_ = descriptor_radius / bounding_radius;
    } else if (visual.effect_id != 0u && visual.area_effect_size > 0.0f) {
      object_scale_ = descriptor_radius / visual.area_effect_size;
    }
  }

  if (visual.effect_id != 0u && visual.effect_scale > 0.0f) {
    object_scale_ *= visual.effect_scale;
  }

  ApplyVisualScaleToNativeScale(object_scale_);

  if (visual.effect_id != 0u) {
    SetOpacityTarget(1.0f, 0);
  }

  if (m2_system.InstanceModelHasAnimation(instance_id,
                                          kDynamicObjectBirthAnimationId)) {
    const auto animation_status =
        m2_system.SetAnimation(instance_id, kDynamicObjectBirthAnimationId);
    if (openwow::render::m2::IsTerminalM2ResultStatus(animation_status)) {
      return false;
    }
  }
  return true;
}

bool CGDynamicObject_C::OnModelLoaded(const std::uint32_t instance_id) {
  DynamicObjectVisualState visual;
  visual.spell_id = GetSpellId();
  visual.violence_level = GetClientViolenceLevel();
  const auto* const objects = object_manager();
  const auto* const dbc = objects != nullptr ? &objects->dbc_loader() : nullptr;
  if (dbc != nullptr) {
    visual = ResolveVisualState(*dbc, visual.violence_level);
  }
  return OnModelLoaded(instance_id, visual);
}

}

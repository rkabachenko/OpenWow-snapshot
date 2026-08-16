
#include "openwow/game/missile_node.h"
#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/game/player_control_runtime.h"
#include "openwow/game/world_session.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/game/display_info_resolver.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgobject.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/spell_visual_system.h"
#include "openwow/game/spell_visual_m2_event.h"
#include "openwow/game/vehicle_passenger.h"
#include "openwow/render/m2/m2_system.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace openwow::game {

namespace {

constexpr std::uint32_t kMountTransitionAnimationId = 0x7Fu;
constexpr std::uint32_t kLocalImpactPlaybackPriority = 110u;
CMissileNode_C* g_active_world_spell_visual_effect_head = nullptr;

void LogMissileM2Failure(const std::string& model_path,
                         const render::m2::M2ModelInstanceLoadResult& result) {
  diagnostics::Log(diagnostics::LogLevel::kWarn,
            "CMissileNode_C: model load/create failed for " + model_path +
                " status=" + render::m2::M2ResultStatusName(result.status) +
                " reason=" + render::m2::M2ResultReasonName(result.reason) +
                (result.detail.empty() ? std::string() : " detail=" + result.detail));
}

[[nodiscard]] bool IsDirectVehiclePassengerOf(const CGUnit_C& candidate,
                                              const CGUnit_C& owner) {
  const auto* const passenger = candidate.Vehicle().GetVehiclePassengerComponent();
  return passenger != nullptr && passenger->GetVehicleUnit() == &owner;
}

[[nodiscard]] render::m2::M2ParticleColorRecord ToRenderParticleColors(
    const DisplayParticleColorRecord& record) {
  render::m2::M2ParticleColorRecord converted;
  converted.start = record.start;
  converted.mid = record.mid;
  converted.end = record.end;
  return converted;
}

[[nodiscard]] CGUnit_C* ResolveMissileSourceUnit(const CMissileNode_C& missile_node) {
  return missile_node.owner_object_manager().GetMutableUnit(
      ObjectGuid(missile_node.GetSourceGuid()));
}

[[nodiscard]] CGObject_C* ResolveTriggeredEventObject(
    const CMissileNode_C& missile_node) {
  return dynamic_cast<CGObject_C*>(
      missile_node.owner_object_manager().GetMutable(
          ObjectGuid(missile_node.GetTriggeredEventObjectGuid())));
}

[[nodiscard]] bool CanApplyPlayerEquipmentObjectItemVisual(
    const CGUnit_C& owner) {
  const auto* player = dynamic_cast<const CGPlayer_C*>(&owner);
  if (player == nullptr) {
    return false;
  }

  return player->Presentation().CurrentDisplayId() == player->Presentation().NativeDisplayId();
}

[[nodiscard]] bool ShouldApplyObjectItemVisualToOwner(
    const CGUnit_C& owner,
    const EquipmentSlot slot) {

  return slot == EquipmentSlot::Head ||
         CanApplyPlayerEquipmentObjectItemVisual(owner);
}

void HandlePendingSpellEffectCreatureTemplateLookup(
    CMissileNode_C* missile_node,
    const std::uint32_t creature_entry,
    const bool success) {
  if (!success || missile_node == nullptr) {
    return;
  }

  const auto* creature_template =
      missile_node->owner_object_manager().query_cache().GetCreatureTemplate(
          creature_entry);
  if (creature_template == nullptr) {
    return;
  }

  if (!missile_node->CreatePrimaryModelFromCreatureDisplay(creature_template->display_ids[0])) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "CMissileNode_C: failed to create pending creature display model " +
                           std::to_string(creature_template->display_ids[0]));
  }
}

[[nodiscard]] std::uint32_t FindSpellEffectCreatureEntry(
    const data::dbc::DbcLoader& dbc,
    const std::uint32_t spell_id) {
  const auto* spell = dbc.spell().LookupEntry(spell_id);
  if (spell == nullptr) {
    return 0;
  }

  for (std::size_t index = 0; index < spell->effect.size(); ++index) {
    if (spell->effect[index] == 6u && spell->effect_apply_aura[index] == 78u &&
        spell->effect_misc_value[index] > 0) {
      return static_cast<std::uint32_t>(spell->effect_misc_value[index]);
    }
  }

  return 0;
}

}

CMissileNode_C** GetActiveWorldSpellVisualEffectListHeadSlot() {
  return &g_active_world_spell_visual_effect_head;
}

CMissileNode_C::~CMissileNode_C() {
  RestoreAttachedUnitVisualState();
  ReleaseObjectItemVisualState();
  ReleaseMountTransitionState();
  CancelPendingCreatureTemplateLookup();
  CleanupImpactSound();
  (void)UnlinkFromObjectList();
  DestroyPrimaryModelInstance();
  ReleaseLoopingLightningHandles();
}

void CMissileNode_C::CleanupImpactSound() {
  if (impact_sound_handle_id_ == 0) {
    return;
  }

  auto& sound = session().sound_runtime();
  if (sound.IsSoundHandlePlaying(impact_sound_handle_id_)) {
    sound.StopActiveSoundHandle(impact_sound_handle_id_, false, 0.15f, true);
  } else {
    sound.FreeSoundHandle(impact_sound_handle_id_);
  }
  impact_sound_handle_id_ = 0;
}

void CMissileNode_C::ReleaseLoopingLightningHandles() {
  for (auto& handle : looping_lightning_handles_) {
    if (handle.IsValid()) {
      LightningObject_ReleaseRetainedReference(handle);
    }
    handle = {};
  }
}

void CMissileNode_C::SetLoopingLightningHandle(const std::size_t slot,
                                               const LightningObjectHandle handle) {
  if (slot >= looping_lightning_handles_.size()) {
    return;
  }

  auto& current = looping_lightning_handles_[slot];
  if (current == handle) {
    return;
  }

  if (current.IsValid()) {
    LightningObject_ReleaseRetainedReference(current);
  }

  current = handle;
}

LightningObjectHandle CMissileNode_C::GetLoopingLightningHandle(
    const std::size_t slot) const {
  if (slot >= looping_lightning_handles_.size()) {
    return {};
  }

  return looping_lightning_handles_[slot];
}

std::optional<EquipmentSlot> CMissileNode_C::ResolveObjectItemVisualSlot(
    const std::uint32_t inventory_type) {
  switch (inventory_type) {
    case 1:
      return EquipmentSlot::Head;
    case 3:
      return EquipmentSlot::Shoulder;
    case 4:
      return EquipmentSlot::Shirt;
    case 5:
    case 20:
      return EquipmentSlot::Chest;
    case 6:
      return EquipmentSlot::Waist;
    case 7:
      return EquipmentSlot::Legs;
    case 8:
      return EquipmentSlot::Feet;
    case 9:
      return EquipmentSlot::Wrist;
    case 10:
      return EquipmentSlot::Hands;
    case 16:
      return EquipmentSlot::Back;
    case 19:
      return EquipmentSlot::Tabard;
    default:
      return std::nullopt;
  }
}

bool CMissileNode_C::SetAttachedModelVisualSelectorFlag(const bool enabled) {
  return m2_system().SetAttachedModelVisualSelectorFlag(
             primary_model_instance_id_, enabled) ==
         render::m2::M2ResultStatus::kReady;
}

void CMissileNode_C::PlayImpactSound(
    std::uint32_t sound_kit_id,
    std::uint32_t impact_context_value,
    std::uint32_t impact_event_value,
    const float* position,
    CGObject_C* event_object,
    std::uint32_t distance_value) {
  if (event_object == nullptr) {
    return;
  }

  LinkToObject(event_object->GetEffectNodeListHeadSlot());

  ++sequence_counter_;
  impact_sound_context_value_ = impact_context_value;
  timestamp_start_ = impact_event_value;
  impact_sound_distance_ = distance_value;

  const auto guid = event_object->GetGuid();
  source_guid_low_ = static_cast<std::uint32_t>(guid.GetRawValue() & 0xFFFFFFFF);
  source_guid_high_ = static_cast<std::uint32_t>(guid.GetRawValue() >> 32);

  if (position != nullptr) {
    impact_pos_[0] = position[0];
    impact_pos_[1] = position[1];
    impact_pos_[2] = position[2];
  } else {
    const auto object_position = event_object->GetPosition();
    impact_pos_[0] = object_position.x;
    impact_pos_[1] = object_position.y;
    impact_pos_[2] = object_position.z;
  }

  openwow::audio::SoundKitPlaybackOptions options;
  if ((flags_ & 0x1u) != 0u) {
    options.loop_mode = openwow::audio::SoundLoopMode::kForceLoop;
  } else {
    options.loop_mode = openwow::audio::SoundLoopMode::kForceOneShot;
    if (guid == object_manager().player_control().ActiveMoverGuid()) {
      options.playback_priority = kLocalImpactPlaybackPriority;
    }
  }

  impact_sound_handle_id_ = 0;
  auto& sound = session().sound_runtime();
  if (sound.PlaySoundKit(sound_kit_id, impact_pos_, &impact_sound_handle_id_,
                         options) == 0) {
    if ((flags_ & 0x200000u) == 0u) {
      (void)sound.BindSoundHandleToObjectGuid(impact_sound_handle_id_,
                                              guid.GetRawValue());
    }
  }

  flags_ |= 0x100800u;
}

void CMissileNode_C::SetupFromObject(
    std::uint32_t spell_id,
    std::uint32_t timestamp_start,
    std::uint32_t timestamp_end,
    const std::uint8_t* guid_data,
    std::uint32_t listen_distance) {
  if (!guid_data) return;

  timestamp_end_ = timestamp_end;
  spell_id_ = spell_id;
  timestamp_start_ = timestamp_start;

  source_guid_low_ = *reinterpret_cast<const std::uint32_t*>(guid_data);
  source_guid_high_ = *reinterpret_cast<const std::uint32_t*>(guid_data + 4);

  if (auto* owner = ResolveMissileSourceUnit(*this); owner != nullptr) {
    LinkToObject(owner->GetEffectNodeListHeadSlot());
  }

  ++sequence_counter_;
  flags_ = listen_distance;
}

void CMissileNode_C::AttachSpellVisualEffectToUnit(
    const std::uint32_t spell_id,
    const float duration_seconds,
    const std::uint32_t timestamp_start,
    const std::uint32_t timestamp_end,
    const std::uint8_t* const guid_data,
    const std::uint32_t extra_flags) {
  if (guid_data == nullptr) {
    return;
  }

  const auto owner_guid = ObjectGuid(
      static_cast<std::uint64_t>(*reinterpret_cast<const std::uint32_t*>(guid_data)) |
      (static_cast<std::uint64_t>(
           *reinterpret_cast<const std::uint32_t*>(guid_data + 4))
       << 32u));
  auto* const owner = object_manager().GetMutableUnit(owner_guid);
  if (owner == nullptr) {
    return;
  }

  attached_unit_visual_resets_.clear();
  LinkToObject(owner->GetEffectNodeListHeadSlot());
  ++sequence_counter_;

  AttachedUnitVisualReset owner_restore{};
  owner_restore.unit_guid = owner_guid;
  auto& m2_system = this->m2_system();
  const auto owner_primary_visual =
      m2_system.EnablePrimaryVisualState(owner->Presentation().PrimarySpellVisualModelInstanceId(),
                                         duration_seconds);
  const auto owner_secondary_visual =
      m2_system.EnablePrimaryVisualState(owner->Presentation().SecondarySpellVisualModelInstanceId(),
                                         duration_seconds);
  const bool owner_primary_visual_ready =
      owner_primary_visual.status == render::m2::M2ResultStatus::kReady;
  const bool owner_secondary_visual_ready =
      owner_secondary_visual.status == render::m2::M2ResultStatus::kReady;
  owner_restore.reset_primary_visual_state = owner_primary_visual.reset_on_detach;
  owner_restore.reset_secondary_visual_state = owner_secondary_visual.reset_on_detach;
  if (!owner_primary_visual_ready) {
    owner_restore.reset_primary_visual_state = true;
  }
  if (!owner_secondary_visual_ready) {
    owner_restore.reset_secondary_visual_state = true;
  }
  if (owner_restore.reset_primary_visual_state ||
      owner_restore.reset_secondary_visual_state) {
    attached_unit_visual_resets_.push_back(owner_restore);
  }

  object_manager().ForEachUnit(
      [this, owner, duration_seconds](const ObjectGuid& guid, CGUnit_C& candidate) {
        if (&candidate == owner || !IsDirectVehiclePassengerOf(candidate, *owner)) {
          return;
        }

        AttachedUnitVisualReset restore_entry{};
        restore_entry.unit_guid = guid;
        const auto visual =
            this->m2_system().EnablePrimaryVisualState(
                candidate.Presentation().PrimarySpellVisualModelInstanceId(), duration_seconds);
        const bool visual_ready = visual.status == render::m2::M2ResultStatus::kReady;
        restore_entry.reset_primary_visual_state = visual.reset_on_detach;
        if (!visual_ready) {
          restore_entry.reset_primary_visual_state = true;
        }
        if (restore_entry.reset_primary_visual_state) {
          attached_unit_visual_resets_.push_back(restore_entry);
        }
      });

  timestamp_end_ = timestamp_end;
  spell_id_ = spell_id;
  timestamp_start_ = timestamp_start;
  source_guid_low_ = static_cast<std::uint32_t>(owner_guid.GetRawValue() & 0xFFFFFFFFu);
  source_guid_high_ = static_cast<std::uint32_t>(owner_guid.GetRawValue() >> 32u);
  flags_ |= extra_flags | 0x4u;
}

void CMissileNode_C::SetupFromObjectWithItem(
    std::uint32_t spell_id,
    std::uint32_t item_entry,
    std::uint32_t timestamp_start,
    std::uint32_t timestamp_end,
    const std::uint8_t* guid_data,
    std::uint32_t extra_flags) {
  if (!guid_data) return;

  ReleaseObjectItemVisualState();

  spell_id_ = spell_id;
  timestamp_start_ = timestamp_start;
  timestamp_end_ = timestamp_end;

  source_guid_low_ = *reinterpret_cast<const std::uint32_t*>(guid_data);
  source_guid_high_ = *reinterpret_cast<const std::uint32_t*>(guid_data + 4);

  flags_ |= extra_flags | kObjectItemVisualFlag;
  item_template_entry_id_ = item_entry;
  item_display_id_ = 0;
  object_item_visual_slot_.reset();
  object_item_visual_applied_to_owner_ = false;

  if (auto* owner = ResolveMissileSourceUnit(*this); owner != nullptr) {
    LinkToObject(owner->GetEffectNodeListHeadSlot());
  }

  ++sequence_counter_;

  QueryCache::QueryRequestOptions request_options{
      .context = GetSourceGuid(),
      .callback_key =
          QueryCache::CallbackKey{
              reinterpret_cast<std::uintptr_t>(this),
              item_template_entry_id_},
      .dedupe_callbacks = true,
      .callback = [this, item_entry](const bool success) {
        if (!success || item_template_entry_id_ != item_entry) {
          return;
        }

        const auto* item_template =
            object_manager().query_cache().GetItemTemplate(item_entry);
        if (item_template == nullptr) {
          return;
        }

        HandleResolvedObjectItemTemplate(*item_template);
      },
  };

  if (const auto* item_template =
          object_manager().query_cache().GetOrRequestItemTemplate(
              item_entry, request_options);
      item_template != nullptr) {
    HandleResolvedObjectItemTemplate(*item_template);
  }
}

void CMissileNode_C::SetupFromSpellEffect(
    std::uint32_t spell_id,
    std::uint32_t secondary_value,
    const std::uint32_t* guid_data) {
  if (!guid_data) return;

  CancelPendingCreatureTemplateLookup();

  source_guid_low_ = guid_data[0];
  source_guid_high_ = guid_data[1];
  timestamp_start_ = spell_id;
  timestamp_end_ = secondary_value;
  move_event_type_ = 19;

  auto* owner = ResolveMissileSourceUnit(*this);
  if (owner == nullptr) {
    return;
  }

  creature_template_entry_id_ =
      FindSpellEffectCreatureEntry(object_manager().dbc_loader(), spell_id);
  creature_display_id_ = 0;

  if (creature_template_entry_id_ != 0) {
    const QueryCache::QueryRequestOptions request_options{
        .context = spell_id,
        .callback_key =
            QueryCache::CallbackKey{
                reinterpret_cast<std::uintptr_t>(this),
                creature_template_entry_id_},
        .dedupe_callbacks = true,
        .callback = [this, creature_entry = creature_template_entry_id_](const bool success) {
          HandlePendingSpellEffectCreatureTemplateLookup(
              this, creature_entry, success);
        },
    };

    if (const auto* creature_template =
            object_manager().query_cache().GetOrRequestCreatureTemplate(
                creature_template_entry_id_, request_options);
        creature_template != nullptr) {
      creature_display_id_ = creature_template->display_ids[0];
      creature_template_entry_id_ = 0;
    }
  }

  const bool ready_to_attach =
      (creature_display_id_ != 0 && CreatePrimaryModelFromCreatureDisplay()) ||
      creature_template_entry_id_ != 0;
  if (!ready_to_attach) {
    return;
  }

  LinkToObject(owner->GetEffectNodeListHeadSlot());
  ++sequence_counter_;
  mount_transition_handle_ = MountTransitionObject_CreateForUnit(owner);
  owner->Mount().SetTransitionData(mount_transition_handle_, this);
  if (!RefreshPrimaryModelPlacement()) {
    DestroyPrimaryModelInstance();
  }
}

void CMissileNode_C::HandleResolvedObjectItemTemplate(
    const ItemTemplate& item_template) {
  item_display_id_ = item_template.display_id;
  object_item_visual_slot_ = ResolveObjectItemVisualSlot(
      static_cast<std::uint32_t>(item_template.inventory_type));
  item_template_entry_id_ = 0;
  ApplyObjectItemVisualToOwner();
}

void CMissileNode_C::ApplyObjectItemVisualToOwner() {
  if ((flags_ & kObjectItemVisualFlag) == 0 || item_display_id_ == 0 ||
      !object_item_visual_slot_.has_value()) {
    return;
  }

  auto* owner = ResolveMissileSourceUnit(*this);
  if (owner == nullptr) {
    return;
  }

  object_item_visual_applied_to_owner_ = false;
  if (!ShouldApplyObjectItemVisualToOwner(*owner, *object_item_visual_slot_)) {
    return;
  }

  owner->Presentation().SetTransientEquipmentDisplayOverride(*object_item_visual_slot_,
                                              item_display_id_);
  object_item_visual_applied_to_owner_ = true;
}

void CMissileNode_C::RefreshObjectItemVisualForOwnerState() {
  if ((flags_ & kObjectItemVisualFlag) == 0 || item_display_id_ == 0 ||
      !object_item_visual_slot_.has_value()) {
    return;
  }

  auto* const owner = ResolveMissileSourceUnit(*this);
  if (owner == nullptr) {
    return;
  }

  if (object_item_visual_applied_to_owner_) {
    owner->Presentation().ClearTransientEquipmentDisplayOverride(*object_item_visual_slot_);
    object_item_visual_applied_to_owner_ = false;
  }
  ApplyObjectItemVisualToOwner();
}

void CMissileNode_C::ClearObjectItemVisualFromOwner() {
  if (!object_item_visual_slot_.has_value()) {
    return;
  }

  auto* owner = ResolveMissileSourceUnit(*this);
  if (object_item_visual_applied_to_owner_ && owner != nullptr) {
    owner->Presentation().ClearTransientEquipmentDisplayOverride(*object_item_visual_slot_);
  }

  object_item_visual_applied_to_owner_ = false;
  object_item_visual_slot_.reset();
  item_display_id_ = 0;
}

void CMissileNode_C::CancelPendingCreatureTemplateLookup() {
  if (creature_template_entry_id_ == 0) {
    return;
  }

  object_manager().query_cache().CancelCreatureTemplateCallback(
      creature_template_entry_id_,
      QueryCache::CallbackKey{reinterpret_cast<std::uintptr_t>(this),
                              creature_template_entry_id_});

  creature_template_entry_id_ = 0;
}

void CMissileNode_C::CancelPendingObjectItemTemplateLookup() {
  if (item_template_entry_id_ == 0) {
    return;
  }

  object_manager().query_cache().CancelItemTemplateCallback(
      item_template_entry_id_,
      QueryCache::CallbackKey{
          reinterpret_cast<std::uintptr_t>(this), item_template_entry_id_});

  item_template_entry_id_ = 0;
}

void CMissileNode_C::ReleaseMountTransitionState() {
  if (!mount_transition_handle_.IsValid()) {
    return;
  }

  auto* const owner = ResolveMissileSourceUnit(*this);
  const bool transition_complete =
      MountTransitionObject_IsTransitionComplete(mount_transition_handle_);
  if (owner != nullptr) {
    if (transition_complete) {
      owner->Mount().CompleteTransition(*owner, session());
      owner->Animation().UpdateMountAndPassengerAnimations();
    } else {
      owner->Mount().ClearTransitionData();
    }
    owner->Animation().RefreshSpellVisualStandAnimationState(session());
  }

  MountTransitionObject_Release(mount_transition_handle_);
  mount_transition_handle_ = {};
}

void CMissileNode_C::ReleaseObjectItemVisualState() {
  if ((flags_ & kObjectItemVisualFlag) == 0) {
    return;
  }

  ClearObjectItemVisualFromOwner();
  CancelPendingObjectItemTemplateLookup();
  flags_ &= ~kObjectItemVisualFlag;
}

void CMissileNode_C::RestoreAttachedUnitVisualState() {
  if ((flags_ & 0x4u) == 0u) {
    return;
  }

  for (const auto& restore_entry : attached_unit_visual_resets_) {
    auto* const unit = object_manager().GetMutableUnit(restore_entry.unit_guid);
    if (unit == nullptr) {
      continue;
    }

    auto& m2_system = this->m2_system();
    if (restore_entry.reset_primary_visual_state) {
      const auto status = m2_system.DisablePrimaryVisualState(
          unit->Presentation().PrimarySpellVisualModelInstanceId(), true);
      if (render::m2::IsTerminalM2ResultStatus(status)) {
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                           "CMissileNode_C: failed to restore primary visual state for unit " +
                               std::to_string(unit->GetGuid().GetRawValue()));
      }
    }
    if (restore_entry.reset_secondary_visual_state) {
      const auto status = m2_system.DisablePrimaryVisualState(
          unit->Presentation().SecondarySpellVisualModelInstanceId(), true);
      if (render::m2::IsTerminalM2ResultStatus(status)) {
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                           "CMissileNode_C: failed to restore secondary visual state for unit " +
                               std::to_string(unit->GetGuid().GetRawValue()));
      }
    }

    if (restore_entry.reset_primary_visual_state ||
        restore_entry.reset_secondary_visual_state) {
      unit->Animation().RefreshSpellVisualStandAnimationState(session());
    }
  }

  attached_unit_visual_resets_.clear();
}

bool CMissileNode_C::CreatePrimaryModelFromCreatureDisplay() {
  return CreatePrimaryModelFromCreatureDisplay(creature_display_id_);
}

bool CMissileNode_C::CreatePrimaryModelFromCreatureDisplay(
    const std::uint32_t creature_display_id) {
  creature_display_id_ = creature_display_id;
  creature_template_entry_id_ = 0;
  if (creature_display_id_ == 0) {
    return false;
  }

  const auto* display_info = DisplayInfoResolver::Get().GetDisplayInfo(
      creature_display_id_);
  if (display_info == nullptr || display_info->model_path.empty()) {
    return false;
  }

  if (!CreatePrimaryModelInstanceFromPath(
          display_info->model_path,
          display_info->model_scale > 0.0f ? display_info->model_scale : 1.0f,
          [this](const std::uint32_t animation_id) {
            HandlePrimaryModelAnimation(animation_id);
          })) {
    return false;
  }

  auto& renderer = m2_system();
  const auto overrides =
      DisplayInfoResolver::Get().ResolveCreatureModelOverrides(
          creature_display_id_);
  std::optional<render::m2::M2ParticleColorRecord> particle_colors;
  if (overrides.has_particle_colors) {
    particle_colors = ToRenderParticleColors(overrides.particle_colors);
  }

  const auto override_status = renderer.ApplyCreatureDisplayRecordOverrides(
      primary_model_instance_id_, overrides.texture_paths, particle_colors);
  const auto animation_status =
      override_status == render::m2::M2ResultStatus::kReady
          ? renderer.SetAnimation(primary_model_instance_id_, kMountTransitionAnimationId)
          : override_status;
  if (animation_status != render::m2::M2ResultStatus::kReady) {
    DestroyPrimaryModelInstance();
    return false;
  }
  if (!RefreshPrimaryModelPlacement()) {
    DestroyPrimaryModelInstance();
    return false;
  }
  return true;
}

bool CMissileNode_C::SetupWorldSpellVisualPrimaryModel(
    const data::dbc::SpellVisualEffectNameEntry& effect,
    const float* world_position,
    const std::uint32_t flags,
    const std::uint64_t follow_guid) {
  source_guid_low_ = 0;
  source_guid_high_ = 0;
  triggered_event_guid_low_ = 0;
  triggered_event_guid_high_ = 0;
  move_event_type_ = std::numeric_limits<std::uint32_t>::max();
  follow_guid_low_ = static_cast<std::uint32_t>(follow_guid & 0xFFFFFFFFu);
  follow_guid_high_ = static_cast<std::uint32_t>(follow_guid >> 32u);
  flags_ = flags | 0x2u;

  if (world_position != nullptr) {
    impact_pos_[0] = world_position[0];
    impact_pos_[1] = world_position[1];
    impact_pos_[2] = world_position[2];
  }

  if (!CreatePrimaryModelInstanceFromPath(std::string(effect.file_path),
                                          1.0f,
                                          {})) {
    return false;
  }

  ++sequence_counter_;
  LinkToObject(GetActiveWorldSpellVisualEffectListHeadSlot());

  const auto animation_status =
      m2_system().SetAnimation(primary_model_instance_id_, 0u);
  if (animation_status != render::m2::M2ResultStatus::kReady) {
    DestroyPrimaryModelInstance();
    return false;
  }
  if (!RefreshPrimaryModelPlacement()) {
    DestroyPrimaryModelInstance();
    return false;
  }
  return true;
}

void CMissileNode_C::HandlePrimaryModelAnimation(
    const std::uint32_t animation_id) {
  if (animation_id != kMountTransitionAnimationId) {
    return;
  }

  MountTransitionObject_MarkTransitionComplete(mount_transition_handle_);
}

void CMissileNode_C::ResetPrimaryModelForLightningInit() {
  primary_model_visual_state_ = 0;
  const auto reset_status = m2_system().ResetPrimaryVisualStateForLightning(
      primary_model_instance_id_);
  if (render::m2::IsTerminalM2ResultStatus(reset_status)) {
    primary_model_instance_id_ = 0;
    primary_model_scale_ = 1.0f;
  }
}

void CMissileNode_C::DestroyPrimaryModelInstance() {
  if (primary_model_instance_id_ == 0) {
    return;
  }

  auto& renderer = m2_system();
  render::m2::M2ResultStatus cleanup_status = render::m2::M2ResultStatus::kReady;
  cleanup_status = render::m2::MergeM2ResultStatus(
      cleanup_status, renderer.ClearAnimationChangedCallback(primary_model_instance_id_));
  cleanup_status = render::m2::MergeM2ResultStatus(
      cleanup_status, renderer.ClearTriggeredEventCallback(primary_model_instance_id_));
  cleanup_status = render::m2::MergeM2ResultStatus(
      cleanup_status, renderer.DestroyInstance(primary_model_instance_id_));
  if (render::m2::IsTerminalM2ResultStatus(cleanup_status)) {
    primary_model_visual_state_ = 0;
  }
  primary_model_instance_id_ = 0;
  primary_model_scale_ = 1.0f;
}

bool CMissileNode_C::SetPrimaryModelWorldTransform(
    const render::RenderMatrix4x4& matrix) {
  if (primary_model_instance_id_ == 0) {
    return false;
  }

  auto& renderer = m2_system();
  if (renderer.QueryInstanceReadiness(primary_model_instance_id_).status !=
      render::m2::M2ResultStatus::kReady) {
    primary_model_instance_id_ = 0;
    primary_model_scale_ = 1.0f;
    return false;
  }

  return renderer.SetWorldTransformMatrix(primary_model_instance_id_, matrix) ==
         render::m2::M2ResultStatus::kReady;
}

void CMissileNode_C::ClearPrimaryModelWorldTransform() {
  if (primary_model_instance_id_ == 0) {
    return;
  }

  const auto clear_status = m2_system().ClearWorldTransformMatrix(
      primary_model_instance_id_);
  if (render::m2::IsTerminalM2ResultStatus(clear_status)) {
    primary_model_instance_id_ = 0;
    primary_model_scale_ = 1.0f;
  }
}

void CMissileNode_C::SetPrimaryModelAlpha(const float alpha) {
  model_alpha_ = std::clamp(alpha, 0.0f, 1.0f);
  if (primary_model_instance_id_ == 0) {
    return;
  }

  const auto alpha_status = m2_system().SetAlpha(
      primary_model_instance_id_, model_alpha_);
  if (render::m2::IsTerminalM2ResultStatus(alpha_status)) {
    primary_model_instance_id_ = 0;
    primary_model_scale_ = 1.0f;
  }
}

bool CMissileNode_C::CreatePrimaryModelInstanceFromPath(
    const std::string& model_path,
    const float model_scale,
    std::function<void(std::uint32_t)> animation_callback) {
  DestroyPrimaryModelInstance();

  auto& renderer = m2_system();
  if (model_path.empty()) {
    return false;
  }

  const auto instance_result = renderer.LoadModelInstance(model_path);
  if (instance_result.status != render::m2::M2ResultStatus::kReady ||
      instance_result.instance_id == 0u) {
    LogMissileM2Failure(model_path, instance_result);
    return false;
  }

  const std::uint32_t instance_id = instance_result.instance_id;
  primary_model_instance_id_ = instance_id;
  primary_model_scale_ = model_scale > 0.0f ? model_scale : 1.0f;

  if (renderer.SetAnimationChangedCallback(
      instance_id,
      [callback = std::move(animation_callback)](
          const std::uint32_t animation_id) {
        if (callback) {
          callback(animation_id);
        }
      }) != render::m2::M2ResultStatus::kReady) {
    DestroyPrimaryModelInstance();
    return false;
  }
  if (renderer.SetTriggeredEventCallback(
      instance_id,
      [this](const render::m2::M2TriggeredEvent& event) {
        auto* const event_object = ResolveTriggeredEventObject(*this);
        auto* const event_unit =
            dynamic_cast<CGUnit_C*>(event_object);
        if (event_unit == nullptr) {
          return;
        }
        (void)DispatchSpellVisualM2Event(
            event,
            {.sound = [this, event_object](const auto& sound_event) {
               PlayImpactSound(sound_event.data, 0, flags_,
                               sound_event.world_position.data(), event_object,
                               0);
             },
             .hit = [this, event_unit] {
               RefreshSpellVisualM2HitReaction(*event_unit, session());
             }});
      }) != render::m2::M2ResultStatus::kReady) {
    DestroyPrimaryModelInstance();
    return false;
  }
  if (!RefreshPrimaryModelPlacement()) {
    DestroyPrimaryModelInstance();
    return false;
  }
  return true;
}

bool CMissileNode_C::RefreshPrimaryModelPlacement() {
  if (primary_model_instance_id_ == 0) {
    return false;
  }

  auto& renderer = m2_system();
  if (renderer.QueryInstanceReadiness(primary_model_instance_id_).status !=
      render::m2::M2ResultStatus::kReady) {
    primary_model_instance_id_ = 0;
    primary_model_scale_ = 1.0f;
    return false;
  }

  render::m2::M2ResultStatus result = render::m2::M2ResultStatus::kReady;
  result = render::m2::MergeM2ResultStatus(
      result, renderer.SetScale(primary_model_instance_id_, primary_model_scale_));
  result = render::m2::MergeM2ResultStatus(
      result, renderer.ClearWorldTransformMatrix(primary_model_instance_id_));

  const auto* owner = !GetAttachmentOwnerGuid().IsEmpty()
                          ? object_manager().GetUnit(GetAttachmentOwnerGuid())
                          : ResolveMissileSourceUnit(*this);
  render::RenderVec3 position{impact_pos_[0], impact_pos_[1], impact_pos_[2]};
  render::RenderVec3 rotation_degrees{0.0f, 0.0f, 0.0f};
  if (owner == nullptr) {
    result = render::m2::MergeM2ResultStatus(
        result, renderer.SetPosition(primary_model_instance_id_, position));
    result = render::m2::MergeM2ResultStatus(
        result, renderer.SetRotationDegrees(primary_model_instance_id_, rotation_degrees));
    return result == render::m2::M2ResultStatus::kReady;
  }

  const auto owner_position = owner->GetPosition();
  position[0] = owner_position.x;
  position[1] = owner_position.y;
  position[2] = owner_position.z;
  rotation_degrees[1] = owner_position.facing;
  result = render::m2::MergeM2ResultStatus(
      result, renderer.SetPosition(primary_model_instance_id_, position));
  result = render::m2::MergeM2ResultStatus(
      result, renderer.SetRotationDegrees(primary_model_instance_id_, rotation_degrees));
  return result == render::m2::M2ResultStatus::kReady;
}

}


#include "openwow/game/object_manager.h"
#include "openwow/game/player_control_runtime.h"

#include "openwow/runtime/time/game_clock.h"
#include "openwow/core/client_misc.h"
#include "openwow/game/battlefield_info.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/objects/cgobject.h"
#include "openwow/game/objects/cggameobject.h"
#include "openwow/game/objects/cgitem.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/spell_c_internals.h"
#include "openwow/game/spellbook_system.h"
#include "openwow/game/transport_manager.h"
#include "openwow/game/unit_sound_dispatch.h"
#include "openwow/game/vehicle_passenger.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/game_ui_core.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/world/movement/movement_spline.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <numeric>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace openwow::game {

namespace {

constexpr std::uint32_t kPendingObjectReapDelayMs = 10000;

constexpr std::uint32_t kStalePendingObjectTimeoutMs = 120000;

bool MatchesRequiredTypeMask(const WorldObject *object, const std::uint32_t required_type_mask) {
  return object != nullptr && required_type_mask != 0 &&
         (required_type_mask & object->GetTypeMask()) != 0;
}

WorldObject *ResolveObjectByGuidAndTypeMask(ObjectManager &manager, const ObjectGuid guid,
                                            const std::uint32_t required_type_mask) {
  if (guid.IsEmpty()) {
    return nullptr;
  }

  WorldObject *object = manager.GetMutable(guid);
  if (!MatchesRequiredTypeMask(object, required_type_mask)) {
    return nullptr;
  }

  return object;
}

const WorldObject *ResolveObjectByGuidAndTypeMask(const ObjectManager &manager,
                                                  const ObjectGuid guid,
                                                  const std::uint32_t required_type_mask) {
  if (guid.IsEmpty()) {
    return nullptr;
  }

  const WorldObject *object = manager.Get(guid);
  if (!MatchesRequiredTypeMask(object, required_type_mask)) {
    return nullptr;
  }

  return object;
}

bool ShouldRunDestroyPacketDeathCleanup(const CGObject_C &object, const bool destroy_packet_flag) {
  return destroy_packet_flag && object.IsUnit() && object.GetHealth() > 0;
}

bool ShouldDeferUnitClickTarget(
    const WorldSession& session,
    const CGUnit_C& unit,
    const ObjectManager& objects,
    const std::uint32_t current_tick_ms) {
  if (unit.State().IsNotSelectable()) {
    return true;
  }
  if (unit.GetTypeId() != TypeID::kUnit || unit.State().GetHealth() > 0u) {
    return false;
  }

  if (unit.State().IsLootableCorpseAt(current_tick_ms)) {
    if (const auto* const active_player = objects.GetActivePlayer();
        active_player != nullptr) {
      float interaction_range_squared = 0.0f;
      if (active_player->Interaction().GetInteractionRangeSquared(
              session, unit.GetGuid(), 3, &interaction_range_squared)) {
        const float distance = active_player->GetDistance(unit);
        if (distance * distance <= interaction_range_squared) {
          return false;
        }
      }
    }
  }

  constexpr std::uint32_t kUnitFlagSkinnable = 0x04000000u;
  if ((unit.State().GetUnitFlags() & kUnitFlagSkinnable) != 0u) {
    const auto gather_spell_id =
        SpellbookSystem::Get().ResolveGatherInteractionSpellId(
            unit, &objects.query_cache());
    if (gather_spell_id != 0u) {
      return false;
    }
  }

  return true;
}

void RunDestroyPacketDeathCleanup(CGObject_C &object, const bool enabled) {
  if (!enabled || !object.IsUnit()) {
    return;
  }

  static_cast<CGUnit_C &>(object).Interaction().CompleteAutoAttackInteraction(
      false, true);
}

std::uint32_t CurrentPendingObjectTick() {
  return openwow::core::GameClock::GetTickCount32();
}

void ClearObjectScopedDescriptorCallbacks(const ObjectGuid guid) {
  if (!guid) {
    return;
  }

  DescriptorCallbackRegistry::Get().UnregisterObjectCallbacks(guid);
}

FieldUpdateBatch BuildFieldUpdateBatch(const CGObject_C *object,
                                       const UpdateFieldValues &field_data, bool is_create) {
  FieldUpdateBatch batch;
  ForEachAppliedUpdateField(field_data, [&](std::uint16_t field_index, std::uint32_t value_index) {
    if (value_index >= field_data.values.size()) {
      return;
    }

    const std::uint32_t new_value = field_data.values[value_index];
    batch.updated_fields.push_back(field_index);

    const std::uint32_t old_value =
        is_create || object == nullptr ? 0 : object->GetUInt32(field_index);
    if (old_value != new_value) {
      batch.value_changes.push_back(
          {.field_index = field_index, .old_value = old_value, .new_value = new_value});
    }
  });

  return batch;
}

void FinalizePacketUpdatePromotion(ObjectManagerCallbacks &callbacks,
                                   CGObject_C &object) {
  object.FinalizePacketUpdatePromotion();
  if (callbacks.on_object_packet_promoted) {
    callbacks.on_object_packet_promoted(object);
  }
}

UpdateFieldValues ExpandExistingCreateFields(const UpdateFieldValues &wire_fields) {
  UpdateFieldValues expanded;
  expanded.field_count = wire_fields.field_count;
  expanded.bitmask.assign((expanded.field_count + 31u) / 32u, 0xFFFFFFFFu);
  if (!expanded.bitmask.empty() && (expanded.field_count % 32u) != 0u) {
    expanded.bitmask.back() =
        (1u << (expanded.field_count % 32u)) - 1u;
  }
  expanded.values.assign(expanded.field_count, 0u);

  ForEachAppliedUpdateField(
      wire_fields, [&](const std::uint16_t field_index,
                       const std::uint32_t value_index) {
        if (field_index < expanded.values.size() &&
            value_index < wire_fields.values.size()) {
          expanded.values[field_index] = wire_fields.values[value_index];
        }
      });
  return expanded;
}

struct TransportAttachmentState {
  ObjectGuid transport_guid;
  float offset_x{0.0f};
  float offset_y{0.0f};
  float offset_z{0.0f};
  float offset_o{0.0f};
};

std::optional<TransportAttachmentState> ReadTransportAttachment(const CGObject_C &object) {
  const auto &movement_update = object.GetMovementUpdate();
  if (movement_update.IsLiving() && !movement_update.movement.transport.guid.IsEmpty()) {
    return TransportAttachmentState{
        .transport_guid = movement_update.movement.transport.guid,
        .offset_x = movement_update.movement.transport.offset_x,
        .offset_y = movement_update.movement.transport.offset_y,
        .offset_z = movement_update.movement.transport.offset_z,
        .offset_o = movement_update.movement.transport.offset_o,
    };
  }

  if (movement_update.HasUpdateFlag(kUpdateFlagPosition) &&
      !movement_update.transport_guid.IsEmpty()) {
    return TransportAttachmentState{
        .transport_guid = movement_update.transport_guid,
        .offset_x = movement_update.transport_offset_x,
        .offset_y = movement_update.transport_offset_y,
        .offset_z = movement_update.transport_offset_z,
        .offset_o = movement_update.position_o,
    };
  }

  return std::nullopt;
}

TransportManager *GetActiveTransportManager(ObjectManager& objects) {
  return &objects.transport_manager();
}

void RemoveTransportPassenger(ObjectManager& objects, TransportManager *transport_manager,
                              const ObjectGuid transport_guid,
                              const ObjectGuid passenger_guid) {
  if (transport_guid.IsEmpty()) {
    return;
  }

  if (auto *transport_object = objects.GetMutable(transport_guid);
      transport_object != nullptr && transport_object->IsGameObject()) {
    static_cast<CGGameObject_C *>(transport_object)
        ->RemoveTransportPassengerAttachment(passenger_guid.GetRawValue());
  }

  if (transport_manager == nullptr) {
    return;
  }

  if (auto *transport = transport_manager->GetTransportMutable(transport_guid);
      transport != nullptr) {
    transport->RemovePassenger(passenger_guid);
  }
}

void AddTransportPassenger(ObjectManager& objects, TransportManager *transport_manager,
                           const ObjectGuid passenger_guid,
                           const TransportAttachmentState &attachment) {
  if (attachment.transport_guid.IsEmpty()) {
    return;
  }

  if (auto *transport_object = objects.GetMutable(attachment.transport_guid);
      transport_object != nullptr && transport_object->IsGameObject()) {
    static_cast<CGGameObject_C *>(transport_object)
        ->UpsertTransportPassengerAttachment(passenger_guid.GetRawValue());
  }

  if (transport_manager == nullptr) {
    return;
  }

  if (auto *transport = transport_manager->GetTransportMutable(attachment.transport_guid);
      transport != nullptr) {
    transport->AddPassenger(passenger_guid, attachment.offset_x, attachment.offset_y,
                            attachment.offset_z, attachment.offset_o);
  }
}

void SyncTransportPassengerMembership(
    ObjectManager& objects, const CGObject_C &object,
    const std::optional<TransportAttachmentState> &previous_attachment) {
  TransportManager *transport_manager = GetActiveTransportManager(objects);

  const auto current_attachment = ReadTransportAttachment(object);
  const ObjectGuid passenger_guid = object.GetGuid();

  if (previous_attachment.has_value() &&
      (!current_attachment.has_value() ||
       current_attachment->transport_guid != previous_attachment->transport_guid)) {
    RemoveTransportPassenger(objects, transport_manager,
                             previous_attachment->transport_guid,
                             passenger_guid);
  }

  if (!current_attachment.has_value()) {
    return;
  }

  AddTransportPassenger(objects, transport_manager, passenger_guid, *current_attachment);
}

void ClearTransportPassengerMembership(ObjectManager& objects,
                                       const CGObject_C &object) {
  TransportManager *transport_manager = GetActiveTransportManager(objects);

  if (const auto attachment = ReadTransportAttachment(object); attachment.has_value()) {
    RemoveTransportPassenger(objects, transport_manager, attachment->transport_guid,
                             object.GetGuid());
  }
}

void NotifyTransportPassengersIfDespawningTransport(
    ObjectManager& objects, CGObject_C &object,
    const ObjectManagerCallbacks &callbacks) {
  if (!object.IsGameObject() ||
      (!callbacks.on_transport_attachment_destroyed &&
       !callbacks.on_transport_passenger_destroyed &&
       !callbacks.on_local_player_transport_destroyed)) {
    return;
  }
  auto &game_object = static_cast<CGGameObject_C &>(object);
  if (!game_object.IsAnyTransport()) {
    return;
  }
  game_object.BeginTransportAttachmentDestruction();

  struct AttachmentTarget {
    std::uint64_t guid = 0u;
    bool movement_attachment = false;
    GameObjectAttachmentNode *node = nullptr;
  };
  std::vector<AttachmentTarget> attachment_targets;
  const auto &attachments = game_object.GetActiveAttachments();
  attachment_targets.reserve(attachments.size());

  for (auto *const node : attachments) {
    if (node == nullptr || node->target_guid == 0u) {
      continue;
    }
    const bool movement_attachment = node->HasAutoPlayParticle();
    attachment_targets.push_back(
        {node->target_guid, movement_attachment, node});
  }

  if (callbacks.on_transport_attachment_destroyed) {
    for (const auto &attachment : attachment_targets) {
      if (attachment.movement_attachment && attachment.node != nullptr) {

        attachment.node->ClearParticleStopFlag();
      }
      callbacks.on_transport_attachment_destroyed(
          game_object.GetGuid(), ObjectGuid(attachment.guid),
          attachment.movement_attachment, attachment.node);
    }
    return;
  }

  if (callbacks.on_transport_passenger_destroyed) {
    for (const auto &attachment : attachment_targets) {
      if (attachment.movement_attachment) {
        if (attachment.node != nullptr) {
          attachment.node->ClearParticleStopFlag();
        }
        callbacks.on_transport_passenger_destroyed(
            game_object.GetGuid(), ObjectGuid(attachment.guid));
      }
    }
    return;
  }

  const ObjectGuid active_player_guid = objects.GetActivePlayerGuid();
  if (!active_player_guid.IsEmpty() &&
      std::any_of(attachment_targets.begin(), attachment_targets.end(),
                  [&active_player_guid](const AttachmentTarget &attachment) {
                    return attachment.movement_attachment &&
                           attachment.guid == active_player_guid.GetRawValue();
                  })) {
    callbacks.on_local_player_transport_destroyed(game_object.GetGuid());
  }
}

void ClearTransportRuntimeRegistration(ObjectManager& objects,
                                       const CGObject_C &object) {
  TransportManager *transport_manager = GetActiveTransportManager(objects);
  if (!object.IsGameObject()) {
    return;
  }

  const auto &game_object = static_cast<const CGGameObject_C &>(object);
  if (game_object.IsAnyTransport()) {
    transport_manager->OnTransportDestroy(game_object.GetGuid());
  }
}

}

ObjectManager::ObjectManager(
    PlayerInventoryReplica& inventory, PlayerControlRuntime& player_control,
    ItemDefinitions& item_definitions,
    openwow::render::m2::M2System& m2_system,
    const openwow::data::dbc::DbcLoader& dbc_loader,
    QueryCache& query_cache, TransportManager& transport_manager,
    openwow::audio::SoundRuntime& sound_runtime)
    : inventory_(inventory), player_control_(player_control),
      sound_runtime_(sound_runtime),
      item_definitions_(item_definitions),
      m2_system_(m2_system), dbc_loader_(dbc_loader),
      query_cache_(query_cache), transport_manager_(transport_manager) {
  presentation_state_.rehash(kPresentationStateBucketFloor);
}

WorldObject *CGObject_HasFlags(ObjectManager& objects,
                               const std::uint64_t guid_raw,
                               const std::uint32_t required_type_mask) {
  return ResolveObjectByGuidAndTypeMask(objects, ObjectGuid(guid_raw),
                                        required_type_mask);
}

const WorldObject *CGObject_HasFlags(
    const ObjectManager& objects,
    const std::uint64_t guid_raw,
    const std::uint32_t required_type_mask) {
  return ResolveObjectByGuidAndTypeMask(
      objects, ObjectGuid(guid_raw), required_type_mask);
}

bool Movement_C_IsGuidTransport(const ObjectManager& objects,
                                const std::uint64_t guid_raw) {
  auto *obj = CGObject_HasFlags(objects, guid_raw, kTypeMaskObject);
  if (obj == nullptr) {
    return false;
  }
  return obj->CanBeTransportParent();
}

const WorldObject *ObjectManager::Get(ObjectGuid guid) const {
  const auto it = objects_.find(guid);
  return it != objects_.end() ? it->second.get() : nullptr;
}

WorldObject *ObjectManager::GetMutable(ObjectGuid guid) {
  const auto it = objects_.find(guid);
  return it != objects_.end() ? it->second.get() : nullptr;
}

const WorldObject *ObjectManager::GetLocalPlayer() const {
  return Get(local_player_guid_);
}

void ObjectManager::ApplyMovementUpdate(const MovementOnlyUpdate &upd) {
  OnMovement(upd);
}

void ObjectManager::SynchronizeUnitTransportPassengerMembership(
    const CGUnit_C &unit, const MovementInfo &previous_movement) {
  std::optional<TransportAttachmentState> previous_attachment;
  if (previous_movement.IsOnTransport() &&
      !previous_movement.transport.guid.IsEmpty()) {
    previous_attachment = TransportAttachmentState{
        .transport_guid = previous_movement.transport.guid,
        .offset_x = previous_movement.transport.offset_x,
        .offset_y = previous_movement.transport.offset_y,
        .offset_z = previous_movement.transport.offset_z,
        .offset_o = previous_movement.transport.offset_o,
    };
  }
  SyncTransportPassengerMembership(*this, unit, previous_attachment);
}

void ObjectManager::AdvanceSplineMovement(
    world::MovementSplineManager &spline_manager) {
  ForEachUnit([&spline_manager, this](const ObjectGuid &guid, CGUnit_C &unit) {
    auto *const spline = spline_manager.GetSpline(guid.GetRawValue());
    if (spline == nullptr) {
      if (unit.Movement().HasSplineMovementPoseOwnership()) {
        (void)unit.Movement().TrySettleSplineMovementPoseOwnership();
      }
      return;
    }

    if ((spline->IsFinished() || spline->GetDuration() == 0u) &&
        spline->GetFacingMode() == world::SplineFacingMode::kTarget) {
      const auto *const target =
          Get(ObjectGuid(spline->GetFacingTarget()));
      std::optional<Vec3> target_position;
      if (target != nullptr) {
        const auto position = target->GetPosition();
        target_position = Vec3{position.x, position.y, position.z};
      }
      spline->ResolveArrivalTargetFacing(target_position);
    }

    Vec3 position = spline->GetCurrentPosition();
    float facing = spline->GetCurrentFacing();
    const auto velocity = spline->GetCurrentVelocity();
    const bool locomoting = spline->IsActive() &&
                            velocity.x * velocity.x + velocity.y * velocity.y >
                                0.0018490001f;
    unit.Movement().ApplySplineMovementPose(
        position, facing, locomoting,
        (spline->GetSplineFlags() & SplineFlag::kBackward) != 0u,
        spline->HasCoordinateParentBinding(), spline->GetCoordinateParent(),
        spline->GetCoordinateParentSeat(), spline->GetSplineFlags(),
        spline->GetTotalArcLength(), spline->GetDuration(),
        spline->IsActive());
    if (spline->HasTriggeredAnimationTier()) {
      unit.Animation().ApplySplineAnimationTier(spline->GetAnimationId());
    }
  });
}

void ObjectManager::AdvanceMovementEvents(WorldSession &session,
                                          const std::uint32_t current_tick_ms) {
  ForEachUnit([&session, current_tick_ms](const ObjectGuid &, CGUnit_C &unit) {
    unit.Movement().Update(session, current_tick_ms);
  });
}

void ObjectManager::AdvanceEmoteQueues() {

  ForEachUnit([](const ObjectGuid &, CGUnit_C &unit) {
    unit.Animation().EmoteSequencePlayer();
  });
}

void ObjectManager::AdvanceTransportPathStates() {
  const std::uint64_t frame_stamp =
      openwow::core::GameClock::Instance().FrameCount();
  EnumVisibleObjectsMutable([frame_stamp](WorldObject &object) {
    if (!object.IsGameObject()) {
      return;
    }
    auto &game_object = static_cast<CGGameObject_C &>(object);

    if (!game_object.IsMOTransport()) {
      return;
    }
    game_object.AdvanceMOTransportPathStateForFrame(frame_stamp);
  });
}

void ObjectManager::AdvanceVisualState(const std::uint32_t current_tick_ms,
                                       const float elapsed_seconds) {
  EnumVisibleObjectsMutable([current_tick_ms, elapsed_seconds](WorldObject &object) {
    object.AnimateOpacityTransition(current_tick_ms);
    if (object.IsUnit()) {
      auto &unit = static_cast<CGUnit_C &>(object);
      unit.UpdateSceneEnvironmentCache(current_tick_ms);

      unit.Movement().UpdateSmoothBodyFacing(elapsed_seconds);
      if (unit.Vehicle().GetVehiclePassengerComponent() == nullptr) {
        unit.Movement().UpdateBodyFacing(nullptr);
      }
    }
    object.UpdateModelNodeTransform(elapsed_seconds, current_tick_ms);
  });
}

bool ObjectManager::AcquireObjectLifetimeHold(const ObjectGuid &guid) {
  auto *object = GetMutable(guid);
  if (object == nullptr) {
    return false;
  }

  (void)static_cast<CGObject_C *>(object)->AdjustLifetimeHold(true);
  return true;
}

bool ObjectManager::ReleaseObjectLifetimeHold(const ObjectGuid &guid) {
  auto it = objects_.find(guid);
  if (it == objects_.end()) {
    return false;
  }

  CGObject_C &object = *it->second;
  const std::uint16_t remaining_holds = object.AdjustLifetimeHold(false);
  if (remaining_holds == 0 && object.IsPendingRemoval()) {
    return FinalizeActiveObjectServerRemoval(guid);
  }

  return true;
}

const CGUnit_C *ObjectManager::GetUnit(ObjectGuid guid) const {
  return static_cast<const CGUnit_C *>(
      ResolveObjectByGuidAndTypeMask(*this, guid, kTypeMaskUnit));
}

CGUnit_C *ObjectManager::GetMutableUnit(ObjectGuid guid) {
  return static_cast<CGUnit_C *>(
      ResolveObjectByGuidAndTypeMask(*this, guid, kTypeMaskUnit));
}

namespace {

void ApplySelectionGlowTransition(ObjectManager &objects, const ObjectGuid &old_guid,
                                  const ObjectGuid &new_guid,
                                  const std::uint8_t highlight_type) {
  if (old_guid == new_guid) {
    return;
  }
  if (auto *const gained = objects.GetMutableUnit(new_guid);
      gained != nullptr &&
      openwow::ui::game::GameUI_ShouldShowHighlightedNameplates()) {
    gained->Nameplate().Set(*gained, highlight_type);
  }
  if (auto *const lost = objects.GetMutableUnit(old_guid); lost != nullptr) {
    lost->Nameplate().Clear(*lost, highlight_type);
  }
}

}

void ObjectManager::SetTarget(const ObjectGuid &guid) {
  ApplySelectionGlowTransition(*this, target_, guid,
                               UnitNameplateComponent::kHighlightTypeTarget);
  target_ = guid;
}

void ObjectManager::SetMouseover(const ObjectGuid &guid) {
  ApplySelectionGlowTransition(*this, mouseover_, guid,
                               UnitNameplateComponent::kHighlightTypeMouseover);
  mouseover_ = guid;
}

const CGPlayer_C *ObjectManager::GetPlayer(ObjectGuid guid) const {
  return static_cast<const CGPlayer_C *>(
      ResolveObjectByGuidAndTypeMask(*this, guid, kTypeMaskPlayer));
}

CGPlayer_C *ObjectManager::GetMutablePlayer(ObjectGuid guid) {
  return static_cast<CGPlayer_C *>(
      ResolveObjectByGuidAndTypeMask(*this, guid, kTypeMaskPlayer));
}

const CGItem_C *ObjectManager::GetItem(ObjectGuid guid) const {
  return static_cast<const CGItem_C *>(
      ResolveObjectByGuidAndTypeMask(*this, guid, kTypeMaskItem));
}

const CGGameObject_C *ObjectManager::GetGameObject(ObjectGuid guid) const {
  return static_cast<const CGGameObject_C *>(
      ResolveObjectByGuidAndTypeMask(*this, guid, kTypeMaskGameObject));
}

CGGameObject_C *ObjectManager::GetMutableGameObject(ObjectGuid guid) {
  return static_cast<CGGameObject_C *>(
      ResolveObjectByGuidAndTypeMask(*this, guid, kTypeMaskGameObject));
}

const CGPlayer_C *ObjectManager::GetLocalPlayerTyped() const {
  return GetPlayer(local_player_guid_);
}

const CGDynamicObject_C *ObjectManager::GetDynamicObject(ObjectGuid guid) const {
  return static_cast<const CGDynamicObject_C *>(
      ResolveObjectByGuidAndTypeMask(*this, guid, kTypeMaskDynamicObject));
}

const CGCorpse_C *ObjectManager::GetCorpse(ObjectGuid guid) const {
  return static_cast<const CGCorpse_C *>(
      ResolveObjectByGuidAndTypeMask(*this, guid, kTypeMaskCorpse));
}

const CGContainer_C *ObjectManager::GetContainer(ObjectGuid guid) const {
  auto *obj = Get(guid);
  if (obj && obj->IsContainer())
    return static_cast<const CGContainer_C *>(obj);
  return nullptr;
}

CGUnit_C *ObjectManager::GetTarget() {
  return GetMutableUnit(target_);
}

const CGUnit_C *ObjectManager::GetTarget() const {
  return GetUnit(target_);
}

CGUnit_C *ObjectManager::GetFocusTarget() {
  return GetMutableUnit(focus_target_);
}

const CGUnit_C *ObjectManager::GetFocusTarget() const {
  return GetUnit(focus_target_);
}

CGPlayer_C *ObjectManager::GetActivePlayer() {
  return GetMutablePlayer(CGObject_C::GetActivePlayerGuid());
}

const CGPlayer_C *ObjectManager::GetActivePlayer() const {
  return GetPlayer(CGObject_C::GetActivePlayerGuid());
}

void ObjectManager::ForEachObject(const std::function<void(const ObjectGuid &, CGObject_C &)> &fn) {
  for (auto &[guid, obj] : objects_) {
    fn(guid, *obj);
  }
}

void ObjectManager::ForEachUnit(const std::function<void(const ObjectGuid &, CGUnit_C &)> &fn) {
  for (auto &[guid, obj] : objects_) {
    if (obj->IsUnit()) {
      fn(guid, static_cast<CGUnit_C &>(*obj));
    }
  }
}

void ObjectManager::ForEachPlayer(const std::function<void(const ObjectGuid &, CGPlayer_C &)> &fn) {
  for (auto &[guid, obj] : objects_) {
    if (obj->IsPlayer()) {
      fn(guid, static_cast<CGPlayer_C &>(*obj));
    }
  }
}

std::size_t ObjectManager::GetUnitCount() const {
  std::size_t count = 0;
  for (const auto &[guid, obj] : objects_) {
    if (obj->IsUnit())
      ++count;
  }
  return count;
}

std::size_t ObjectManager::GetPlayerCount() const {
  std::size_t count = 0;
  for (const auto &[guid, obj] : objects_) {
    if (obj->IsPlayer())
      ++count;
  }
  return count;
}

void ObjectManager::CachePlayerName(const ObjectGuid &guid, const std::string &name,
                                    std::uint8_t race, std::uint8_t gender, std::uint8_t cls) {
  name_cache_[guid.GetRawValue()] = {name, race, gender, cls};
}

bool ObjectManager::InvalidatePlayerName(const ObjectGuid &guid) {
  return name_cache_.erase(guid.GetRawValue()) != 0;
}

std::string ObjectManager::GetPlayerName(const ObjectGuid &guid) const {
  auto it = name_cache_.find(guid.GetRawValue());
  return it != name_cache_.end() ? it->second.name : std::string{};
}

const ObjectManager::NameCacheEntry *ObjectManager::GetNameEntry(const ObjectGuid &guid) const {
  auto it = name_cache_.find(guid.GetRawValue());
  return it != name_cache_.end() ? &it->second : nullptr;
}

std::optional<ObjectGuid> ObjectManager::FindPlayerGuidByName(const std::string_view name) const {
  if (name.empty()) {
    return std::nullopt;
  }

  const auto matches_name = [&](const std::string_view candidate) {
    return !candidate.empty() && openwow::text::EqualsIgnoreCaseAscii(candidate, name);
  };

  for (const auto &[guid, object] : objects_) {
    if (!object->IsPlayer()) {
      continue;
    }

    if (matches_name(object->GetName()) || matches_name(GetPlayerName(guid))) {
      return guid;
    }
  }

  for (const auto &[raw_guid, entry] : name_cache_) {
    if (matches_name(entry.name)) {
      return ObjectGuid(raw_guid);
    }
  }

  return std::nullopt;
}

void ObjectManager::CreateObject(const ObjectGuid &guid, TypeID type_id) {
  if (auto pending_it = pending_objects_.find(guid); pending_it != pending_objects_.end()) {
    const std::size_t pending_type = static_cast<std::size_t>(pending_it->second.type_id);
    if (pending_type < pending_by_type_.size()) {
      pending_by_type_[pending_type].erase(pending_it->second.type_order_it);
    }
    pending_objects_.erase(pending_it);
  }
  RemoveWorldPublication(guid);

  auto obj = openwow::game::CreateObject(
      guid, type_id, *this, inventory_, item_definitions_, dbc_loader_);
  obj->BindM2System(m2_system_);
  obj->BindWorldFrame(world_frame_);
  obj->BindWorldEnvironmentState(world_environment_);
  presentation_state_[guid].presentation_generation =
      next_object_presentation_generation_++;
  objects_[guid] = std::move(obj);
}

void ObjectManager::BindWorldFrame(openwow::render::WorldFrame* world_frame) {
  world_frame_ = world_frame;
  for (auto& [guid, object] : objects_) {
    object->BindWorldFrame(world_frame);
  }
  for (auto& [guid, pending] : pending_objects_) {
    pending.object->BindWorldFrame(world_frame);
  }
}

void ObjectManager::BindWorldEnvironmentState(WorldEnvironmentState* world_environment) {
  world_environment_ = world_environment;
  for (auto& [guid, object] : objects_) {
    object->BindWorldEnvironmentState(world_environment);
  }
  for (auto& [guid, pending] : pending_objects_) {
    pending.object->BindWorldEnvironmentState(world_environment);
  }
}

void ObjectManager::DestroyObject(const ObjectGuid &guid) {
  if (objects_.contains(guid)) {
    DestroyActiveObject(guid, false);
    return;
  }

  (void)DestroyPendingObject(guid);
}

void ObjectManager::DestroyAllObjects() {
  for (const auto &[guid, object] : objects_) {
    NotifyTransportPassengersIfDespawningTransport(*this, *object, callbacks_);
    object->PrepareForWorldRemoval();
    ClearTransportPassengerMembership(*this, *object);
    ClearTransportRuntimeRegistration(*this, *object);
    ClearObjectScopedDescriptorCallbacks(guid);
  }
  for (const auto &[guid, pending] : pending_objects_) {
    NotifyTransportPassengersIfDespawningTransport(*this, *pending.object, callbacks_);
    pending.object->PrepareForWorldRemoval();
    ClearTransportPassengerMembership(*this, *pending.object);
    ClearTransportRuntimeRegistration(*this, *pending.object);
    ClearObjectScopedDescriptorCallbacks(guid);
  }

  objects_.clear();
  pending_objects_.clear();
  world_publication_queue_.clear();
  world_publication_entries_.clear();
  preallocated_create_objects_.clear();
  for (auto &pending_list : pending_by_type_) {
    pending_list.clear();
  }
  ClearAllTrackedReferences();
}

void ObjectManager::Reset() {
  DestroyAllObjects();
  name_cache_.clear();
  presentation_state_.clear();
  presentation_slot_free_list_.clear();
  presentation_slot_count_ = 0;
  map_id_ = 0;
  zone_id_ = 0;
  area_id_ = 0;
}

ObjectPresentationSnapshot ObjectManager::PublishPresentationSnapshot(
    const WorldSession& session) {
  ObjectPresentationSnapshot snapshot;
  snapshot.publication_generation =
      ++object_presentation_publication_generation_;
  snapshot.target = target_;

  auto& walk_records = presentation_walk_scratch_;
  walk_records.clear();
  walk_records.reserve(objects_.size());

  const std::uint64_t publication_generation = snapshot.publication_generation;

  const auto* const active_player = GetActivePlayer();
  const std::uint32_t publish_tick_ms = openwow::core::GameClock::GetTickCount32();

  for (const auto& [guid, object] : objects_) {

    if (object->IsPendingRemoval()) {
      continue;
    }

    auto& published = presentation_state_[guid];
    if (published.presentation_generation == 0) {
      published.presentation_generation = next_object_presentation_generation_++;
    }
    if (published.presentation_slot == kNoPresentationSlot) {
      if (!presentation_slot_free_list_.empty()) {
        published.presentation_slot = presentation_slot_free_list_.back();
        presentation_slot_free_list_.pop_back();
      } else {
        published.presentation_slot = presentation_slot_count_++;
      }
    }

    const auto world_position = object->GetPosition();
    const auto* unit =
        object->IsUnit() ? static_cast<const CGUnit_C*>(object.get())
                         : nullptr;
    const std::uint32_t mount_display_id =
        unit != nullptr ? unit->Mount().CachedDisplayForSpell() : 0u;
    std::uint32_t relation_mask = 0u;
    if (unit != nullptr && active_player != nullptr) {

      const auto group_relation =
          active_player->Interaction().ResolveGroupRelation(*unit);
      relation_mask |= group_relation.same_party ? 0x10000u : 0u;
      relation_mask |= group_relation.same_raid ? 0x20000u : 0u;

      UnitInteractionRuntime::ReactionMemo reaction;
      relation_mask |=
          active_player->Interaction().CanAssistSpellTarget(*unit, false,
                                                            reaction)
              ? 0x40000u
              : 0u;
      relation_mask |=
          active_player->Interaction().CanAttackSpellTarget(*unit, reaction)
              ? 0x80000u
              : 0u;
    }
    const auto* game_object =
        object->IsGameObject()
            ? static_cast<const CGGameObject_C*>(object.get())
            : nullptr;
    ObjectPresentationRecord record{
        .handle = ObjectHandle{guid, published.presentation_generation},
        .presentation_slot = published.presentation_slot,
        .type_id = object->GetTypeId(),
        .x = world_position.x,
        .y = world_position.y,
        .z = world_position.z,
        .facing = object->GetWorldFacing(),
        .scale = object->GetScale(),
        .display_id = object->GetDisplayId(),
        .health = object->GetHealth(),
        .max_health = object->GetMaxHealth(),
        .level = object->GetLevel(),
        .movement_flags = object->GetMovementInfo().flags,
        .mount_display_id = mount_display_id,
        .unit_flags = unit != nullptr ? unit->State().GetUnitFlags() : 0u,
        .unit_flags2 = unit != nullptr ? unit->State().GetUnitFlags2() : 0u,
        .creature_type =
            unit != nullptr
                ? static_cast<std::uint32_t>(unit->State().GetCreatureType())
                : 0u,
        .model_relation_mask = relation_mask,
        .dynamic_flags =
            unit != nullptr
                 ? unit->State().GetDynamicFlags()
                 : (object->IsCorpse()
                        ? static_cast<const CGCorpse_C*>(object.get())
                              ->GetDynamicFlags()
                        : 0u),
        .render_opacity = object->GetEffectiveRenderOpacity(),
        .controlled_by_local_player =
            unit != nullptr && active_player != nullptr &&
            (guid == active_player->GetGuid() ||
              unit->Interaction().MatchesImmediateControllerGuid(
                 active_player->GetGuid())),
        .game_object_requirements_met =
            game_object != nullptr &&
            game_object->PlayerMeetsRequirements(session),
        .game_object_highlighted =
            game_object != nullptr && game_object->ShouldHighlight(),
        .game_object_spell_focus_eligible =
            game_object != nullptr &&
            game_object->IsSpellFocusTargetEligible(session.spells()),
        .corpse_spell_target_eligible =
            object->IsCorpse() &&
            openwow::game::ShouldTargetCorpseForCaster(
                session.spells().GetTargeting(),
                active_player,
                static_cast<const CGCorpse_C*>(object.get())),
        .unit_click_deferred =
            unit != nullptr && !unit->IsPlayer() &&
            ShouldDeferUnitClickTarget(session, *unit, *this, publish_tick_ms),
        .mounted = mount_display_id != 0u,
        .locomotion =
            unit != nullptr
                ? BuildLocomotionState(*unit,
                                       object->GetMovementInfo().flags,
                                       unit->State().IsDead(),
                                       unit->Animation().GetEmoteInternalFlags())
                : CharacterLocomotionState{},
    };
    if (guid == local_player_guid_) {
      snapshot.local_player = record.handle;
    }
    walk_records.push_back(record);

    if (published.seen_publication_generation != 0u &&
        published.record.handle != record.handle) {
      snapshot.retired.push_back(std::move(published.record));
    }
    published.record = std::move(record);
    published.seen_publication_generation = publication_generation;
  }

  for (auto it = presentation_state_.begin(); it != presentation_state_.end();) {
    if (it->second.seen_publication_generation == publication_generation) {
      ++it;
      continue;
    }

    if (it->second.seen_publication_generation != 0u) {
      snapshot.retired.push_back(std::move(it->second.record));
    }

    if (it->second.presentation_slot != kNoPresentationSlot) {
      presentation_slot_free_list_.push_back(it->second.presentation_slot);
    }
    it = presentation_state_.erase(it);
  }

  const auto by_handle = [](const ObjectPresentationRecord& lhs,
                            const ObjectPresentationRecord& rhs) {
    if (lhs.handle.guid.GetRawValue() != rhs.handle.guid.GetRawValue()) {
      return lhs.handle.guid.GetRawValue() < rhs.handle.guid.GetRawValue();
    }
    return lhs.handle.generation < rhs.handle.generation;
  };
  SortActiveRecords(walk_records, snapshot.active);
  std::sort(snapshot.retired.begin(), snapshot.retired.end(), by_handle);

  return snapshot;
}

void ObjectManager::SortActiveRecords(
    std::vector<ObjectPresentationRecord>& walk_records,
    std::vector<ObjectPresentationRecord>& sorted_records) {

  const std::size_t count = walk_records.size();
  auto& keys = presentation_sort_keys_;
  auto& seeded = presentation_sort_seeded_keys_;
  auto& order = presentation_sort_order_;
  keys.resize(count);
  for (std::size_t index = 0u; index < count; ++index) {
    const auto& handle = walk_records[index].handle;
    keys[index] = {.raw_guid = handle.guid.GetRawValue(),
                   .generation = handle.generation,
                   .walk_index = static_cast<std::uint32_t>(index)};
  }

  if (order.size() != count) {
    order.resize(count);
    std::iota(order.begin(), order.end(), 0u);
  }
  seeded.resize(count);
  for (std::size_t position = 0u; position < count; ++position) {
    seeded[position] = keys[order[position]];
  }
  std::sort(seeded.begin(), seeded.end(),
            [](const PresentationSortKey& lhs, const PresentationSortKey& rhs) {
              if (lhs.raw_guid != rhs.raw_guid) {
                return lhs.raw_guid < rhs.raw_guid;
              }
              return lhs.generation < rhs.generation;
            });
  sorted_records.clear();
  sorted_records.reserve(count);
  for (std::size_t position = 0u; position < count; ++position) {
    order[position] = seeded[position].walk_index;
    sorted_records.push_back(std::move(walk_records[order[position]]));
  }
}

std::optional<ObjectHandle> ObjectManager::GetObjectHandle(
    const ObjectGuid guid) const {
  const auto object = objects_.find(guid);
  const auto state = presentation_state_.find(guid);
  if (object == objects_.end() || state == presentation_state_.end() ||
      state->second.presentation_generation == 0) {
    return std::nullopt;
  }
  return ObjectHandle{guid, state->second.presentation_generation};
}

const WorldObject* ObjectManager::ResolveObjectHandle(
    const ObjectHandle handle) const {
  const auto state = presentation_state_.find(handle.guid);
  if (state == presentation_state_.end() ||
      state->second.presentation_generation != handle.generation) {
    return nullptr;
  }
  return Get(handle.guid);
}

UpdateObjectHandler ObjectManager::MakeHandler() {
  return UpdateObjectHandler{
      .on_create = [this](const CreateObjectUpdate &u) { OnCreate(u); },
      .on_values = [this](const ValuesUpdate &u) { OnValues(u); },
      .on_values_skipped =
          [this](const ObjectGuid guid) { (void)ConsumeDeferredPrepassValues(guid); },
      .on_movement = [this](const MovementOnlyUpdate &u) { OnMovement(u); },
      .on_out_of_range = [this](const OutOfRangeUpdate &u) { OnOutOfRange(u); },
      .on_near_objects = [this](const NearObjectsUpdate &u) { OnNearObjects(u); },
      .resolve_values_field_count = [this](ObjectGuid guid) -> std::optional<std::uint16_t> {
        return ResolveFieldCountForTrackedObject(guid);
      },
  };
}

bool ObjectManager::HandleUpdateObject(const std::uint8_t *data, std::size_t len) {
  deferred_prepass_values_.clear();
  deferred_prepass_values_cursor_ = 0;
  if (callbacks_.on_update_object_batch_started) {
    callbacks_.on_update_object_batch_started();
  }

  const auto finish_batch = [this](const bool committed) {
    deferred_prepass_values_.clear();
    deferred_prepass_values_cursor_ = 0;
    if (callbacks_.on_update_object_batch_finished) {
      callbacks_.on_update_object_batch_finished(committed);
    }
    SweepStalePendingObjects();
  };

  UpdateObjectHandler leading_out_of_range_handler;
  leading_out_of_range_handler.on_out_of_range =
      [this](const OutOfRangeUpdate &update) { OnOutOfRange(update); };
  if (!ParseLeadingOutOfRangeUpdate(data, len,
                                    leading_out_of_range_handler)) {
    finish_batch(false);
    return false;
  }

  std::vector<ObjectGuid> created_shells;

  if (!PreallocateCreateObjects(data, len, created_shells)) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "UpdateObject: preallocation pass failed shells=" +
            std::to_string(created_shells.size()) + " payload=" +
            std::to_string(len) + " bytes");
    ClearPreallocatedCreateMarkers(created_shells);
    finish_batch(false);
    return false;
  }

  auto handler = MakeHandler();

  handler.on_out_of_range = {};
  handler.on_near_objects = {};
  UpdateObjectParseStats stats;
  const bool ok = ParseUpdateObject(data, len, handler, &stats);
  if (!ok) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "UpdateObject: apply pass failed shells=" +
            std::to_string(created_shells.size()) + " payload=" +
            std::to_string(len) + " bytes");
    ClearPreallocatedCreateMarkers(created_shells);
    finish_batch(false);
    return false;
  }

  openwow::diagnostics::Log(
      openwow::diagnostics::LogLevel::kDebug,
      "UpdateObject: parsed blocks=" +
          std::to_string(stats.completed_blocks) +
          " values=" + std::to_string(stats.blocks_by_type[0]) +
          " movement=" + std::to_string(stats.blocks_by_type[1]) +
          " create=" + std::to_string(stats.blocks_by_type[2]) +
          " create2=" + std::to_string(stats.blocks_by_type[3]) +
          " outOfRange=" + std::to_string(stats.blocks_by_type[4]) +
          " near=" + std::to_string(stats.blocks_by_type[5]) +
          " payload=" + std::to_string(len) + " bytes");

  DrainWorldPublicationQueue();
  finish_batch(true);
  return true;
}

bool ObjectManager::HandleCompressedUpdateObject(const std::uint8_t *data, std::size_t len) {
  auto decompressed = DecompressUpdateObjectPayload(data, len);
  if (!decompressed.has_value()) {
    return false;
  }
  return HandleUpdateObject(decompressed->data(), decompressed->size());
}

void ObjectManager::HandleDestroyObject(const std::uint8_t *data, std::size_t len) {
  PacketReader reader(data, len);
  ObjectGuid guid;
  if (!reader.ReadGuid(guid))
    return;

  std::uint8_t destroy_flag = 0;
  if (!reader.ReadU8(destroy_flag))
    return;

  if (objects_.contains(guid)) {
    (void)StageActiveObjectForServerRemoval(guid, destroy_flag != 0);
  }
}

static const char *TypeIDName(TypeID t) {
  switch (t) {
  case TypeID::kObject:
    return "Object";
  case TypeID::kItem:
    return "Item";
  case TypeID::kContainer:
    return "Container";
  case TypeID::kUnit:
    return "Unit";
  case TypeID::kPlayer:
    return "Player";
  case TypeID::kGameObject:
    return "GameObject";
  case TypeID::kDynamicObject:
    return "DynamicObject";
  case TypeID::kCorpse:
    return "Corpse";
  }
  return "Unknown";
}

void ObjectManager::DumpState(const std::string &filepath) const {

  auto parent = std::filesystem::path(filepath).parent_path();
  if (!parent.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
  }

  std::ofstream out(filepath);
  if (!out.is_open()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "ObjectManager::DumpState: cannot open " + filepath);
    return;
  }

  std::size_t counts[kNumClientObjectTypes] = {};
  for (const auto &[guid, obj] : objects_) {
    auto idx = static_cast<std::size_t>(obj->GetTypeId());
    if (idx < kNumClientObjectTypes)
      ++counts[idx];
  }

  out << "=== ObjectManager State Dump ===\n";
  out << "Total objects: " << Count() << "\n";
  out << "Active objects: " << objects_.size() << "  Pending objects: " << pending_objects_.size()
      << "\n";
  out << "Map: " << map_id_ << "  Zone: " << zone_id_ << "  Area: " << area_id_ << "\n";
  out << "Local player: " << local_player_guid_.ToString() << "\n";
  out << "Target: " << target_.ToString() << "\n";
  out << "Focus: " << focus_target_.ToString() << "\n";
  out << "Name cache entries: " << name_cache_.size() << "\n\n";

  out << "--- Counts by TypeID ---\n";
  for (int i = 0; i < kNumClientObjectTypes; ++i) {
    if (counts[i] > 0)
      out << "  " << TypeIDName(static_cast<TypeID>(i)) << ": " << counts[i] << "\n";
  }
  out << "\n";

  out << "--- Object Details ---\n";
  for (const auto &[guid, obj] : objects_) {
    out << "GUID=" << guid.ToString() << "  Type=" << TypeIDName(obj->GetTypeId())
        << "  Entry=" << obj->GetEntry();

    std::string name = obj->GetName();
    if (name.empty()) {

      auto it = name_cache_.find(guid.GetRawValue());
      if (it != name_cache_.end())
        name = it->second.name;
    }
    if (!name.empty())
      out << "  Name=\"" << name << "\"";

    out << "  Level=" << obj->GetLevel();

    auto pos = obj->GetPosition();
    out << "  Pos=(" << pos.x << ", " << pos.y << ", " << pos.z << " | o=" << pos.facing << ")";

    if (obj->IsUnit()) {
      const auto *unit = static_cast<const CGUnit_C *>(obj.get());
      out << "  HP=" << unit->State().GetHealth() << "/" << unit->State().GetMaxHealth();
      auto pt = unit->State().GetPowerType();
      out << "  Power[" << static_cast<int>(pt) << "]=" << unit->State().GetPower(pt) << "/"
          << unit->State().GetMaxPower(pt);
      out << "  Race=" << static_cast<int>(unit->State().GetRace())
          << "  Class=" << static_cast<int>(unit->State().GetClass())
          << "  Gender=" << static_cast<int>(unit->State().GetGender());
      if (unit->State().IsInCombat())
        out << "  [COMBAT]";
      if (unit->State().IsDead())
        out << "  [DEAD]";
    }

    out << "\n";
  }

  out << "\n=== End Dump ===\n";
  out.close();

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo, "ObjectManager::DumpState: wrote " +
                                                         std::to_string(objects_.size()) +
                                                         " objects to " + filepath);
}

void ObjectManager::Clear() {
  DestroyAllObjects();
}

void ObjectManager::ApplyCreateFieldsToExistingObject(
    CGObject_C &object, const CreateObjectUpdate &upd,
    const bool clear_missing_fields) {

  const auto expanded_fields = clear_missing_fields
                                   ? ExpandExistingCreateFields(upd.fields)
                                   : UpdateFieldValues{};
  const auto &applied_fields =
      clear_missing_fields ? expanded_fields : upd.fields;
  object.ApplyRawFieldValues(applied_fields);
  RefreshActivePlayerCorpseReference(object);
}

bool ObjectManager::ApplyCreateBlockToExistingObject(
    CGObject_C &object, const CreateObjectUpdate &upd) {
  const auto previous_transport_attachment = ReadTransportAttachment(object);
  bool movement_applied = false;

  if (object.IsUnit() && upd.guid != CGObject_C::GetActivePlayerGuid()) {
    movement_applied = object.ApplyMovementUpdate(
        MovementOnlyUpdate{upd.guid, upd.movement, upd.client_receive_tick_ms});
  }

  ApplyCreateFieldsToExistingObject(object, upd,
                                    false);
  if (movement_applied) {
    SyncTransportPassengerMembership(*this, object,
                                     previous_transport_attachment);
  }
  return movement_applied;
}

void ObjectManager::OnCreate(const CreateObjectUpdate &upd) {
  const auto notify_create_movement_metadata = [this, &upd](CGObject_C &object) {
    if (callbacks_.on_unit_create_movement_metadata && object.IsUnit()) {
      callbacks_.on_unit_create_movement_metadata(
          static_cast<CGUnit_C &>(object), upd.movement);
    }
  };
  const auto notify_authoritative_unit_movement =
      [this, &upd](CGObject_C &object) {
        if (callbacks_.on_unit_authoritative_movement && object.IsUnit() &&
            upd.movement.IsLiving()) {
          callbacks_.on_unit_authoritative_movement(
              static_cast<CGUnit_C &>(object), upd.movement,
              upd.client_receive_tick_ms);
        }
      };
  const bool was_preallocated = preallocated_create_objects_.erase(upd.guid) != 0;
  if (auto *existing = FindMutableForPacketUpdate(upd.guid)) {
    const auto batch =
        was_preallocated ? BuildFieldUpdateBatch(nullptr, upd.fields, true)
                         : FieldUpdateBatch{};
    bool movement_applied = false;
    if (was_preallocated) {

      if (upd.movement.IsSelf() && upd.type_id == TypeID::kPlayer) {
        SetActivePlayer(upd.guid);
      }
      notify_create_movement_metadata(*existing);
      auto finalized_update = upd;
      if (existing->IsUnit() &&
          upd.guid != CGObject_C::GetActivePlayerGuid()) {
        const auto previous_transport_attachment =
            ReadTransportAttachment(*existing);
        movement_applied = existing->ApplyMovementUpdate(MovementOnlyUpdate{
            upd.guid, upd.movement, upd.client_receive_tick_ms});
        if (movement_applied) {
          finalized_update.movement_applied_before_post_init = true;
          SyncTransportPassengerMembership(
              *this, *existing, previous_transport_attachment);
          notify_authoritative_unit_movement(*existing);
        }
      }
      existing->FinalizeCreateUpdate(finalized_update);
      if (!existing->IsUnit()) {
        SyncTransportPassengerMembership(*this, *existing, std::nullopt);
      }
      RefreshActivePlayerCorpseReference(*existing);
    } else {

      movement_applied = ApplyCreateBlockToExistingObject(*existing, upd);
      notify_create_movement_metadata(*existing);
    }
    if (!was_preallocated && movement_applied) {
      notify_authoritative_unit_movement(*existing);
    }

    if (was_preallocated) {
      if (upd.movement.IsSelf() && upd.type_id == TypeID::kPlayer) {
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                           "ObjectManager: local player created - " + upd.guid.ToString());
        if (callbacks_.on_player_self_created)
          callbacks_.on_player_self_created(upd.guid);
      }

      if (callbacks_.on_object_created)
        callbacks_.on_object_created(*existing);
    }

    if (was_preallocated && callbacks_.on_fields_changed &&
        !batch.updated_fields.empty()) {
      callbacks_.on_fields_changed(*existing, batch, was_preallocated);
    }
    return;
  }

  if (!(upd.movement.IsSelf() && upd.type_id == TypeID::kPlayer) &&
      upd.guid != CGObject_C::GetActivePlayerGuid()) {
    ReapExpiredPendingObjectBeforeCreate(upd.type_id);
  }

  auto obj = openwow::game::CreateObject(
      upd.guid, upd.type_id, *this, inventory_, item_definitions_, dbc_loader_);
  obj->BindM2System(m2_system_);
  obj->BindWorldFrame(world_frame_);
  obj->BindWorldEnvironmentState(world_environment_);
  const auto batch = BuildFieldUpdateBatch(nullptr, upd.fields, true);
  obj->ApplyCreateUpdate(upd);
  RefreshActivePlayerCorpseReference(*obj);
  SyncTransportPassengerMembership(*this, *obj, std::nullopt);

  auto *raw_ptr = obj.get();
  presentation_state_[upd.guid].presentation_generation =
      next_object_presentation_generation_++;
  objects_[upd.guid] = std::move(obj);
  notify_create_movement_metadata(*raw_ptr);
  notify_authoritative_unit_movement(*raw_ptr);

  if (upd.movement.IsSelf() && upd.type_id == TypeID::kPlayer) {

    SetActivePlayer(upd.guid);
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                       "ObjectManager: local player created - " + upd.guid.ToString());
    if (callbacks_.on_player_self_created)
      callbacks_.on_player_self_created(upd.guid);
  }

  if (callbacks_.on_object_created)
    callbacks_.on_object_created(*raw_ptr);

  if (callbacks_.on_fields_changed && !batch.updated_fields.empty()) {
    callbacks_.on_fields_changed(*raw_ptr, batch, true);
  }
}

void ObjectManager::ApplyPrepassValues(const ValuesUpdate &upd) {
  auto& deferred = deferred_prepass_values_.emplace_back();
  deferred.guid = upd.guid;
  bool promoted_from_pending = false;
  auto *object = FindMutableForPacketUpdate(upd.guid, &promoted_from_pending);
  if (object == nullptr) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "ObjectManager: values update for unknown object " + upd.guid.ToString());
    return;
  }
  const auto batch = BuildFieldUpdateBatch(object, upd.fields, false);
  object->ApplyRawFieldValues(upd.fields);
  RefreshActivePlayerCorpseReference(*object);
  if (promoted_from_pending) {
    FinalizePacketUpdatePromotion(callbacks_, *object);
  }
  if (!batch.value_changes.empty()) {
    deferred.changed_fields = batch;
  }
}

std::optional<FieldUpdateBatch>
ObjectManager::ConsumeDeferredPrepassValues(const ObjectGuid guid) {
  if (deferred_prepass_values_cursor_ >= deferred_prepass_values_.size()) {
    return std::nullopt;
  }

  const auto& deferred =
      deferred_prepass_values_[deferred_prepass_values_cursor_++];
  if (deferred.guid != guid) {
    return std::nullopt;
  }
  return deferred.changed_fields;
}

void ObjectManager::OnValues(const ValuesUpdate &upd) {
  bool promoted_from_pending = false;
  auto *object = FindMutableForPacketUpdate(upd.guid, &promoted_from_pending);
  if (object == nullptr) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "ObjectManager: values update for unknown object " + upd.guid.ToString());
    return;
  }
  FieldUpdateBatch batch = BuildFieldUpdateBatch(object, upd.fields, false);
  if (auto deferred = ConsumeDeferredPrepassValues(upd.guid);
      deferred.has_value()) {
    batch = std::move(*deferred);
  }
  object->ApplyValuesUpdate(upd);
  RefreshActivePlayerCorpseReference(*object);
  if (promoted_from_pending) {
    FinalizePacketUpdatePromotion(callbacks_, *object);
  }
  if (callbacks_.on_object_updated && !batch.value_changes.empty())
    callbacks_.on_object_updated(*object);

  if (callbacks_.on_fields_changed && !batch.value_changes.empty()) {
    callbacks_.on_fields_changed(*object, batch, false);
  }
}

void ObjectManager::OnMovement(const MovementOnlyUpdate &upd) {

  if (upd.guid == player_control_.ActiveMoverGuid()) {
    return;
  }

  bool promoted_from_pending = false;
  auto *object = FindMutableForPacketUpdate(upd.guid, &promoted_from_pending);
  if (object == nullptr) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "ObjectManager: movement update for unknown object " + upd.guid.ToString());
    return;
  }
  const auto previous_transport_attachment = ReadTransportAttachment(*object);
  if (!object->ApplyMovementUpdate(upd)) {
    return;
  }
  SyncTransportPassengerMembership(*this, *object, previous_transport_attachment);
  if (promoted_from_pending) {
    FinalizePacketUpdatePromotion(callbacks_, *object);
    RefreshActivePlayerCorpseReference(*object);
  }
  if (!upd.has_resolved_presentation_tick &&
      callbacks_.on_unit_authoritative_movement && object->IsUnit() &&
      upd.movement.IsLiving()) {
    callbacks_.on_unit_authoritative_movement(
        static_cast<CGUnit_C &>(*object), upd.movement,
        upd.client_receive_tick_ms);
  }
  if (callbacks_.on_object_updated)
    callbacks_.on_object_updated(*object);
}

void ObjectManager::OnOutOfRange(const OutOfRangeUpdate &upd) {

  VehiclePassenger_SetUpdateFlag();

  constexpr std::uint32_t kArenaMapInstanceType = 4u;

  const auto local_guid = GetActivePlayerGuid();
  auto &bf_info = BattlefieldInfo::Get();
  const bool in_arena = bf_info.GetActiveBGType() == kArenaMapInstanceType;

  for (const auto &guid : upd.guids) {
    if (guid == local_guid)
      continue;

    auto *object = GetMutable(guid);
    if (object == nullptr)
      continue;

    if (in_arena) {
      bf_info.OnArenaUnitUnseen(guid, *this);
    }

    if (callbacks_.on_object_out_of_range) {
      callbacks_.on_object_out_of_range(*object);
    }
    (void)StageActiveObjectForServerRemoval(guid, false);
  }

  if (callbacks_.on_out_of_range_vehicle_transitions_ready) {
    callbacks_.on_out_of_range_vehicle_transitions_ready();
  }
}

void ObjectManager::OnNearObjects(const NearObjectsUpdate &upd) {

  const auto local_guid = GetActivePlayerGuid();
  for (const auto &guid : upd.guids) {
    if (guid == local_guid)
      continue;
    bool promoted_from_pending = false;
    auto *object = FindMutableForPacketUpdate(guid, &promoted_from_pending);
    if (object != nullptr && promoted_from_pending) {
      FinalizePacketUpdatePromotion(callbacks_, *object);
      RefreshActivePlayerCorpseReference(*object);
    }
  }
}

ObjectManager::PendingObjectEntry *ObjectManager::FindPendingEntry(ObjectGuid guid) {
  auto it = pending_objects_.find(guid);
  return it != pending_objects_.end() ? &it->second : nullptr;
}

const ObjectManager::PendingObjectEntry *ObjectManager::FindPendingEntry(ObjectGuid guid) const {
  auto it = pending_objects_.find(guid);
  return it != pending_objects_.end() ? &it->second : nullptr;
}

CGObject_C *ObjectManager::FindMutableForPacketUpdate(
    ObjectGuid guid, bool *promoted_from_pending) {
  if (promoted_from_pending != nullptr) {
    *promoted_from_pending = false;
  }
  if (auto *object = GetMutable(guid); object != nullptr) {
    auto *typed_object = static_cast<CGObject_C *>(object);
    typed_object->SetPendingRemoval(false);
    return typed_object;
  }

  auto *object = PromotePendingObject(guid);
  if (object != nullptr) {
    EnqueueWorldPublication(guid);
  }
  if (object != nullptr && promoted_from_pending != nullptr) {
    *promoted_from_pending = true;
  }
  return object;
}

CGObject_C *ObjectManager::PromotePendingObject(ObjectGuid guid) {
  auto pending_it = pending_objects_.find(guid);
  if (pending_it == pending_objects_.end()) {
    return nullptr;
  }

  const std::size_t type_index = static_cast<std::size_t>(pending_it->second.type_id);
  if (type_index < pending_by_type_.size()) {
    pending_by_type_[type_index].erase(pending_it->second.type_order_it);
  }
  auto object = std::move(pending_it->second.object);
  auto *raw_ptr = object.get();
  raw_ptr->SetPendingRemoval(false);
  presentation_state_[guid].presentation_generation =
      next_object_presentation_generation_++;
  objects_[guid] = std::move(object);
  pending_objects_.erase(pending_it);
  return raw_ptr;
}

bool ObjectManager::DestroyPendingObject(ObjectGuid guid) {
  auto pending_it = pending_objects_.find(guid);
  if (pending_it == pending_objects_.end()) {
    return false;
  }

  NotifyTransportPassengersIfDespawningTransport(*this, *pending_it->second.object,
                                                 callbacks_);
  pending_it->second.object->PrepareForWorldRemoval();
  ClearTransportPassengerMembership(*this, *pending_it->second.object);
  ClearTransportRuntimeRegistration(*this, *pending_it->second.object);

  const std::size_t type_index = static_cast<std::size_t>(pending_it->second.type_id);
  if (type_index < pending_by_type_.size()) {
    pending_by_type_[type_index].erase(pending_it->second.type_order_it);
  }
  pending_objects_.erase(pending_it);
  ClearTrackedReferences(guid);
  return true;
}

bool ObjectManager::StageActiveObjectForServerRemoval(ObjectGuid guid,
                                                      bool destroy_packet_death_cleanup) {
  auto it = objects_.find(guid);
  if (it == objects_.end()) {
    return false;
  }

  const bool run_death_cleanup =
      ShouldRunDestroyPacketDeathCleanup(*it->second, destroy_packet_death_cleanup);
  if (run_death_cleanup) {
    RunDestroyPacketDeathCleanup(*it->second, true);
  }

  if (it->second->IsGameObject()) {
    NotifyTransportPassengersIfDespawningTransport(*this, *it->second,
                                                   callbacks_);
  }
  it->second->PrepareForWorldRemoval();

  if (callbacks_.on_object_pre_destroyed) {
    callbacks_.on_object_pre_destroyed(*it->second, run_death_cleanup);
  }

  if (it->second->HasLifetimeHolds()) {
    it->second->SetPendingRemoval(true);
    return true;
  }

  return FinalizeActiveObjectServerRemoval(guid);
}

bool ObjectManager::FinalizeActiveObjectServerRemoval(ObjectGuid guid) {
  auto it = objects_.find(guid);
  if (it == objects_.end()) {
    return false;
  }

  preallocated_create_objects_.erase(guid);
  ClearObjectScopedDescriptorCallbacks(guid);
  const TypeID type_id = it->second->GetTypeId();
  if (callbacks_.on_object_destroyed_typed) {
    callbacks_.on_object_destroyed_typed(guid, type_id);
  }
  if (callbacks_.on_object_destroyed) {
    callbacks_.on_object_destroyed(guid);
  }

  auto object = std::move(it->second);
  RemoveWorldPublication(guid);
  ClearTransportPassengerMembership(*this, *object);
  ClearTransportRuntimeRegistration(*this, *object);
  object->SetPendingRemoval(false);
  objects_.erase(it);
  ClearTrackedReferences(guid);
  StagePendingObject(std::move(object), type_id);
  return true;
}

void ObjectManager::DestroyActiveObject(ObjectGuid guid, bool destroy_packet_death_cleanup) {
  auto it = objects_.find(guid);
  if (it == objects_.end()) {
    return;
  }

  const bool run_death_cleanup =
      ShouldRunDestroyPacketDeathCleanup(*it->second, destroy_packet_death_cleanup);
  RunDestroyPacketDeathCleanup(*it->second, run_death_cleanup);
  if (it->second->IsGameObject()) {
    NotifyTransportPassengersIfDespawningTransport(*this, *it->second,
                                                   callbacks_);
  }
  it->second->PrepareForWorldRemoval();

  preallocated_create_objects_.erase(guid);
  if (callbacks_.on_object_pre_destroyed) {
    callbacks_.on_object_pre_destroyed(*it->second, run_death_cleanup);
  }
  ClearObjectScopedDescriptorCallbacks(guid);
  if (callbacks_.on_object_destroyed_typed) {
    callbacks_.on_object_destroyed_typed(guid, it->second->GetTypeId());
  }
  if (callbacks_.on_object_destroyed) {
    callbacks_.on_object_destroyed(guid);
  }
  ClearTransportPassengerMembership(*this, *it->second);
  ClearTransportRuntimeRegistration(*this, *it->second);
  RemoveWorldPublication(guid);
  objects_.erase(it);
  ClearTrackedReferences(guid);
}

std::optional<std::uint16_t>
ObjectManager::ResolveFieldCountForTrackedObject(ObjectGuid guid) const {
  TypeID type_id = TypeID::kObject;
  if (const auto it = objects_.find(guid); it != objects_.end()) {
    type_id = it->second->GetTypeId();
  } else if (const auto *pending = FindPendingEntry(guid); pending != nullptr) {
    type_id = pending->type_id;
  } else {
    return std::nullopt;
  }

  if (type_id != TypeID::kPlayer) {
    return std::optional<std::uint16_t>(FieldCountFor(type_id));
  }

  return std::optional<std::uint16_t>(
      FieldCountForPlayer(guid == CGObject_C::GetActivePlayerGuid()));
}

bool ObjectManager::PreallocateCreateObjects(const std::uint8_t *data, std::size_t len,
                                             std::vector<ObjectGuid> &created_shells) {
  UpdateObjectHandler handler;
  handler.on_values = [this](const ValuesUpdate &update) { ApplyPrepassValues(update); };
  handler.on_values_skipped = [this](const ObjectGuid guid) {
    auto& deferred = deferred_prepass_values_.emplace_back();
    deferred.guid = guid;
  };
  handler.on_near_objects =
      [this](const NearObjectsUpdate &update) { OnNearObjects(update); };
  handler.resolve_values_field_count =
      [this](const ObjectGuid guid) -> std::optional<std::uint16_t> {
    return ResolveFieldCountForTrackedObject(guid);
  };
  handler.on_create = [this, &created_shells](const CreateObjectUpdate &upd) {
    if (upd.movement.IsSelf() && upd.type_id == TypeID::kPlayer) {
      SetActivePlayer(upd.guid);
    }

    bool promoted_from_pending = false;
    if (auto *existing = FindMutableForPacketUpdate(upd.guid, &promoted_from_pending)) {
      ApplyCreateFieldsToExistingObject(*existing, upd,
                                        true);
      if (promoted_from_pending) {
        FinalizePacketUpdatePromotion(callbacks_, *existing);
        RefreshActivePlayerCorpseReference(*existing);
      }
      return;
    }

    if (upd.guid != CGObject_C::GetActivePlayerGuid()) {
      ReapExpiredPendingObjectBeforeCreate(upd.type_id);
    }

    auto object = openwow::game::CreateObject(
        upd.guid, upd.type_id, *this, inventory_, item_definitions_, dbc_loader_);
    object->BindM2System(m2_system_);
    object->BindWorldFrame(world_frame_);
    object->BindWorldEnvironmentState(world_environment_);
    auto first_pass_update = upd;
    first_pass_update.defer_post_init = true;
    object->ApplyCreateUpdate(first_pass_update);
    presentation_state_[upd.guid].presentation_generation =
        next_object_presentation_generation_++;
    objects_[upd.guid] = std::move(object);
    preallocated_create_objects_.insert(upd.guid);
    created_shells.push_back(upd.guid);
  };

  return ParseUpdateObject(data, len, handler);
}

void ObjectManager::ClearPreallocatedCreateMarkers(
    const std::vector<ObjectGuid> &created_shells) {
  for (const ObjectGuid guid : created_shells) {
    preallocated_create_objects_.erase(guid);
  }
}

void ObjectManager::StagePendingObject(std::unique_ptr<CGObject_C> object, TypeID type_id) {
  const ObjectGuid guid = object->GetGuid();
  if (auto pending_it = pending_objects_.find(guid); pending_it != pending_objects_.end()) {
    const std::size_t existing_type_index = static_cast<std::size_t>(pending_it->second.type_id);
    if (existing_type_index < pending_by_type_.size()) {
      pending_by_type_[existing_type_index].erase(pending_it->second.type_order_it);
    }
    pending_objects_.erase(pending_it);
  }

  const std::size_t type_index = static_cast<std::size_t>(type_id);
  auto &pending_list = pending_by_type_[type_index];
  pending_list.push_back(guid);
  auto order_it = std::prev(pending_list.end());

  PendingObjectEntry entry;
  entry.object = std::move(object);
  entry.type_id = type_id;
  entry.created_at_ms = CurrentPendingObjectTick();
  entry.type_order_it = order_it;
  pending_objects_[guid] = std::move(entry);
}

void ObjectManager::EnqueueWorldPublication(const ObjectGuid guid) {
  if (world_publication_entries_.contains(guid)) {
    return;
  }

  world_publication_queue_.push_front(guid);
  world_publication_entries_.emplace(guid, world_publication_queue_.begin());
}

void ObjectManager::RemoveWorldPublication(const ObjectGuid guid) {
  const auto entry_it = world_publication_entries_.find(guid);
  if (entry_it == world_publication_entries_.end()) {
    return;
  }

  world_publication_queue_.erase(entry_it->second);
  world_publication_entries_.erase(entry_it);
}

void ObjectManager::DrainWorldPublicationQueue() {
  while (!world_publication_queue_.empty()) {
    const ObjectGuid guid = world_publication_queue_.front();
    world_publication_queue_.pop_front();
    world_publication_entries_.erase(guid);
    const auto object_it = objects_.find(guid);
    if (object_it == objects_.end()) {
      continue;
    }

    object_it->second->FinalizeWorldPublication();
    if (callbacks_.on_object_world_published) {
      callbacks_.on_object_world_published(*object_it->second);
    }
  }
}

bool ObjectManager::ReapExpiredPendingObjectForType(const TypeID type_id,
                                                    const std::uint32_t now_ms,
                                                    const std::uint32_t expiration_ms) {
  auto &pending_list = pending_by_type_[static_cast<std::size_t>(type_id)];
  while (!pending_list.empty()) {
    const ObjectGuid guid = pending_list.front();
    auto pending_it = pending_objects_.find(guid);
    if (pending_it == pending_objects_.end()) {
      pending_list.pop_front();
      continue;
    }

    if (static_cast<std::uint32_t>(now_ms - pending_it->second.created_at_ms) <
        expiration_ms) {
      return false;
    }

    return DestroyPendingObject(guid);
  }

  return false;
}

void ObjectManager::ReapExpiredPendingObjectBeforeCreate(const TypeID type_id) {
  (void)ReapExpiredPendingObjectForType(type_id, CurrentPendingObjectTick(),
                                        kPendingObjectReapDelayMs);
}

void ObjectManager::SweepStalePendingObjects() {
  const auto now = CurrentPendingObjectTick();
  for (int i = 1; i < kNumClientObjectTypes; ++i) {
    (void)ReapExpiredPendingObjectForType(static_cast<TypeID>(i), now,
                                          kStalePendingObjectTimeoutMs);
  }
}

void ObjectManager::ClearAllTrackedReferences() {
  SetActivePlayer(ObjectGuid());
  active_player_corpse_guid_ = ObjectGuid();
  target_ = ObjectGuid();
  focus_target_ = ObjectGuid();
  mouseover_ = ObjectGuid();
}

void ObjectManager::ClearTrackedReferences(ObjectGuid guid) {
  if (guid == local_player_guid_) {
    SetActivePlayer(ObjectGuid());
  }
  if (guid == target_) {
    target_ = ObjectGuid();
  }
  if (guid == focus_target_) {
    focus_target_ = ObjectGuid();
  }
  if (guid == mouseover_) {
    mouseover_ = ObjectGuid();
  }
  if (guid == active_player_corpse_guid_) {
    active_player_corpse_guid_ = ObjectGuid();
  }
}

void ObjectManager::RefreshActivePlayerCorpseReference(
    const CGObject_C &object) {
  if (!object.IsCorpse()) {
    return;
  }

  const auto &corpse = static_cast<const CGCorpse_C &>(object);
  const bool is_live_active_player_corpse =
      !local_player_guid_.IsEmpty() &&
      corpse.GetOwner() == local_player_guid_ &&
      (corpse.GetCorpseFlags() & kCorpseFlagBones) == 0u;
  if (is_live_active_player_corpse) {
    active_player_corpse_guid_ = corpse.GetGuid();
  } else if (active_player_corpse_guid_ == corpse.GetGuid()) {
    active_player_corpse_guid_ = ObjectGuid();
  }
}

}

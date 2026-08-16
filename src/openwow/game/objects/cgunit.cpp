#include "openwow/game/objects/cgunit.h"

#include "openwow/game/object_manager.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/vehicle_passenger.h"
#include "openwow/game/world_environment_state.h"
#include "openwow/runtime/time/game_clock.h"

#include <algorithm>
#include <cstring>

namespace openwow::game {
namespace {

[[nodiscard]] bool HasUpdatedField(const std::vector<std::uint16_t> &updated_fields,
                                   const std::uint16_t field) {
  return std::find(updated_fields.begin(), updated_fields.end(), field) != updated_fields.end();
}

}

CGUnit_C::CGUnit_C(ItemDefinitions& item_definitions,
                   const data::dbc::DbcLoader& dbc_loader,
                   const TypeID type_id)
    : CGObject_C(type_id),
      item_definitions_(item_definitions),
      dbc_loader_(dbc_loader),
      animation_(*this),
      movement_(*this),
      interaction_(*this),
      presentation_(*this),
      spell_visuals_(*this),
      state_(*this) {
  SeedTargetChangeTime();
}

CGUnit_C::CGUnit_C(ItemDefinitions& item_definitions,
                   const data::dbc::DbcLoader& dbc_loader,
                   ObjectGuid guid, TypeID type_id)
    : CGObject_C(guid, type_id),
      item_definitions_(item_definitions),
      dbc_loader_(dbc_loader),
      animation_(*this),
      movement_(*this),
      interaction_(*this),
      presentation_(*this),
      spell_visuals_(*this),
      state_(*this) {
  SeedTargetChangeTime();
}

CGUnit_C::CGUnit_C(ObjectManager& objects, ItemDefinitions& item_definitions,
                   const data::dbc::DbcLoader& dbc_loader,
                   ObjectGuid guid, TypeID type_id)
    : CGObject_C(objects, guid, type_id),
      item_definitions_(item_definitions),
      dbc_loader_(dbc_loader),
      animation_(*this),
      movement_(*this),
      interaction_(*this),
      presentation_(*this),
      spell_visuals_(*this),
      state_(*this) {
  SeedTargetChangeTime();
}

void CGUnit_C::SeedTargetChangeTime() {

  target_change_time_ms_ =
      core::GameClock::GetTickCount32() - kTargetChangeRingDurationMs;
}

void CGUnit_C::GetWorldMatrix(float* const out_matrix) const {
  if (out_matrix == nullptr) {
    return;
  }

  Vehicle().BuildWorldMatrixWithVehicle(*this, out_matrix);
}

bool CGUnit_C::UpdateModelNodeTransform(float dt,
                                        const std::uint32_t current_tick_ms) {
  ClearVisualModelWorldTransform();

  if (Vehicle().GetVehiclePassengerComponent() != nullptr) {
    auto *passenger = Vehicle().GetVehiclePassengerComponent();
    if (passenger) {
      passenger->RenderAttachment();
    }
    Presentation().UpdateMountTransitionNodeTransform();
    return true;
  }

  const auto &mi = GetMovementInfo();
  const std::uint32_t move_flags = mi.flags;
  constexpr std::uint32_t kSwimOrFly = kMoveFlagSwimming | kMoveFlagFlying;
  constexpr std::uint32_t kForwardOrBackward =
      kMoveFlagForward | kMoveFlagBackward;

  if ((move_flags & kSwimOrFly) != 0 &&
      ((move_flags & kForwardOrBackward) != 0 ||
       Movement().HasBodyLean())) {

    float matrix[16];
    std::memset(matrix, 0, sizeof(matrix));
    matrix[0] = 1.0f;
    matrix[5] = 1.0f;
    matrix[10] = 1.0f;
    matrix[15] = 1.0f;

    if (IsActiveMover()) {

      const bool apply_lean = (move_flags & kMoveFlagFlying) != 0;
      Movement().ComputeBodyLeanMatrix(matrix, dt, apply_lean);

      if ((GetMovementInfo().flags & kForwardOrBackward) != 0) {
        Movement().MarkBodyLeanMoving();
      }
    } else {

      Movement().BuildStaticBodyMatrix(matrix);
    }

    SetVisualModelWorldTransform(matrix);

    Presentation().UpdateMountTransitionNodeTransform();
    return true;
  }

  Movement().ResetFlightTransitionBodyLeanState();

  if (Mount().TransitionHandle().IsValid() &&
      Mount().TransitionNode() != nullptr) {

    Movement().BlendMountTransitionPosition(dt, current_tick_ms);
  } else {

    Movement().InterpolateShadowBlobPosition(dt);
  }

  Presentation().UpdateMountTransitionNodeTransform();
  return true;
}

std::vector<std::uint16_t> CGUnit_C::ApplyCreateUpdate(const CreateObjectUpdate &upd) {
  const auto previous_movement_flags = GetMovementInfo().flags;
  auto updated_fields = CGObject_C::ApplyCreateUpdate(upd);
  if (upd.defer_post_init) {
    return updated_fields;
  }

  FinalizeUnitCreateState(upd, previous_movement_flags, true);
  return updated_fields;
}

void CGUnit_C::FinalizeCreateUpdate(const CreateObjectUpdate &upd) {
  FinalizeUnitCreateState(upd, 0u, false);
}

void CGUnit_C::FinalizePacketUpdatePromotion() {

  CGObject_C::FinalizePacketUpdatePromotion();

  Movement().SeedBodyFacing(GetLocalFacing());

  for (std::uint32_t slot = 0u; slot != 3u; ++slot) {
    OnVirtualItemDisplayChanged(slot);
  }
}

void CGUnit_C::FinalizeUnitCreateState(
    const CreateObjectUpdate &upd,
    const std::uint32_t previous_movement_flags,
    const bool base_post_init_complete) {

  Movement().SeedBodyFacing(GetLocalFacing());
  Animation().SeedCachedSheatheStateFromDescriptor();

  if (!upd.movement_applied_before_post_init) {
    movement_.ApplyCreateUpdate(upd.client_receive_tick_ms);
  }

  Presentation().RefreshDisplayInfoScale(true);
  Presentation().RefreshActiveDisplayRuntimeState();
  (void)Presentation().InitDisplayCollisionBounds(true, true);
  RefreshSceneEnvironmentCache(upd.client_receive_tick_ms);

  if (!base_post_init_complete) {
    CGObject_C::FinalizeCreateUpdate(upd);
  }

  if (!upd.movement_applied_before_post_init) {
    Animation().UpdatePendingFallAnimation(previous_movement_flags,
                                           GetMovementInfo().flags);
    Animation().HandleMovementAnimation(previous_movement_flags,
                                        GetMovementInfo().flags);
  }

  Animation().RequestStandSelectorRefresh();

  if (State().IsDead() && loot_.CorpseReadyTick() == 0) {
    loot_.MarkCorpseReadyNow(upd.client_receive_tick_ms != 0
                                       ? upd.client_receive_tick_ms
                                       : openwow::core::GameClock::GetTickCount32());
  }
}

std::vector<std::uint16_t> CGUnit_C::ApplyValuesUpdate(const ValuesUpdate &upd) {
  const auto previous_mount_display = Mount().DisplayId(*this);
  auto updated_fields = CGObject_C::ApplyValuesUpdate(upd);

  if (HasUpdatedField(updated_fields, UNIT_FIELD_MOUNTDISPLAYID) &&
      previous_mount_display != Mount().DisplayId(*this)) {
    mount_.SetPendingDisplayChange(Mount().DisplayId(*this));
  }
  if (HasUpdatedField(updated_fields, UNIT_FIELD_DISPLAYID)) {
    Presentation().RefreshActiveDisplayRuntimeState();
    (void)Presentation().InitDisplayCollisionBounds(true, false);
    SpellVisuals().UpdateObjectEffect();
  }
  if (predicted_power_.IsActive()) {
    predicted_power_.SyncFromUpdatedFields(*this, updated_fields);
  }
  return updated_fields;
}

bool CGUnit_C::ApplyMovementUpdate(const MovementOnlyUpdate &upd) {
  return movement_.ApplyMovementUpdate(upd);
}

bool CGUnit_C::CanBeTransportParent() const { return true; }

std::uint32_t CGUnit_C::UpdateOverlayModel() {
  constexpr std::uint32_t kCreatureTypeFlagInteractWhileDead = 0x00000080u;
  if (State().GetHealth() == 0u) {
    const auto *const objects = object_manager();
    const auto *const creature_template =
        objects != nullptr && GetEntry() != 0u
            ? objects->query_cache().GetCreatureTemplate(GetEntry())
            : nullptr;
    if (creature_template == nullptr ||
        (creature_template->type_flags & kCreatureTypeFlagInteractWhileDead) ==
            0u) {
      return 0u;
    }
  }
  return CGObject_C::UpdateOverlayModel();
}

std::uint32_t CGUnit_C::GetHealth() const { return State().GetHealth(); }
std::uint32_t CGUnit_C::GetMaxHealth() const { return State().GetMaxHealth(); }
std::uint32_t CGUnit_C::GetLevel() const { return State().GetLevel(); }
std::uint32_t CGUnit_C::GetFactionTemplate() const { return State().GetFactionTemplate(); }
ObjectGuid CGUnit_C::GetTransportGUID() const { return GetMovementInfo().transport.guid; }

void CGUnit_C::OnDestroyEffectNode(
    const WorldSession&, const UnitSpellVisualRuntime::AttachedEffectNode&) {}

void CGUnit_C::ResetMatchingSpellVisualNodes(
    const WorldSession& session, const std::uint32_t spell_id,
    const std::uint32_t visual_kit_param) {
  SpellVisuals().ResetMatchingNodes(session, spell_id, visual_kit_param);
}

}

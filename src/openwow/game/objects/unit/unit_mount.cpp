#include "openwow/game/objects/cgunit.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/core/storm_intrusive_list.h"
#include "openwow/game/missile_node.h"
#include "openwow/game/spell_visual_system.h"
#include "openwow/game/object_effect_system.h"
#include "openwow/game/objects/unit/unit_spell_visual_runtime.h"
#include "openwow/render/m2/m2_system.h"
#include "openwow/ui/game/script_event_dispatch.h"

#include <cstddef>
#include <cstring>

namespace openwow::game {

UnitMountComponent &CGUnit_C::Mount() noexcept { return mount_; }

const UnitMountComponent &CGUnit_C::Mount() const noexcept { return mount_; }

bool UnitMountComponent::IsMounted(const CGUnit_C &owner) const {
  return DisplayId(owner) != 0u;
}

std::uint32_t UnitMountComponent::DisplayId(const CGUnit_C &owner) const {
  return owner.GetUInt32(UNIT_FIELD_MOUNTDISPLAYID);
}

bool UnitMountComponent::IsMountedStateActive(const CGUnit_C &owner) const {
  constexpr std::uint32_t kMountedStateSuppressedSpellStateFlag = 0x10000000u;
  return static_cast<std::int32_t>(CachedDisplayForSpell()) > 0 &&
         (owner.State().GetSpellStateFlags() & kMountedStateSuppressedSpellStateFlag) == 0u;
}

bool UnitMountComponent::HasMountedDisplay(const CGUnit_C &owner) const {
  constexpr std::uint32_t kMountedDisplayAnimationFlag = 0x80u;
  const auto animation_id = owner.Animation().GetCurrentAnimationId();
  if (!animation_id.has_value()) {
    return false;
  }
  const auto *const dbc = owner.dbc_loader();
  if (dbc == nullptr) {
    return false;
  }
  const auto *const animation = dbc->animation_data().LookupEntry(*animation_id);
  return animation != nullptr &&
         (animation->flags & kMountedDisplayAnimationFlag) != 0u;
}

bool UnitMountComponent::HasCompletedTransition() const {
  return TransitionHandle().IsValid() &&
         MountTransitionObject_IsTransitionComplete(TransitionHandle());
}

void UnitMountComponent::CompleteTransition(CGUnit_C &owner,
                                            const WorldSession &session) {
  if (TransitionNode() != nullptr) {
    ApplyDisplayChange(owner, session, TransitionNode()->GetCreatureDisplayId());
  }
  ClearTransitionData();
}

void UnitMountComponent::SetPendingTransition(
    const std::uint32_t spell_id,
    const std::uint32_t cached_mount_display) noexcept {
  SetCachedDisplayForSpell(cached_mount_display);
  SetPendingTransitionSpellId(spell_id);
}

void UnitMountComponent::HandleDismountPacket(CGUnit_C &owner) {
  if (CachedDisplayForSpell() == 0u) {
    return;
  }
  const auto *const vehicle_data =
      static_cast<const std::byte *>(owner.Vehicle().GetVehicleData());
  if (vehicle_data != nullptr) {
    constexpr std::size_t kVehiclePassengerListHeadNodeOffset = 376u;
    std::uintptr_t head_node = 0u;
    std::memcpy(&head_node,
                vehicle_data + kVehiclePassengerListHeadNodeOffset,
                sizeof(head_node));
    if ((head_node & core::kStormIntrusiveSentinelBit<std::uintptr_t>) == 0u &&
        head_node != 0u) {
      owner.State().AddSpellStateFlags(CGUnit_C::kSpellStateSuppressMountFootprint);
      return;
    }
  }
  owner.State().ClearSpellStateFlags(CGUnit_C::kSpellStateSuppressMountFootprint);
  Dismount(owner, true);
  SetCachedDisplayForSpell(0u);
  owner.UpdateOverlayModel();
  owner.Presentation().RefreshModelBoundsAndEffectsForced();
  owner.SpellVisuals().UpdateObjectEffect();
}

void UnitMountComponent::Dismount(CGUnit_C &owner,
                                  const bool update_spell_visuals) {
  if (CachedDisplayForSpell() == 0u) {
    return;
  }
  SetModelDefaultAnimationId(std::nullopt);
  owner.State().ClearSpellStateFlags(0x00882004u);
  owner.Presentation().RefreshActiveDisplayRuntimeState();
  static_cast<void>(update_spell_visuals);
  owner.State().ClearSpellStateFlags(CGUnit_C::kSpellStateSuppressMountFootprint);
}

void UnitMountComponent::ApplyDisplayChange(
    CGUnit_C &owner, const WorldSession &session,
    const std::uint32_t mount_display_id) {
  if (CachedDisplayForSpell() == mount_display_id) {
    return;
  }
  if (mount_display_id == 0u && TransitionHandle().IsValid() &&
      !MountTransitionObject_IsTransitionComplete(TransitionHandle())) {
    owner.State().AddSpellStateFlags(CGUnit_C::kSpellStateSuppressMountFootprint);
    return;
  }
  owner.State().ClearSpellStateFlags(CGUnit_C::kSpellStateSuppressMountFootprint);
  const auto previous_display = CachedDisplayForSpell();
  if (previous_display != 0u) {
    Dismount(owner, mount_display_id == 0u);
  }
  SetCachedDisplayForSpell(mount_display_id);
  owner.Presentation().RefreshActiveDisplayRuntimeState();
  if (mount_display_id != 0u && previous_display == 0u) {
    AddHardcodedOneShotEffect(session, owner, HardcodedEffectId::kMountPoof);
  }

  owner.Animation().RefreshSelectedStandAnimation(session, 0u, ~0u);
  owner.Presentation().RefreshModelBoundsAndEffects();
  ui::game::ScriptEventDispatch::Get().FireUnitModel(owner.GetGuid().GetRawValue());
  owner.SpellVisuals().UpdateObjectEffect();
}

void UnitMountComponent::ReleaseOverlayM2Instance(CGUnit_C &owner) {
  if (OverlayM2InstanceId() == 0u) {
    return;
  }
  auto &system = *owner.m2_system();
  const auto instance_id = OverlayM2InstanceId();
  auto status = render::m2::M2ResultStatus::kReady;
  status = render::m2::MergeM2ResultStatus(
      status, system.ClearAnimationChangedCallback(instance_id));
  status = render::m2::MergeM2ResultStatus(
      status, system.ClearTriggeredEventCallback(instance_id));
  status = render::m2::MergeM2ResultStatus(
      status, system.ClearReplacementColors(instance_id));
  status = render::m2::MergeM2ResultStatus(
      status, system.DestroyInstance(instance_id));
  if (render::m2::IsTerminalM2ResultStatus(status)) {
    SetModelDefaultAnimationId(std::nullopt);
  }
  SetOverlayM2InstanceId(0u);
  SetModelDefaultAnimationId(std::nullopt);
}

}

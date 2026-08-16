#include "openwow/game/objects/unit/unit_animation_runtime.h"

#include "openwow/game/objects/cgunit.h"

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/audio/effects/impact_sounds.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/core/display_settings.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/game/activities/dance/application/unit_dance_state.h"
#include "openwow/game/barber_shop.h"
#include "openwow/game/c_input_control.h"
#include "openwow/game/ceffect_c.h"
#include "openwow/game/character_animation.h"
#include "openwow/game/creature_sound.h"
#include "openwow/game/character_component_backend.h"
#include "openwow/game/game_misc_utils.h"
#include "openwow/game/inebriation.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/interaction_sender.h"
#include "openwow/game/object_effect_system.h"
#include "openwow/game/unit_animation_resolution.h"
#include "openwow/game/unit_combat.h"
#include "openwow/game/combat_sounds.h"
#include "openwow/game/unit_sound_dispatch.h"
#include "openwow/game/vehicle.h"
#include "openwow/game/vehicle_passenger.h"
#include "openwow/game/world_session.h"
#include "openwow/game/world_environment_state.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/objects/cggameobject.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/data/model/m2_model.h"
#include "openwow/render/m2/m2_system.h"
#include "openwow/render/models/animation/animation_state.h"
#include "openwow/render/scene/world_frame.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/game_ui_core.h"
#include "openwow/world/camera/world_camera.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>

namespace openwow::game {

namespace {

constexpr std::uint32_t kInvalidUnitAnimationId = 506u;
constexpr std::uint32_t kDirectionalMovementMask = 0x0Fu;
constexpr std::uint32_t kReadySheatheRangedTypeMask = 0x88u;

constexpr std::uint32_t kEmoteInternalFlagAnimationBehavior39 = 0x00000004u;
constexpr std::uint32_t kEmoteInternalFlagAnimationBehavior127 = 0x00000008u;
constexpr std::uint32_t kEmoteInternalFlagAnimationBehavior192 = 0x00040000u;
constexpr std::uint32_t kEmoteInternalFlagAnimationBehavior121 = 0x00080000u;
constexpr std::uint32_t kEmoteInternalFlagAnimationBehavior201 = 0x00400000u;
constexpr std::uint32_t kEmoteInternalFlagAnimationBehavior458 = 0x02000000u;

constexpr std::uint32_t kEmoteInternalFlagInFlightSubmitFilterMask =
    kEmoteInternalFlagAnimationBehavior192 |
    kEmoteInternalFlagAnimationBehavior121;
constexpr std::uint32_t kInvalidAnimationBehaviorId = 506u;

constexpr std::uint32_t kJumpStartBehaviorId = 0x25u;
constexpr std::uint32_t kJumpEndBehaviorId = 0x27u;
constexpr std::uint32_t kJumpLandRunBehaviorId = 0xBBu;
constexpr std::uint32_t kKnockdownBehaviorId = 0x79u;
constexpr std::uint32_t kBirthBehaviorId = 0x7Fu;
constexpr std::uint32_t kLiftOffBehaviorId = 0xC0u;
constexpr std::uint32_t kLandBehaviorId = 0xC8u;
constexpr std::uint32_t kSubmergeBehaviorId = 0xC9u;
constexpr std::uint32_t kSubmergedBehaviorId = 0xCAu;
constexpr std::uint32_t kToFlyBehaviorId = 0x1CAu;
constexpr std::uint32_t kToGroundBehaviorId = 0x1CCu;
constexpr std::uint32_t kSitGroundUpBehaviorId = 0x62u;

constexpr std::uint32_t kWoundIdleStandingMovementMask = 0x02E0100Fu;

constexpr std::uint16_t kRiderMountAnimationId = 0x5Bu;

constexpr std::uint32_t kUnitFlags2FeignDeath = 0x00000001u;

constexpr std::int32_t kSheatheStateRanged = 2;
constexpr std::int32_t kFallAnimationId = 40;
constexpr std::uint8_t kStandStateDead = 7u;
constexpr std::uint16_t kHoverStandAnimationId = 193u;
constexpr std::uint16_t kDeadTransitionAnimationId = 466u;
constexpr std::uint16_t kAlternateDeadTransitionAnimationId = 131u;
constexpr std::uint16_t kFlightTransitionTakeoffAnimationId = 37u;
constexpr std::uint16_t kFlightTransitionLandingAnimationId = 187u;

constexpr std::uint16_t kLootAnimationId = 50u;
constexpr std::uint16_t kLootHoldAnimationId = 188u;
constexpr std::uint16_t kLootUpAnimationId = 189u;

constexpr std::uint32_t kUnitFlagLooting = 0x00000400u;

constexpr std::uint32_t kUnitFlagLootPoseBlocked = 0x10000000u;
constexpr std::uint32_t kMovementOpcodeStandRefreshUnitFlagMask = 0x00C0100Fu;

constexpr std::uint32_t kLootStandReplayFlag = 0x00000008u;
constexpr std::uint32_t kLootStandPassiveClaimMask = 0xFFFFFFF0u;

constexpr std::uint32_t kRangedHoldStandReplayFlag = 0x00000080u;
constexpr std::uint32_t kRangedHoldStandPassiveClaimMask = 0xFFFFFF00u;

constexpr std::uint32_t kEmoteInternalFlagRangedHoldSuppressed = 0x00004000u;

constexpr std::uint32_t kEmoteInternalFlagAirborneDeathSubmit = 0x04000000u;
constexpr std::uint32_t kAirborneDeathSubmitMovementMask =
    kMoveFlagFlying | kMoveFlagSwimming | kMoveFlagFalling;

constexpr std::uint8_t kAnimationTierFly = 3u;

constexpr std::uint8_t kRangedVisibleWeaponSlot = 2u;

constexpr std::uint32_t kItemSubclassWeaponBow = 2u;
constexpr std::uint32_t kItemSubclassWeaponGun = 3u;
constexpr std::uint32_t kItemSubclassWeaponThrown = 16u;
constexpr std::uint32_t kItemSubclassWeaponCrossbow = 18u;
constexpr std::uint32_t kItemSubclassWeaponWand = 19u;

constexpr std::uint32_t kItemSubclassWeaponDagger = 15u;

constexpr std::uint16_t kAttack1HPierceAnimationId = 85u;
constexpr std::uint16_t kAttackOffAnimationId = 87u;
constexpr std::uint16_t kAttackOffPierceAnimationId = 88u;
constexpr std::uint16_t kAttackUnarmedOffAnimationId = 117u;

constexpr std::uint32_t kHitInfoAffectsVictim = 0x00000002u;
constexpr std::uint32_t kHitInfoOffhandSwing = 0x00000004u;
constexpr std::uint32_t kHitInfoCriticalHit = 0x00000200u;
constexpr std::uint32_t kHitInfoNoAnimation = 0x00040000u;

constexpr std::uint8_t kVictimStateDodge = 2u;
constexpr std::uint8_t kVictimStateParry = 3u;

constexpr std::uint8_t kVictimStateDeflect = 8u;

constexpr std::uint16_t kLoadBowAnimationId = 105u;
constexpr std::uint16_t kLoadRifleAnimationId = 106u;
constexpr std::uint16_t kHoldThrownAnimationId = 111u;
constexpr std::uint16_t kLoadThrownAnimationId = 112u;
constexpr std::uint32_t kEmoteInternalFlagCachedTargetMask = 0x00000060u;
constexpr std::uint32_t kCachedTargetStandSelectorBit = 0x00000020u;
constexpr std::uint32_t kCachedTargetStandPassiveMask = 0xFFFFFFC0u;
constexpr std::uint32_t kEmoteInternalFlagStandSelectorTransitionMask = 0x00001800u;
constexpr std::uint32_t kEmoteInternalFlagFlightTransitionLock = 0x00800000u;

constexpr std::uint32_t kLiftOffAnimationId = 192u;
constexpr std::uint32_t kLandAnimationId = 200u;
constexpr std::uint32_t kStandSelectorBlockedEmoteInternalFlagMask =
    kEmoteInternalFlagAnimationBehavior39 |
    kEmoteInternalFlagAnimationBehavior127 |
    kEmoteInternalFlagAnimationBehavior192;
constexpr std::uint32_t kStandSelectorEmoteAnimFlag = 0x0200u;

constexpr std::uint32_t kEmoteRowSkipForNpcInteractionTargetFlag = 0x2000u;

constexpr std::uint32_t kEmoteStateResolveEnabledFlag = 0x20u;
constexpr std::uint32_t kEmoteStateResolveModelGatedFlag = 0x40u;

constexpr std::uint32_t kBaseAnimationStateMask = 0x70u;
constexpr std::uint32_t kStandAnimationCustomRequestId = 15u;
constexpr std::uint8_t kTransitionStandStateId = 9u;

constexpr std::uint32_t kUnitFieldFlagsInCombat = 0x00000800u;

constexpr std::uint8_t kStandStateSitGround = 1u;
constexpr std::uint8_t kStandStateSleep = 3u;
constexpr std::uint8_t kStandStateSitChairLow = 4u;
constexpr std::uint8_t kStandStateSitChairMed = 5u;
constexpr std::uint8_t kStandStateSitChairHigh = 6u;
constexpr std::uint8_t kStandStateKneel = 8u;
constexpr std::uint16_t kEmoteDanceAnimationId = 69u;
constexpr std::uint16_t kSitGroundAnimationId = 97u;
constexpr std::uint16_t kSitGroundUpAnimationId = 98u;
constexpr std::uint16_t kSleepAnimationId = 100u;
constexpr std::uint16_t kSleepUpAnimationId = 101u;
constexpr std::uint16_t kSitChairLowAnimationId = 102u;
constexpr std::uint16_t kSitChairMedAnimationId = 103u;
constexpr std::uint16_t kSitChairHighAnimationId = 104u;
constexpr std::uint16_t kHoldBowAnimationId = 109u;
constexpr std::uint16_t kHoldRifleAnimationId = 110u;
constexpr std::uint16_t kKneelLoopAnimationId = 115u;
constexpr std::uint16_t kKneelEndAnimationId = 116u;
constexpr std::uint16_t kDrownedAnimationId = 132u;
constexpr std::uint16_t kFishingLoopAnimationId = 134u;
constexpr std::uint16_t kSubmergeAnimationId = 201u;
constexpr std::uint16_t kSubmergedAnimationId = 202u;
constexpr std::uint16_t kSettleAnimationId = 464u;
constexpr std::uint16_t kDeathLoopAnimationId = 467u;
constexpr std::uint16_t kDeathEndAnimationId = 468u;
constexpr std::uint16_t kDeathEndHoldAnimationId = 472u;

constexpr std::uint16_t kStandTransitionEntryAnimationId = kSubmergeAnimationId;
constexpr std::uint16_t kStandTransitionLoopAnimationId = kSubmergedAnimationId;
constexpr std::uint16_t kStandTransitionExitAnimationId = 224u;
constexpr std::uint16_t kStandTransitionExitFallbackAnimationId = 127u;
constexpr std::uint32_t kEmoteInternalFlagUseSpellVisualStartAnimation = 0x00010000u;

constexpr std::uint32_t kEmoteInternalFlagEmoteQueueDrained = 0x00008000u;

constexpr std::size_t kEmoteQueueSlotCount = 4u;
constexpr std::uint32_t kEmoteQueueIdleSentinel = 0xFFFFFFFFu;

constexpr std::uint32_t kEmoteQueueInterEmoteDelayMs = 500u;

[[nodiscard]] constexpr std::size_t EmoteQueueDelayWord(std::size_t slot) noexcept {
  return slot * 2u;
}
[[nodiscard]] constexpr std::size_t EmoteQueueEmoteWord(std::size_t slot) noexcept {
  return slot * 2u + 1u;
}
constexpr std::uint32_t kSpellVisualKitFlagDirectSequence = 0x00000004u;
constexpr std::uint32_t kSpellVisualKitFlagBypassAliasResolution = 0x00000008u;
constexpr std::uint32_t kSpellVisualKitFlagSkipWeaponPoseAdjustment = 0x00000010u;
constexpr std::uint32_t kSpellVisualKitFlagReplayOnSelectorFlag = 0x00000040u;
constexpr std::uint32_t kSpellVisualKitFlagUnknown0x100 = 0x00000100u;

constexpr std::uint32_t kEmoteAnimationFlagDirectSequence = 0x00000002u;
constexpr std::uint32_t kEmoteAnimationFlagBypassAliasResolution = 0x00000004u;
constexpr std::uint32_t kEmoteAnimationFlagSkipWeaponPoseAdjustment = 0x00000008u;
constexpr std::uint32_t kEmoteAnimationFlagSuppressSheatheUpdate = 0x00000020u;
constexpr std::uint32_t kStandStateTransitionReplayFlag = 0x00000002u;
constexpr std::uint32_t kStandSelectorReplayFlag = 0x00000010u;
constexpr std::uint32_t kStandStateTransitionPassiveClaimMask = 0x00000003u;
constexpr std::uint32_t kStandSelectorPassiveClaimMask = 0x0000001Fu;

constexpr std::uint32_t kStandSelectorLocomotionClaimFlag = 0x00000004u;
constexpr std::uint32_t kStandSelectorLocomotionPassiveClaimMask = 0x00000007u;

constexpr std::uint32_t kStandSelectorTurnInPlaceClaimFlag = 0x00000040u;

constexpr float kLocomotionSprintSpeedThreshold = 11.0f;

constexpr std::uint32_t kUnitVisFlagCreep = 0x02u;
constexpr std::uint16_t kStealthStandAnimationId = 120u;
constexpr std::uint32_t kSettleBehaviorId = 0x1D0u;
constexpr std::uint32_t kWeaponContactPrimaryAttachmentLookup = 35u;
constexpr std::uint32_t kWeaponContactSecondaryAttachmentLookup = 36u;
std::array<std::uint32_t, 5> g_speech_emote_slots{};

[[nodiscard]] bool IsValidUnitAnimationId(const std::uint32_t animation_id) {
  return animation_id < kInvalidUnitAnimationId;
}

[[nodiscard]] bool IsUsableSpellVisualAnimationId(
    const std::int32_t animation_id) {

  return animation_id >= 0 &&
         IsValidUnitAnimationId(static_cast<std::uint32_t>(animation_id));
}

[[nodiscard]] const data::dbc::AnimationDataEntry *LookupAnimationDataEntry(
    const CGUnit_C &unit, const std::uint32_t animation_id) {
  if (!IsValidUnitAnimationId(animation_id)) {
    return nullptr;
  }
  const auto *const dbc = unit.dbc_loader();
  return dbc != nullptr ? dbc->animation_data().LookupEntry(animation_id) : nullptr;
}

[[nodiscard]] std::uint32_t ResolveAnimationBehaviorId(
    const CGUnit_C &unit, const std::uint32_t animation_id) {
  const auto *const entry = LookupAnimationDataEntry(unit, animation_id);
  return entry != nullptr ? entry->behavior_id : kInvalidAnimationBehaviorId;
}

[[nodiscard]] bool IsPrimaryWeaponContactAnimationBehavior(
    const std::uint32_t behavior_id) noexcept {

  return behavior_id == 105u || behavior_id == 109u || behavior_id == 46u;
}

[[nodiscard]] bool IsSecondaryWeaponContactAnimationBehavior(
    const std::uint32_t behavior_id) noexcept {

  return behavior_id == 106u || behavior_id == 110u || behavior_id == 49u;
}

[[nodiscard]] std::array<float, 3> ResolveWeaponContactPosition(
    const CGUnit_C& unit, const float* const event_position) {
  std::array<float, 3> resolved{};
  if (event_position == nullptr) {
    return resolved;
  }
  std::copy_n(event_position, resolved.size(), resolved.begin());

  const std::uint32_t primary_instance = unit.GetPrimaryM2InstanceId();
  auto* const m2 = unit.m2_system();
  if (primary_instance == 0u || m2 == nullptr) {
    return resolved;
  }

  const auto animation = m2->QueryInstanceAnimationInfo(primary_instance);
  if (animation.status != render::m2::M2ResultStatus::kReady) {
    return resolved;
  }

  const std::uint32_t behavior_id = ResolveAnimationBehaviorId(
      unit, animation.info.resolved_animation_id);
  const auto try_attachment = [&](const std::uint32_t attachment_lookup) {
    const auto attachment =
        m2->QueryAttachmentPosition(primary_instance, attachment_lookup);
    if (attachment.status == render::m2::M2ResultStatus::kReady) {
      resolved = attachment.position;
      return true;
    }
    return false;
  };

  if (IsPrimaryWeaponContactAnimationBehavior(behavior_id) &&
      try_attachment(kWeaponContactPrimaryAttachmentLookup)) {
    return resolved;
  }
  if (IsSecondaryWeaponContactAnimationBehavior(behavior_id)) {
    (void)try_attachment(kWeaponContactSecondaryAttachmentLookup);
  }
  return resolved;
}

[[nodiscard]] bool PrimaryM2ModelContainsAnimation(
    const CGUnit_C& unit, const std::uint32_t animation_id) {
  const std::uint32_t instance_id = unit.GetPrimaryM2InstanceId();
  auto* const m2_system = unit.m2_system();
  if (instance_id == 0u || m2_system == nullptr) {
    return false;
  }
  const auto instance_model = m2_system->QueryInstanceModel(instance_id);
  return instance_model.status == render::m2::M2ResultStatus::kReady &&
         m2_system->ModelContainsAnimation(instance_model.model_id,
                                           animation_id);
}

[[nodiscard]] bool IsPrimaryM2ModelStreamedFor(const CGUnit_C& unit) {
  const std::uint32_t instance_id = unit.GetPrimaryM2InstanceId();
  auto* const m2_system = unit.m2_system();
  if (instance_id == 0u || m2_system == nullptr) {
    return false;
  }
  return m2_system->QueryInstanceModel(instance_id).status ==
         render::m2::M2ResultStatus::kReady;
}

[[nodiscard]] bool IsDeadAnimationFamily(const std::uint32_t animation_id) {
  switch (animation_id) {
  case 1u: case 6u: case 131u: case 132u: case 466u: case 467u: case 468u: case 472u:
    return true;
  default:
    return false;
  }
}

[[nodiscard]] constexpr bool IsAirborneDeathSubmitBehavior(
    const std::uint32_t behavior_id) noexcept {
  return behavior_id == 0x1D2u || behavior_id == 0x1D3u ||
         behavior_id == 0x1D4u || behavior_id == 0x1D8u;
}

[[nodiscard]] bool IsMovementStandPreservingBehaviorId(const std::uint32_t behavior_id) {
  switch (behavior_id) {
  case 37u: case 38u: case 39u: case 40u: case 467u:
    return true;
  default:
    return false;
  }
}

[[nodiscard]] bool IsStandSelectorBlockedAnimationBehavior(const std::uint32_t behavior_id) {
  switch (behavior_id) {
  case 2u: case 32u: case 33u: case 46u: case 49u: case 53u: case 54u:
  case 60u: case 61u: case 62u: case 63u: case 64u: case 65u: case 66u:
  case 67u: case 68u: case 69u: case 70u: case 71u: case 72u: case 73u:
  case 74u: case 76u: case 77u: case 78u: case 80u: case 81u: case 82u:
  case 83u: case 84u: case 105u: case 106u: case 107u: case 109u: case 110u:
  case 111u: case 112u: case 113u: case 136u: case 137u: case 138u:
  case 178u: case 185u: case 186u: case 195u:
    return true;
  default:
    return false;
  }
}

[[nodiscard]] bool IsLoopingCombatAnimationBehavior(const std::uint32_t behavior_id) {
  switch (behavior_id) {
  case 10u: case 16u: case 17u: case 18u: case 19u: case 20u: case 21u:
  case 22u: case 23u: case 24u: case 30u: case 36u: case 57u: case 58u:
  case 59u: case 85u: case 86u: case 87u: case 88u: case 95u: case 117u:
  case 118u: case 170u: case 171u: case 172u: case 173u: case 174u:
  case 175u: case 176u: case 177u: case 178u: case 179u: case 212u:
    return true;
  default:
    return false;
  }
}

[[nodiscard]] bool MatchesSplineAwareMovementGate(
    const MovementInfo &movement_info, const openwow::world::MoveSpline *const spline,
    const std::uint32_t fallback_movement_flags) {
  constexpr std::uint16_t kMovementFlag2UnknownBit4 = 0x0004u;
  const std::uint32_t spline_flags = spline != nullptr ? spline->GetSplineFlags() : 0u;
  const bool has_non_exempt_falling_spline =
      spline != nullptr && SplineFlag::HasNonExemptFlag(spline_flags, SplineFlag::kFalling);
  if (!has_non_exempt_falling_spline) {
    if (spline != nullptr && SplineFlag::HasNonExemptFlag(spline_flags, SplineFlag::kFlying)) {
      return true;
    }
    if ((movement_info.flags2 & kMovementFlag2UnknownBit4) != 0u ||
        (movement_info.flags & kMoveFlagDisableGravity) != 0u) {
      return true;
    }
  }
  return (movement_info.flags & fallback_movement_flags) != 0u;
}

[[nodiscard]] const openwow::world::MoveSpline *GetActiveMovementSpline(
    const WorldSession &session, const CGUnit_C &unit) {
  return session.movement_spline_mgr().GetSpline(unit.GetGuid().GetRawValue());
}

[[nodiscard]] std::optional<float> ResolveDeadTransitionSupportSurfaceHeight(
    const CGUnit_C &unit) {
  const auto *const environment = unit.world_environment();
  if (environment == nullptr) {
    return std::nullopt;
  }
  const auto position = unit.GetPosition();
  return environment->QuerySupportSurfaceHeight(
      position.x, position.y, position.z);
}

[[nodiscard]] std::uint32_t MapSpellVisualKitFlagsToEmoteAnimationFlags(
    const std::uint32_t flags) {
  std::uint32_t animation_flags = 0u;
  if ((flags & kSpellVisualKitFlagDirectSequence) != 0u) animation_flags |= kEmoteAnimationFlagDirectSequence;
  if ((flags & kSpellVisualKitFlagBypassAliasResolution) != 0u) animation_flags |= kEmoteAnimationFlagBypassAliasResolution;
  if ((flags & kSpellVisualKitFlagSkipWeaponPoseAdjustment) != 0u) animation_flags |= kEmoteAnimationFlagSkipWeaponPoseAdjustment;
  if ((flags & kSpellVisualKitFlagUnknown0x100) != 0u) animation_flags |= kEmoteAnimationFlagSuppressSheatheUpdate;
  return animation_flags;
}

[[nodiscard]] bool SpellHasPositiveCastDuration(
    const CGUnit_C &unit, const WorldSession &session,
    const data::dbc::SpellEntry *const spell) {
  if (spell == nullptr) {
    return false;
  }
  if (const auto *const player = dynamic_cast<const CGPlayer_C *>(&unit);
      player != nullptr) {
    return player->CalcSpellDuration(session, spell) > 0;
  }
  constexpr std::uint32_t kSpellAttributeUsesRangedSlot = 0x00000002u;
  if ((spell->attributes & kSpellAttributeUsesRangedSlot) != 0u) {
    return true;
  }
  const auto *const dbc = unit.dbc_loader();
  if (dbc == nullptr) {
    return false;
  }
  const auto *const cast_times =
      dbc->spell_cast_times().LookupEntry(spell->casting_time_index);
  if (cast_times == nullptr) {
    return false;
  }
  return cast_times->base_cast_time > 0 || cast_times->minimum > 0;
}

[[nodiscard]] constexpr bool EmoteAnimationFlagsBypassAliasResolution(
    const std::uint32_t animation_flags) noexcept {
  return (animation_flags & (kEmoteAnimationFlagDirectSequence |
                             kEmoteAnimationFlagBypassAliasResolution)) != 0u;
}

[[nodiscard]] bool ShouldPassivelyClaimStandSelector(const std::uint32_t selector_flags) {
  return (selector_flags & ~kStandSelectorPassiveClaimMask) == 0u;
}

[[nodiscard]] bool SheatheTypeUsesRangedReadyAnimation(
    const std::uint32_t sheathe_type) {
  return sheathe_type < 32u &&
         ((1u << sheathe_type) & kReadySheatheRangedTypeMask) != 0u;
}

[[nodiscard]] bool IsPrimaryRangedInventoryType(
    const std::uint32_t inventory_type) {
  return inventory_type ==
             static_cast<std::uint32_t>(InventoryType::Thrown) ||
         inventory_type ==
             static_cast<std::uint32_t>(InventoryType::RangedRight);
}

[[nodiscard]] std::optional<VisibleItemTemplateMetadata>
GetVisibleWeaponMetadataForAnimation(const CGUnit_C& unit,
                                     const std::uint8_t weapon_slot) {
  const auto* player = dynamic_cast<const CGPlayer_C*>(&unit);
  if (player != nullptr) {
    return player->GetVisibleWeaponSlotMetadata(weapon_slot,
                                                true);
  }

  const auto item_entry = unit.State().GetVirtualItemSlotEntry(weapon_slot);
  const auto* dbc = unit.dbc_loader();
  if (item_entry == 0u || dbc == nullptr) {
    return std::nullopt;
  }
  const auto* const item = dbc->item().LookupEntry(item_entry);
  if (item == nullptr) {
    return std::nullopt;
  }
  return VisibleItemTemplateMetadata{
      .entry = item->id,
      .item_class = item->class_id,
      .subclass = item->subclass_id,
      .sound_override = item->sound_override_subclass,
      .material = static_cast<std::int32_t>(item->material),
      .inventory_type = item->inventory_type,
      .sheath = item->sheathe_type,
      .display_id = item->display_info_id,
  };
}

[[nodiscard]] bool IsUnarmedCombatBehavior(
    const std::uint32_t behavior_id) {
  switch (behavior_id) {
  case 16u:
  case 20u:
  case 25u:
  case 117u:
  case 118u:
    return true;
  default:
    return false;
  }
}

void SetUpdatedFieldValue(UpdateFieldValues &fields, const std::uint16_t index,
                          const std::uint32_t value) {
  const auto block = static_cast<std::size_t>(index / 32u);
  const auto bit = static_cast<std::uint32_t>(index % 32u);
  fields.bitmask[block] |= 1u << bit;
  fields.values.push_back(value);
}

}

[[nodiscard]] bool IsTurnInPlaceDeclinedBehavior(
    const std::uint32_t behavior_id) {
  return (behavior_id >= 0x3Cu && behavior_id <= 0x4Au) ||
         (behavior_id >= 0x4Cu && behavior_id <= 0x4Eu) ||
         (behavior_id >= 0x50u && behavior_id <= 0x54u) ||
         behavior_id == 0x71u ||
         (behavior_id >= 0x88u && behavior_id <= 0x8Au) ||
         behavior_id == 0xB2u ||
         behavior_id == 0xB9u || behavior_id == 0xBAu ||
         behavior_id == 0xC3u;
}

CharacterLocomotionState BuildLocomotionState(const CGUnit_C &unit,
                                              const std::uint32_t movement_flags,
                                              const bool dead,
                                              const std::uint32_t emote_internal_flags) {
  bool turn_in_place_declined = false;

  if ((movement_flags & (kMoveFlagSwimming | kMoveFlagFlying)) != 0u) {
    turn_in_place_declined = true;
  } else if (const auto current_animation =
                 unit.Animation().GetCurrentAnimationId();
             current_animation.has_value()) {
    const auto *const dbc = unit.dbc_loader();
    const auto *const row =
        dbc != nullptr
            ? dbc->animation_data().LookupEntry(*current_animation)
            : nullptr;
    if (row != nullptr && IsTurnInPlaceDeclinedBehavior(row->behavior_id)) {
      turn_in_place_declined = true;
    }
  }

  const bool directional_locomotion_suppressed =
      (emote_internal_flags & kBaseAnimationStateMask) == 0u ||
      (emote_internal_flags & kEmoteInternalFlagFlightTransitionLock) != 0u ||
      (emote_internal_flags & kEmoteInternalFlagAnimationBehavior458) != 0u;
  return CharacterLocomotionState{
      .movement_flags = movement_flags,
      .dead = dead,
      .mounted = unit.GetUInt32(UNIT_FIELD_MOUNTDISPLAYID) != 0u,
      .current_speed = unit.Movement().ComputeCurrentSpeed(),
      .walk_speed = unit.Movement().Data().GetSpeed(kSpeedWalk),
      .stealthed = (unit.State().GetVisFlags() & kUnitVisFlagCreep) != 0u,
      .flying_spline = unit.Movement().HasNonExemptFlyingSpline(),
      .directional_locomotion_suppressed = directional_locomotion_suppressed,
      .turning_left_latch =
          (emote_internal_flags &
           UnitAnimationRuntime::kEmoteFlagTurnInPlaceLeft) != 0u,
      .turning_right_latch =
          (emote_internal_flags &
           UnitAnimationRuntime::kEmoteFlagTurnInPlaceRight) != 0u,
      .turn_in_place_declined = turn_in_place_declined,
  };
}

void UnitAnimationRuntime::AssignAnimationSlotsByFlags(
    const data::dbc::DbcLoader &dbc_loader) {
  g_speech_emote_slots.fill(0u);
  const auto &entries = dbc_loader.emotes().entries();
  for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
    const auto &entry = *it;
    if ((entry.flags & 0x008u) != 0u) {
      g_speech_emote_slots[0] = entry.id;
    } else if ((entry.flags & 0x010u) != 0u) {
      g_speech_emote_slots[1] = entry.id;
    } else if ((entry.flags & 0x020u) != 0u) {
      g_speech_emote_slots[2] = entry.id;
    } else if ((entry.flags & 0x040u) != 0u) {
      g_speech_emote_slots[3] = entry.id;
    } else if ((entry.flags & 0x100u) != 0u) {
      g_speech_emote_slots[4] = entry.id;
    }
  }
}

void UnitAnimationRuntime::ClearAnimationSlots() {
  g_speech_emote_slots.fill(0u);
}

void UnitAnimationRuntime::SetAutoRepeatActive(const bool active) noexcept {
  if (active) {
    emote_internal_flags_ |= kEmoteFlagAutoRepeatActive;
  } else {
    emote_internal_flags_ &= ~kEmoteFlagAutoRepeatActive;
  }
}

bool UnitAnimationRuntime::IsAutoRepeatActive() const noexcept {
  return (emote_internal_flags_ & kEmoteFlagAutoRepeatActive) != 0u;
}

void UnitAnimationRuntime::SetChannelingActionLock(const bool locked) noexcept {
  if (locked) {
    emote_internal_flags_ |= kEmoteFlagChannelingActionLock;
  } else {
    emote_internal_flags_ &= ~kEmoteFlagChannelingActionLock;
  }
}

bool UnitAnimationRuntime::HasChannelingActionLock() const noexcept {
  return (emote_internal_flags_ & kEmoteFlagChannelingActionLock) != 0u;
}

void UnitAnimationRuntime::SetAnimationBoneAvailability(
    const bool has_bone_4, const bool has_bone_6) noexcept {
  emote_internal_flags_ &=
      ~(kEmoteFlagHasAnimationBone4 | kEmoteFlagHasAnimationBone6);
  if (has_bone_4) {
    emote_internal_flags_ |= kEmoteFlagHasAnimationBone4;
  }
  if (has_bone_6) {
    emote_internal_flags_ |= kEmoteFlagHasAnimationBone6;
  }
  animation_bone_index_ = has_bone_4 ? 4 : has_bone_6 ? 6 : -1;
}

void UnitAnimationRuntime::SetBodyYawTurnLatches(const bool left,
                                                 const bool right) noexcept {
  emote_internal_flags_ &=
      ~(kEmoteFlagTurnInPlaceLeft | kEmoteFlagTurnInPlaceRight);
  if (left) {
    emote_internal_flags_ |= kEmoteFlagTurnInPlaceLeft;
  }
  if (right) {
    emote_internal_flags_ |= kEmoteFlagTurnInPlaceRight;
  }
}

bool UnitAnimationRuntime::UpdateCachedAnimationTier(
    const std::uint8_t tier) noexcept {
  if (cached_anim_tier_ == tier) {
    return false;
  }
  cached_anim_tier_ = tier;
  return true;
}

void UnitAnimationRuntime::ResetInternalEmoteStorage() noexcept {

  emote_internal_flags_ = kConstructedEmoteInternalFlags;

  for (auto &slot : emote_slots_) {
    slot = 0u;
  }
  emote_slots_[EmoteQueueEmoteWord(0)] = kEmoteQueueIdleSentinel;
}

void UnitAnimationRuntime::ProcessGroundContactAnimationEvent(
    const WorldSession& session, const float* const position,
    const bool right_side) {
  (void)right_side;
  if (position == nullptr || owner_.State().GetPetNumber() != 0u ||
      owner_.GetMovementInfo().HasFlag(kMoveFlagHover) ||
      (owner_.State().GetVisFlags() & 0x02u) != 0u ||
      owner_.Movement().Data().GetTransportGuid() != 0u ||

      owner_.Movement().IsGhostPlayerDescriptorPair()) {
    return;
  }

  const auto* viewport = owner_.world_frame();
  if (viewport == nullptr) {
    return;
  }
  const auto camera_position = viewport->GetCameraPosition();
  const float dx = position[0] - camera_position[0];
  const float dy = position[1] - camera_position[1];
  const float dz = position[2] - camera_position[2];
  const float distance_squared = dx * dx + dy * dy + dz * dz;
  if (distance_squared > 2500.0f ||
      !owner_.Presentation().HasActiveCreatureModelData()) {
    return;
  }

  UnitSoundGroundState ground_state{};
  const bool has_ground_state =
      UnitSound_QueryGroundState(owner_, position, ground_state);
  if (has_ground_state) {
    owner_.Presentation().Footprint().SetTerrainTypeId(ground_state.terrain_type_id);
  }

  const auto* const dbc = owner_.dbc_loader();

  if (distance_squared > 625.0f ||
      owner_.GetMovementInfo().HasFlag(kMoveFlagBackward) ||
      (owner_.Presentation().ActiveCreatureModelFlags() & 0x01u) != 0u ||
      !has_ground_state ||
      dbc == nullptr) {
    return;
  }

  auto& cvars = openwow::ui::game::CVarSystem::Instance();
  static const std::string kShowFootprintParticlesCVar{
      "showfootprintparticles"};
  if (!cvars.GetCVarBool(kShowFootprintParticlesCVar)) {
    return;
  }

  const float walk_speed = owner_.Movement().Data().GetSpeed(kSpeedWalk);

  const bool uses_run_spray =
      walk_speed + walk_speed < owner_.Movement().ComputeCurrentSpeed();
  std::array<float, 3> effect_position{
      position[0], position[1], position[2]};
  std::uint32_t effect_name_id = 0u;

  if (ground_state.has_liquid_surface && ground_state.liquid_type_id != 0u) {
    const float liquid_depth = ground_state.liquid_surface_z - owner_.GetZ();
    if (liquid_depth < 0.0f ||
        !(liquid_depth < owner_.Presentation().CollisionHeight() * 0.5f)) {
      return;
    }
    effect_position[2] += liquid_depth;
    effect_name_id = HardcodedEffectIdTable::GetEffectId(
        uses_run_spray ? HardcodedEffectId::kFootstepWaterRunSpray
                       : HardcodedEffectId::kFootstepWaterWalkSpray);
  } else {
    const auto* const terrain =
        dbc->terrain_type().LookupEntry(ground_state.terrain_type_id);
    if (terrain == nullptr) {
      return;
    }
    effect_name_id = uses_run_spray ? terrain->footstep_spray_run
                                    : terrain->footstep_spray_walk;
  }

  const auto* const effect_name =
      dbc->spell_visual_effect_name().LookupEntry(effect_name_id);
  if (effect_name == nullptr) {
    return;
  }

  CEffectCreateInfo create_info;
  create_info.owner = &owner_;
  create_info.source_guid = owner_.GetGuid();
  create_info.effect_name = effect_name;
  create_info.attachment_point = -1;
  create_info.flags =
      CEffectFlags::kPendingDestroy | CEffectFlags::kScaleFromOwner;
  create_info.position = &effect_position;
  create_info.world_space = true;
  (void)CEffect_C::AddEffect(session, create_info);
}

void UnitAnimationRuntime::HandleAnimationEvent(WorldSession& session,
                                    std::uint32_t event_type,
                                    std::uint32_t fourcc,
                                    std::int32_t event_data, const float *position,
                                    std::int32_t bone_index) {
  const auto event = ClassifyUnitAnimationEvent(fourcc);

  if (event.route == UnitAnimationEventRoute::kTradeSpellSound && owner_.IsPlayer()) {
    const std::uint32_t spell_id = owner_.Casts().GetCurrentCast().spell_id;
    if (spell_id == 0u) {
      return;
    }

    const auto *dbc = owner_.dbc_loader();
    const auto *spell = dbc != nullptr ? dbc->spell().LookupEntry(spell_id) : nullptr;
    if (spell == nullptr) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                         "NOSPELLIDFOUND|" + std::to_string(spell_id));
      return;
    }

    data::dbc::SpellVisualEntry merged_visual{};
    const auto *visual = owner_.ResolveSpellVisualRecord(*spell, merged_visual, 0u, 0u);
    if (visual == nullptr) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                         "SPELLVISUALIDNOTFOUND|" +
                             std::to_string(spell->spell_visual[0]));
      return;
    }

    const std::uint32_t sound_kit_id = visual->anim_event_sound_id;
    if (sound_kit_id != 0u) {
      const auto unit_position = owner_.GetPosition();
      const float sound_position[3] = {
          unit_position.x, unit_position.y, unit_position.z + 1.0f};
      (void)owner_.sound_runtime().PlaySoundKit(
          sound_kit_id, sound_position);
    }
    return;
  }

  switch (event.route) {
    case UnitAnimationEventRoute::kCreatureFidget:
    case UnitAnimationEventRoute::kFootstepSound:
    case UnitAnimationEventRoute::kCustomSound:
    case UnitAnimationEventRoute::kEmoteSound:
    case UnitAnimationEventRoute::kDirectCreatureSound:
    case UnitAnimationEventRoute::kCreatureVocal:
      (void)UnitSound_DispatchAnimFourCC(&owner_, fourcc, event_data, position);
      return;
    default:
      break;
  }

  if (event.route == UnitAnimationEventRoute::kGroundContactRight ||
      event.route == UnitAnimationEventRoute::kGroundContactLeft) {
    ProcessGroundContactAnimationEvent(
        session, position,
        event.route == UnitAnimationEventRoute::kGroundContactRight);
    return;
  }

  if (event.route == UnitAnimationEventRoute::kSpellContact) {
    owner_.SpellVisuals().RecordAnimHitPosition(position);
    return;
  }

  if (event.route == UnitAnimationEventRoute::kCombat) {
    HandleCombatAudioAnimationEvent(session, fourcc, position);

    if (fourcc == unit_combat::kFourCC_CPP && pending_combat_audio_.active) {
      auto* const objects = owner_.object_manager();
      auto* const victim =
          objects != nullptr
              ? objects->GetMutableUnit(ObjectGuid(pending_combat_audio_.victim_guid))
              : nullptr;
      if (victim != nullptr) {
        victim->Animation().PlayMeleeContactReaction(
            pending_combat_audio_.victim_state, pending_combat_audio_.damage);
      }
    }
    unit_combat::UnitCombat_HandleAnimEvent(&owner_, fourcc, event_data, position, 0);
    return;
  }

  if (event.route == UnitAnimationEventRoute::kVehicleGesture ||
      event.route == UnitAnimationEventRoute::kVehicleTransition) {
    void* const vehicle_data = owner_.Vehicle().GetVehicleData();
    if (vehicle_data != nullptr && vehicle::Vehicle_C_HasDbcEntry(vehicle_data)) {
      const auto route = event.route == UnitAnimationEventRoute::kVehicleGesture
                             ? vehicle::PendingSeatAnimationRoute::kEnterGesture
                             : vehicle::PendingSeatAnimationRoute::kExitTransition;
      (void)vehicle::Vehicle_C_ConsumePendingSeatAnimation(
          session, vehicle_data, route, event_type, fourcc);
    }
    return;
  }

  if (event.route == UnitAnimationEventRoute::kBodyThudEffect) {

    if (owner_.GetPrimaryM2InstanceId() == 0u) {
      return;
    }

    auto effect = HardcodedEffectId::kBreathUnderwater;
    if (owner_.IsPlayer() &&
        GetEffectiveInebriationValue(
            static_cast<const CGPlayer_C&>(owner_).GetDrunkState(),
            static_cast<const CGPlayer_C&>(owner_).GetFakeInebriation()) >= 50u) {
      effect = HardcodedEffectId::kInebriatedBubbles;
    } else if (!owner_.IsSceneSubmergedBelowLiquidSurface()) {
      if (!owner_.IsSceneInSnowArea()) {
        return;
      }
      effect = HardcodedEffectId::kBreathCold;
    }
    AddHardcodedOneShotEffect(session, owner_, effect);
    return;
  }

  if (event.route == UnitAnimationEventRoute::kWeaponContact) {

    if (position == nullptr) {
      return;
    }
    const auto resolved_position = ResolveWeaponContactPosition(owner_, position);
    owner_.SpellVisuals().RecordAnimHitPosition(resolved_position.data());
    return;
  }

  if (event.route == UnitAnimationEventRoute::kShieldLeft ||
      event.route == UnitAnimationEventRoute::kShieldRight) {

    UnitAnimationVisualEvent visual_event{
        .unit_guid = owner_.GetGuid().GetRawValue(),
        .route = event.route,
        .variant = event.variant,
        .event_type = event_type,
        .fourcc = fourcc,
        .event_data = event_data,
        .bone_index = bone_index,
        .has_position = position != nullptr,
    };
    if (position != nullptr) {
      visual_event.position = {position[0], position[1], position[2]};
    }
    (void)DispatchUnitAnimationVisualEvent(visual_event);
    return;
  }
}

void UnitAnimationRuntime::AnimationEventCallback(WorldSession& session,
                                      ObjectManager &objects,
                                      const std::uint64_t unit_guid,
                                      const std::uint32_t event_type,
                                      const std::uint32_t fourcc,
                                      const std::int32_t event_data,
                                      const float *position,
                                      const std::int32_t bone_index) {
  auto* const unit = objects.GetMutableUnit(ObjectGuid(unit_guid));
  if (unit == nullptr) {
    return;
  }

  if (position != nullptr &&
      (std::isnan(position[0]) || std::isnan(position[1]) ||
       std::isnan(position[2]))) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "*** WARNING NAN in AnimEventCallback position eventId=" +
            std::to_string(fourcc) + " eventData=" +
            std::to_string(event_data) + " unit=" + unit->GetName());
  }

  unit->Animation().HandleAnimationEvent(session, event_type, fourcc, event_data, position,
                             bone_index);
}

void UnitAnimationRuntime::ApplyValuesUpdateSessionEffects(
    WorldSession &session, const std::uint8_t previous_stand_state,
    const bool stand_state_changed, const bool emote_state_changed) {
  if (owner_.mount_.PendingDisplayChange().has_value()) {
    const auto mount_display_id = *owner_.mount_.PendingDisplayChange();
    owner_.mount_.SetPendingDisplayChange(std::nullopt);
    owner_.Mount().ApplyDisplayChange(owner_, session, mount_display_id);
  }
  if (stand_state_changed) {
    HandleStandStateTransition(session, previous_stand_state);
    RefreshCameraBoundModelDisplayIfTargeted(session.world_camera());
  } else if (emote_state_changed && GetStandState() != kStandStateDead) {
    UpdateStandAnimation(session, current_anim_group_, ResolveStandAnimationRequestId());
  }
}

void UnitAnimationRuntime::TrySetStandStateAndNotifyServer(WorldSession &session,
                                               const std::uint8_t stand_state) {
  if (stand_state > 10u) {
    std::abort();
  }

  if (owner_.GetGuid() != CGObject_C::GetActivePlayerGuid()) {
    return;
  }

  constexpr std::uint32_t kAllowMountedStandRequest = 0x10000000u;
  constexpr std::uint32_t kUnitFlagStandRequestBlocked = 0x00100000u;
  constexpr std::uint32_t kUnitFlags2StandRequestBlocked = 0x00000001u;
  constexpr std::uint32_t kEmoteStandRequestBlocked = 0x00000400u;
  constexpr std::uint32_t kChannelAllowsStandRequest = 0x08000000u;
  constexpr std::uint32_t kSleepOrSitMovementMask = 0x00000030u;
  constexpr std::uint32_t kNonStandingMovementMask = 0x02E0100Fu;

  if (static_cast<std::int32_t>(owner_.mount_.CachedDisplayForSpell()) > 0 &&
      !owner_.State().HasSpellStateFlags(kAllowMountedStandRequest)) {
    return;
  }
  if ((owner_.State().GetUnitFlags() & kUnitFlagStandRequestBlocked) != 0u ||
      static_cast<std::int32_t>(owner_.State().GetHealth()) <= 0 ||
      (owner_.State().GetUnitFlags2() & kUnitFlags2StandRequestBlocked) != 0u) {
    return;
  }

  if (!owner_.SpellVisuals().DelayedKits().empty()) {
    std::uint32_t first_position_z_bits = 0u;
    std::memcpy(&first_position_z_bits,
                 &owner_.SpellVisuals().DelayedKits().front().position_z,
                sizeof(first_position_z_bits));
    if (first_position_z_bits != 0u) {
      return;
    }
  }

  if ((emote_internal_flags_ & kEmoteStandRequestBlocked) != 0u ||
      owner_.Casts().GetCurrentCast().spell_id != 0u ||
      !owner_.Movement().CanControlCharacter()) {
    return;
  }

  const auto channel_spell_id = owner_.Casts().GetChannelSpellId(owner_);
  if (channel_spell_id != 0u) {
    const auto *dbc = owner_.dbc_loader();
    const auto *spell =
        dbc != nullptr ? dbc->spell().LookupEntry(channel_spell_id) : nullptr;
    if (spell == nullptr ||
        (spell->attributes & kChannelAllowsStandRequest) == 0u) {
      return;
    }
  }

  const auto movement_flags = owner_.GetMovementInfo().flags;
  if ((movement_flags & kSleepOrSitMovementMask) != 0u &&
      (stand_state == 1u || stand_state == 3u)) {
    return;
  }
  if (stand_state != 0u &&
      (movement_flags & kNonStandingMovementMask) != 0u) {
    return;
  }

  session.interaction().SendStandStateChange(stand_state);
}

void UnitAnimationRuntime::MaybeStandUpIfPlayer(WorldSession &session,
                                    const std::uint8_t stand_state) {
  if (owner_.IsPlayer()) {
    TrySetStandStateAndNotifyServer(session, stand_state);
  }
}

bool UnitAnimationRuntime::ShouldUseTargetFrame(const WorldSession &session) const {
  const auto &movement_info = owner_.GetMovementInfo();
  const auto *spline = GetActiveMovementSpline(session, owner_);
  return MatchesSplineAwareMovementGate(movement_info, spline,
                                         kMoveFlagSwimming | kMoveFlagFallingSlow);
}

bool UnitAnimationRuntime::ShouldUseHoverStandAnimation(const WorldSession &session) const {
  return MatchesSplineAwareMovementGate(owner_.GetMovementInfo(), GetActiveMovementSpline(session, owner_),
                                         kMoveFlagHover);
}

bool UnitAnimationRuntime::TryReplaceAnimSlotWithHoverStand(
    std::int32_t &inout_anim_id, std::int32_t &inout_sub_anim_id) {
  if (inout_anim_id == -1 || owner_.GetPrimaryM2InstanceId() == 0) {
    return false;
  }
  constexpr std::int32_t kStandResolvedId = 0;
  constexpr std::int32_t kStandWoundResolvedId = 8;
  constexpr std::int32_t kReadyUnarmedResolvedId = 25;
  const auto anim_id = static_cast<std::uint32_t>(inout_anim_id);
  if (anim_id != static_cast<std::uint32_t>(kStandResolvedId) &&
      anim_id != static_cast<std::uint32_t>(kStandWoundResolvedId) &&
      anim_id != static_cast<std::uint32_t>(kReadyUnarmedResolvedId)) {
    return false;
  }
  inout_anim_id = static_cast<std::int32_t>(kHoverStandAnimationId);
  inout_sub_anim_id = -1;
  return true;
}

void UnitAnimationRuntime::SeedCachedSheatheStateFromDescriptor() {
  const auto sheath_state = static_cast<std::int32_t>(owner_.State().GetSheathState());
  prev_sheathe_state_ = sheath_state;
  sheathe_state_ = sheath_state;
}

void UnitAnimationRuntime::TransitionWeaponSheatheState() {
  const std::int32_t new_state = sheathe_state_;
  const std::int32_t old_state = prev_sheathe_state_;

  switch (new_state) {
    case 0: {
      if (owner_.Presentation().HasCharacterModelVisual()) {
        owner_.Presentation().AddCharacterVisualRefreshFlags(
            kCharacterModelFlagForceEquipmentRefresh);
      }

      if (old_state == 2) {
        if (owner_.Interaction().AutoAttackType() == 0u) {
          owner_.Interaction().CompleteAutoAttackInteraction(false, true);
        }
      }
      emote_internal_flags_ &= ~kWeaponTransitionMask;
      break;
    }

    case 1: {
      if (owner_.Presentation().HasCharacterModelVisual()) {
        owner_.Presentation().AddCharacterVisualRefreshFlags(
            kCharacterModelFlagForceEquipmentRefresh);
      }

      if (owner_.State().GetVirtualItemSlotEntry(0) != 0u && PlayMainhandSheatheAnimation()) {
        emote_internal_flags_ |= kWeaponTransitionMainhand;
      }

      if (owner_.State().GetVirtualItemSlotEntry(1) != 0u && PlayOffhandSheatheAnimation()) {
        emote_internal_flags_ |= kWeaponTransitionOffhand;
      }
      break;
    }

    case 2: {
      if (owner_.Presentation().HasCharacterModelVisual()) {
        owner_.Presentation().AddCharacterVisualRefreshFlags(
            kCharacterModelFlagForceEquipmentRefresh);
      }

      emote_internal_flags_ &= ~kWeaponTransitionMask;
      break;
    }

    default:
      break;
  }
}

void UnitAnimationRuntime::PlayWeaponSheatheAnimation(std::int32_t ) {}

openwow::render::m2::M2OperationSummary UnitAnimationRuntime::SetAnimationRecursive(
    std::uint32_t instance_id, std::int32_t anim_group,
    std::uint32_t anim_id, std::int32_t sub_variant,
    std::int32_t loop, float speed, std::int32_t blend_in,
    std::int32_t blend_out, bool force) {
  openwow::render::m2::M2OperationSummary result;

  if (!force) {
    const auto *passenger = owner_.Vehicle().GetVehiclePassengerComponent();
    if (passenger != nullptr && passenger->IsAttachedToVehicle()) {
      return result;
    }
  }

  static constexpr std::uint32_t kSpellStateNoBlendIn = 0x8000000u;
  if (owner_.State().HasSpellStateFlags(kSpellStateNoBlendIn)) {
    blend_in = 0;
  }

  current_anim_group_ = anim_group;
  if (instance_id == 0u || instance_id == owner_.GetPrimaryM2InstanceId()) {
    const auto requested_animation = anim_group >= 0
                                         ? static_cast<std::uint32_t>(anim_group)
                                         : anim_id;
    const bool looping = loop != 0 || AnimationSequenceLoops(requested_animation);
    RequestPlayback(static_cast<std::uint16_t>(requested_animation), looping,
                    !looping);
  } else if (owner_.m2_system() != nullptr) {
    const openwow::render::m2::M2AnimationRequest animation_request{
        .animation_lookup_id = -1,
        .animation_id = anim_id,
        .sub_animation_index = sub_variant,
        .loop_count = loop,
        .speed = speed,
    };
    auto &m2_system = *owner_.m2_system();
    result.AddStatus(
        m2_system.SetAnimationRequest(instance_id, animation_request));
    if (anim_group >= 0 &&
        static_cast<std::uint32_t>(anim_group) <
            openwow::render::m2::kM2RetailAnimationSlotCount) {
      result.AddStatus(m2_system.SetAnimationSlotRequest(
          instance_id, static_cast<std::uint32_t>(anim_group),
          animation_request));
    }
  }

  auto *vehicle = owner_.Vehicle().GetVehicleData();
  if (vehicle != nullptr && instance_id == owner_.GetPrimaryM2InstanceId()) {
    vehicle::Vehicle_C_ForEachPassengerUnit(
        vehicle, [&](CGUnit_C &passenger_unit) {
          const auto *pcomp = passenger_unit.Vehicle().GetVehiclePassengerComponent();
          if (pcomp == nullptr || !pcomp->IsAttachedToVehicle()) {
            return;
          }
          const auto passenger_instance_id =
              passenger_unit.GetPrimaryM2InstanceId();
          if (passenger_instance_id == 0) {
            return;
          }

          result.AddSummary(passenger_unit.Animation().SetAnimationRecursive(
              passenger_instance_id, anim_group, anim_id, -1, loop, speed,
              blend_in, blend_out, true));
        });
  }
  return result;
}

int UnitAnimationRuntime::StopAnimAndPropagateToPassengers(const bool clear_primary_channel,
                                               const bool force) {

  if (!force) {
    const auto *passenger = owner_.Vehicle().GetVehiclePassengerComponent();
    if (passenger != nullptr && passenger->IsAttachedToVehicle()) {
      return 0;
    }
  }

  const bool had_primary_state =
      clear_primary_channel && selected_stand_animation_id_.has_value();
  const bool had_secondary_state = current_anim_group_ != -1;

  if (!had_primary_state && !had_secondary_state) {
    return 0;
  }

  if (clear_primary_channel) {
    ClearSelectedStandAnimationState();
  }

  current_anim_group_ = -1;
  return 1;
}

bool UnitAnimationRuntime::PlayMainhandSheatheAnimation() {
  emote_internal_flags_ |= kWeaponTransitionMainhand;

  if (prev_sheathe_state_ == 0)
    return false;

  constexpr std::int32_t  kAnimGroupMainhand      = 3;
  constexpr std::uint32_t kAnimReadySheatheMelee   = 89u;
  constexpr std::uint32_t kAnimReadySheatheRanged  = 90u;

  if (sheathe_state_ == 1) {
    const std::uint32_t display_id = owner_.State().GetVirtualItemSlotEntry(0);
    if (display_id == 0)
      return false;

    const auto metadata = GetVisibleWeaponMetadataForAnimation(owner_, 0u);
    const bool is_ranged_sheathe_type =
        metadata.has_value() &&
        SheatheTypeUsesRangedReadyAnimation(metadata->sheath);
    const std::uint32_t anim_id =
        is_ranged_sheathe_type ? kAnimReadySheatheRanged : kAnimReadySheatheMelee;

    const auto animation_result = SetAnimationRecursive(
        owner_.GetPrimaryM2InstanceId(), kAnimGroupMainhand, anim_id,
        -1, 0, 1.0f,
        1, 1, false);
    return animation_result.status == openwow::render::m2::M2ResultStatus::kReady;
  }

  if (sheathe_state_ == 2) {
    const std::uint32_t display_id = owner_.State().GetVirtualItemSlotEntry(2);
    if (display_id == 0)
      return false;

    const auto metadata = GetVisibleWeaponMetadataForAnimation(owner_, 2u);
    if (!metadata.has_value() ||
        !IsPrimaryRangedInventoryType(metadata->inventory_type)) {
      return false;
    }

    const bool is_ranged_sheathe_type =
        SheatheTypeUsesRangedReadyAnimation(metadata->sheath);
    const std::uint32_t anim_id =
        is_ranged_sheathe_type ? kAnimReadySheatheRanged : kAnimReadySheatheMelee;

    const auto animation_result = SetAnimationRecursive(
        owner_.GetPrimaryM2InstanceId(), kAnimGroupMainhand, anim_id,
        -1, 0, 1.0f,
        1, 1, false);
    return animation_result.status == openwow::render::m2::M2ResultStatus::kReady;
  }

  return false;
}

bool UnitAnimationRuntime::PlayOffhandSheatheAnimation() {
  emote_internal_flags_ |= kWeaponTransitionOffhand;

  if (prev_sheathe_state_ == 0)
    return false;

  constexpr std::int32_t  kAnimGroupOffhand        = 2;
  constexpr std::uint32_t kAnimReadySheatheMelee   = 89u;
  constexpr std::uint32_t kAnimReadySheatheRanged  = 90u;

  if (sheathe_state_ == 1) {
    const std::uint32_t display_id = owner_.State().GetVirtualItemSlotEntry(1);
    if (display_id == 0)
      return false;

    const auto metadata = GetVisibleWeaponMetadataForAnimation(owner_, 1u);
    const bool is_ranged_sheathe_type =
        metadata.has_value() &&
        SheatheTypeUsesRangedReadyAnimation(metadata->sheath);
    const std::uint32_t anim_id =
        is_ranged_sheathe_type ? kAnimReadySheatheRanged : kAnimReadySheatheMelee;

    const auto animation_result = SetAnimationRecursive(
        owner_.GetPrimaryM2InstanceId(), kAnimGroupOffhand, anim_id,
        -1, 0, 1.0f,
        1, 1, false);
    return animation_result.status == openwow::render::m2::M2ResultStatus::kReady;
  }

  if (sheathe_state_ == 2) {
    const std::uint32_t display_id = owner_.State().GetVirtualItemSlotEntry(2);
    if (display_id == 0)
      return false;

    const auto metadata = GetVisibleWeaponMetadataForAnimation(owner_, 2u);
    if (!metadata.has_value() ||
        IsPrimaryRangedInventoryType(metadata->inventory_type)) {
      return false;
    }

    const bool is_ranged_sheathe_type =
        SheatheTypeUsesRangedReadyAnimation(metadata->sheath);
    const std::uint32_t anim_id =
        is_ranged_sheathe_type ? kAnimReadySheatheRanged : kAnimReadySheatheMelee;

    const auto animation_result = SetAnimationRecursive(
        owner_.GetPrimaryM2InstanceId(), kAnimGroupOffhand, anim_id,
        -1, 0, 1.0f,
        1, 1, false);
    return animation_result.status == openwow::render::m2::M2ResultStatus::kReady;
  }

  return false;
}

void UnitAnimationRuntime::ChangeSheatheStateAndNotifyServer(std::int32_t new_state, bool animate,
                                                 bool silent) {
  if (new_state == sheathe_state_)
    return;

  prev_sheathe_state_ = sheathe_state_;
  sheathe_state_ = new_state;

  if (animate) {
    TransitionWeaponSheatheState();
  } else {
    PlayWeaponSheatheAnimation(prev_sheathe_state_);
  }

  if (!silent && owner_.IsActivePlayer()) {
    if (const auto* objects = owner_.object_manager(); objects != nullptr) {
      objects->SendSheathed(static_cast<std::uint32_t>(new_state));
    }
  }
}

void UnitAnimationRuntime::SetIdleWeaponEnchantVisuals(bool enabled) {
  if (enabled == idle_weapon_item_visuals_enabled_)
    return;

  idle_weapon_item_visuals_enabled_ = enabled;
}

void UnitAnimationRuntime::SelectEmoteAnimation(
    const bool suppress_sheathe_update) {
  const auto *const dbc = owner_.dbc_loader();
  const std::int32_t anim_id =
      (owner_.GetPrimaryM2InstanceId() != 0) ? current_anim_group_ : -1;

  const data::dbc::AnimationDataEntry *anim_entry = nullptr;
  if (dbc != nullptr && anim_id >= 0) {
    anim_entry = dbc->animation_data().LookupEntry(
        static_cast<std::uint32_t>(anim_id));
  }

  if (anim_entry != nullptr && (anim_entry->weapon_flags & 0x4u) != 0 &&
      !suppress_sheathe_update) {
    ChangeSheatheStateAndNotifyServer(0, true, false);
    SetIdleWeaponEnchantVisuals(false);
    return;
  }

  if (sheathe_state_ == kSheatheStateRanged) {
    if (!owner_.IsActivePlayer()) {
      ChangeSheatheStateAndNotifyServer(kSheatheStateRanged, true, false);
    }

    const bool anim_exempt =
        anim_id == 105 || anim_id == 106 || anim_id == 112 ||
        (anim_id >= 0 &&
         IsRangedAttackOrSitSleepBehavior(
             static_cast<std::uint32_t>(anim_id)));

    if (!anim_exempt && anim_entry != nullptr) {
      if ((anim_entry->weapon_flags & 0x10u) != 0) {
        ChangeSheatheStateAndNotifyServer(0, true, false);
      } else if ((anim_entry->weapon_flags & 0x20u) != 0) {
        ChangeSheatheStateAndNotifyServer(1, true, false);
      }
    }
    return;
  }

  if (owner_.Casts().IsCasting()) {
    constexpr std::uint32_t kKeepSheathedWhileCastingAttribute = 0x00040000u;
    const auto* const spell =
        dbc != nullptr ? dbc->spell().LookupEntry(owner_.Casts().GetCurrentCast().spell_id)
                       : nullptr;
    if (spell != nullptr &&
        (spell->attributes & kKeepSheathedWhileCastingAttribute) == 0u) {
      ChangeSheatheStateAndNotifyServer(0, true, false);
    }
    SetIdleWeaponEnchantVisuals(false);
    return;
  }

  const bool has_target = owner_.Interaction().HasCachedUpdateTarget();

  if (has_target && anim_id >= 0 && dbc != nullptr) {
    const auto *const entry = dbc->animation_data().LookupEntry(
        static_cast<std::uint32_t>(anim_id));
    if (entry != nullptr &&
        !(entry->behavior_id >= 25u && entry->behavior_id <= 29u) &&
        IsLoopingCombatOrReadyStanceBehavior(
            static_cast<std::uint32_t>(anim_id))) {
      ChangeSheatheStateAndNotifyServer(1, true, false);
      SetIdleWeaponEnchantVisuals(false);
      return;
    }
  }

  if (anim_entry != nullptr) {
    if ((anim_entry->weapon_flags & 0x10u) != 0 && !suppress_sheathe_update) {
      ChangeSheatheStateAndNotifyServer(0, true, false);
      return;
    }
    if ((anim_entry->weapon_flags & 0x20u) != 0) {
      ChangeSheatheStateAndNotifyServer(1, true, false);
      return;
    }
  }

  if (has_target) {
    ChangeSheatheStateAndNotifyServer(1, true, false);
    return;
  }

  if (owner_.IsActivePlayer()) {
    SetIdleWeaponEnchantVisuals(true);
  } else {

    const auto descriptor_sheathe_state =
        static_cast<std::int32_t>(owner_.State().GetSheathState());
    if (sheathe_state_ == descriptor_sheathe_state) {
      SetIdleWeaponEnchantVisuals(true);
    } else {
      ChangeSheatheStateAndNotifyServer(descriptor_sheathe_state, true, false);
    }
  }
}

void UnitAnimationRuntime::TryPlaySpeechEmoteSlot(const std::uint32_t slot_index) {
  if (const auto *const dbc = owner_.dbc_loader(); dbc != nullptr) {
    AssignAnimationSlotsByFlags(*dbc);
  } else {
    ClearAnimationSlots();
  }
  if (slot_index >= g_speech_emote_slots.size() ||
      g_speech_emote_slots[slot_index] == 0u ||
      owner_.Movement().IsSwimming() ||
      owner_.Vehicle().HasAttachedVehiclePassenger()) {
    return;
  }
  PlayEmoteOnUnit(
      static_cast<std::int32_t>(g_speech_emote_slots[slot_index]));
}

void UnitAnimationRuntime::SendTextEmote(const std::uint32_t *emote_entry,
                            std::uint64_t target_guid) {
  if (!emote_entry)
    return;

  auto self_guid = owner_.GetGuid().GetRawValue();
  if (target_guid == self_guid) {
    target_guid = 0;
  }

}

std::uint32_t UnitAnimationRuntime::GetEmoteState() const {
  return owner_.GetUInt32(UNIT_NPC_EMOTESTATE);
}

std::uint8_t UnitAnimationRuntime::GetStandState() const {
  return static_cast<std::uint8_t>(owner_.GetUInt32(UNIT_FIELD_BYTES_1) & 0xFFu);
}

bool UnitAnimationRuntime::HasActiveSpellVisualStandAnimationSource() const {
  return owner_.Casts().GetCurrentCast().spell_id != 0u ||
         owner_.Casts().GetChannelSpellId(owner_) != 0u ||
         owner_.Casts().GetChannelCast().spell_id != 0u || HasChannelingActionLock();
}

std::optional<std::uint16_t> UnitAnimationRuntime::GetCurrentAnimationId() const {
  if (current_anim_group_ >= 0 &&
      static_cast<std::uint32_t>(current_anim_group_) <
          kInvalidUnitAnimationId) {
    return static_cast<std::uint16_t>(current_anim_group_);
  }
  if (selected_stand_animation_id_.has_value()) {
    return selected_stand_animation_id_;
  }
  return std::nullopt;
}

std::uint16_t UnitAnimationRuntime::GetResolvedBasePlaybackAnimationId() const {
  if (playback_request_.base_animation_id == kNoAnimationRow) {
    return kNoAnimationRow;
  }
  if (playback_request_.base_bypass_alias_resolution) {
    return playback_request_.base_animation_id;
  }
  const auto resolved =
      ResolveAnimationId(playback_request_.base_animation_id,
                         owner_.GetPrimaryM2InstanceId());
  return resolved < kInvalidUnitAnimationId
             ? static_cast<std::uint16_t>(resolved)
             : playback_request_.base_animation_id;
}

std::uint16_t UnitAnimationRuntime::GetResolvedPlaybackAnimationId() const {

  if (playback_request_.bypass_alias_resolution) {
    return playback_request_.animation_id;
  }
  const auto resolved =
      ResolveAnimationId(playback_request_.animation_id,
                         owner_.GetPrimaryM2InstanceId());
  return resolved < kInvalidUnitAnimationId
             ? static_cast<std::uint16_t>(resolved)
             : playback_request_.animation_id;
}

bool UnitAnimationRuntime::RequestPlayback(const std::uint16_t animation_id,
                                           bool looping,
                                           const bool restart,
                                           const bool bypass_alias_resolution) {

  const std::uint32_t requested_behavior =
      ResolveAnimationBehaviorId(owner_, animation_id);

  if (IsEmoteAnimationStateBlocked() &&
      !IsDeadAnimationFamily(requested_behavior)) {
    return false;
  }

  if ((emote_internal_flags_ & kEmoteInternalFlagInFlightSubmitFilterMask) != 0u &&
      requested_behavior != kSubmergeBehaviorId &&
      requested_behavior != kSubmergedBehaviorId &&
      requested_behavior != kBirthBehaviorId &&
      !IsDeadAnimationFamily(requested_behavior)) {
    return false;
  }
  if ((emote_internal_flags_ & kEmoteInternalFlagAnimationBehavior458) != 0u) {
    return false;
  }

  std::uint16_t resolved_row = animation_id;
  if (!bypass_alias_resolution) {
    const auto resolved =
        ResolveAnimationId(animation_id, owner_.GetPrimaryM2InstanceId());
    if (resolved < kInvalidUnitAnimationId) {
      resolved_row = static_cast<std::uint16_t>(resolved);
    }
  }
  std::uint32_t resolved_behavior =
      ResolveAnimationBehaviorId(owner_, resolved_row);

  std::uint16_t submit_row = animation_id;
  if ((emote_internal_flags_ & kEmoteStateResolveEnabledFlag) != 0u &&
      animation_id != playback_request_.animation_id &&
      IsLoopingCombatAnimationBehavior(
          ResolveAnimationBehaviorId(owner_, playback_request_.animation_id)) &&
      IsLoopingCombatAnimationBehavior(resolved_behavior)) {
    pending_protected_playback_ = PendingProtectedPlayback{
        animation_id, looping, bypass_alias_resolution};
    return false;
  }

  pending_protected_playback_.reset();

  bool rider_substituted = false;
  if (owner_.GetUInt32(UNIT_FIELD_MOUNTDISPLAYID) != 0u &&
      IsMountModelBehavior(resolved_behavior)) {
    submit_row = kRiderMountAnimationId;
    resolved_row = kRiderMountAnimationId;
    resolved_behavior = ResolveAnimationBehaviorId(owner_, resolved_row);
    looping = AnimationSequenceLoops(resolved_row);
    rider_substituted = true;
  }

  if ((!restart || rider_substituted) &&
      playback_request_.animation_id == submit_row &&
      playback_request_.looping == looping &&
      playback_request_.bypass_alias_resolution == bypass_alias_resolution) {

    current_anim_group_ = submit_row;
    return true;
  }

  const bool upper_body_only =
      IsUpperBodyOnlyAnimation(submit_row, playback_request_.base_animation_id);
  CommitPlaybackRequest(submit_row, looping, upper_body_only,
                        bypass_alias_resolution, false);

  ApplySubmitFunnelFlagBits(resolved_behavior);
  return true;
}

void UnitAnimationRuntime::SubmitRawPlayback(const std::uint16_t animation_id,
                                             const bool looping,
                                             const bool upper_body_only,
                                             const bool zero_blend) {
  CommitPlaybackRequest(animation_id, looping, upper_body_only,
                        true, zero_blend);
}

void UnitAnimationRuntime::CommitPlaybackRequest(
    const std::uint16_t animation_id, const bool looping,
    const bool upper_body_only, const bool bypass_alias_resolution,
    const bool zero_blend) {

  const std::uint16_t previous_base_row = playback_request_.base_animation_id;
  const bool previous_base_looping = playback_request_.base_looping;
  const std::uint16_t previous_row = playback_request_.animation_id;
  const bool previous_upper = playback_request_.upper_body_only;
  const auto interrupt = [this](const std::uint16_t displaced_row) {
    ClearSequenceEndFlagBits(ResolveAnimationBehaviorId(owner_, displaced_row),
                             displaced_row);
  };
  if (upper_body_only) {

    if (previous_upper && previous_row != animation_id) interrupt(previous_row);
  } else {
    if (previous_row != animation_id) interrupt(previous_row);
    if (previous_upper && previous_base_row != kNoAnimationRow &&
        previous_base_row != animation_id) {
      interrupt(previous_base_row);
    }
  }

  playback_request_.animation_id = animation_id;
  playback_request_.looping = looping;
  playback_request_.upper_body_only = upper_body_only;
  if (upper_body_only) {
    playback_request_.base_animation_id = previous_base_row;
    playback_request_.base_looping = previous_base_looping;
  } else {
    playback_request_.base_animation_id = animation_id;
    playback_request_.base_looping = looping;
    playback_request_.base_bypass_alias_resolution = bypass_alias_resolution;
  }
  playback_request_.bypass_alias_resolution = bypass_alias_resolution;
  playback_request_.zero_blend = zero_blend;
  if (++playback_request_.serial == 0u) {
    playback_request_.serial = 1u;
  }

  current_anim_group_ = animation_id;
}

void UnitAnimationRuntime::ApplySubmitFunnelFlagBits(
    const std::uint32_t submitted_behavior) {
  switch (submitted_behavior) {
  case kJumpEndBehaviorId:
    emote_internal_flags_ |= kEmoteInternalFlagAnimationBehavior39;
    break;
  case kBirthBehaviorId:
    emote_internal_flags_ |= kEmoteInternalFlagAnimationBehavior127;
    break;
  case kLiftOffBehaviorId:
  case kLandBehaviorId:
    emote_internal_flags_ |= kEmoteInternalFlagAnimationBehavior192;
    break;
  case kKnockdownBehaviorId:
    emote_internal_flags_ |= kEmoteInternalFlagAnimationBehavior121;
    break;
  case kSubmergeBehaviorId:
    emote_internal_flags_ |= kEmoteInternalFlagAnimationBehavior201;
    break;
  case kToFlyBehaviorId:
  case kToFlyBehaviorId + 1u:
  case kToGroundBehaviorId:
    emote_internal_flags_ |= kEmoteInternalFlagAnimationBehavior458;
    break;
  default:
    break;
  }

  if (IsAirborneDeathSubmitBehavior(submitted_behavior) &&
      cached_anim_tier_ == kAnimationTierFly &&
      (playback_movement_flags_ & kAirborneDeathSubmitMovementMask) != 0u) {
    emote_internal_flags_ |= kEmoteInternalFlagAirborneDeathSubmit;
  } else {
    emote_internal_flags_ &= ~kEmoteInternalFlagAirborneDeathSubmit;
  }
}

void UnitAnimationRuntime::ClearSequenceEndFlagBits(
    const std::uint32_t finished_behavior,
    const std::uint16_t finished_animation_id) {
  if (deferred_animation_satisfied_id_ ==
      static_cast<std::int32_t>(finished_animation_id)) {
    deferred_animation_satisfied_id_ = -1;
  }
  switch (finished_behavior) {
  case kJumpEndBehaviorId:
    emote_internal_flags_ &= ~kEmoteInternalFlagAnimationBehavior39;
    break;
  case kBirthBehaviorId:
    emote_internal_flags_ &= ~kEmoteInternalFlagAnimationBehavior127;
    break;
  case kLiftOffBehaviorId:
  case kLandBehaviorId:
    emote_internal_flags_ &= ~kEmoteInternalFlagAnimationBehavior192;
    break;
  case kKnockdownBehaviorId:
    emote_internal_flags_ &= ~kEmoteInternalFlagAnimationBehavior121;
    break;
  case kSubmergeBehaviorId:
    emote_internal_flags_ &= ~kEmoteInternalFlagAnimationBehavior201;
    break;
  case kJumpStartBehaviorId:
    emote_internal_flags_ &= ~kEmoteInternalFlagFlightTransitionLock;
    break;
  case kToFlyBehaviorId:
  case kToFlyBehaviorId + 1u:
  case kToGroundBehaviorId:
    emote_internal_flags_ &= ~kEmoteInternalFlagAnimationBehavior458;
    break;
  default:
    break;
  }
}

bool UnitAnimationRuntime::IsMountModelBehavior(
    const std::uint32_t behavior) noexcept {
  if (behavior < 0x60u) {
    if (behavior > 0x5Bu) return true;
    if (behavior < 7u) return behavior != 2u;
    return (behavior >= 8u && behavior <= 0xDu) ||
           (behavior >= 0x25u && behavior <= 0x2Du);
  }
  if (behavior < 0x85u) {
    return behavior == 0x77u || behavior == 0x78u || behavior == 0x7Fu ||
           behavior == 0x83u || behavior == 0x84u;
  }
  return behavior == 0x87u || behavior == 0x8Fu || behavior == 0xBBu ||
         behavior == 0xC1u;
}

void UnitAnimationRuntime::HandleMovementAnimation(
    const std::uint32_t previous_flags, const std::uint32_t current_flags,
    const bool suppress_land_animation) {
  playback_movement_flags_ = current_flags;
  const bool was_falling =
      (previous_flags & (kMoveFlagFalling | kMoveFlagFallingFar)) != 0u;
  const bool is_falling =
      (current_flags & (kMoveFlagFalling | kMoveFlagFallingFar)) != 0u;
  if (owner_.State().IsDead()) {
    if (!playback_request_.looping &&
        IsDeadAnimationFamily(playback_request_.animation_id)) {
      return;
    }

    if (PrimaryM2ModelContainsAnimation(owner_,
                                        static_cast<std::uint32_t>(
                                            render::AnimId::kDead))) {
      RequestPlayback(render::AnimId::kDead, false);
    }
    return;
  }
  if (!was_falling && is_falling) {
    if (owner_.GetMovementInfo().HasFallingLaunchVelocity()) {

      owner_.Sound().PlayCreatureSound(
          owner_, static_cast<std::uint32_t>(CreatureSoundType::JumpStart),
          true);
      RequestPlayback(render::AnimId::kJumpStart, false, true);
    }
    return;
  }
  if (was_falling && !is_falling) {
    if (suppress_land_animation) {

      return;
    }

    owner_.Sound().PlayCreatureSound(
        owner_, static_cast<std::uint32_t>(CreatureSoundType::JumpEnd), true);
    const bool running_forward =
        (current_flags & kMoveFlagForward) != 0u &&
        (current_flags & (kMoveFlagBackward | kMoveFlagWalking |
                          kMoveFlagSwimming | kMoveFlagFlying)) == 0u;
    RequestPlayback(running_forward ? 187u : render::AnimId::kJumpEnd,
                    false, true);
    return;
  }

  if ((current_flags & (kMoveFlagFalling | kMoveFlagFallingFar)) != 0u) {
    stand_selector_refresh_pending_ = true;
    return;
  }
  std::uint16_t locomotion_animation_id = 0u;
  (void)ResolveDirectionalLocomotionAnimation(current_flags, ~0u,
                                              &locomotion_animation_id);
  if (locomotion_animation_id != 0u) {
    RequestPlayback(locomotion_animation_id,
                    AnimationSequenceLoops(locomotion_animation_id));
    return;
  }

  if (previous_flags != current_flags) {
    stand_selector_refresh_pending_ = true;
  }
}

void UnitAnimationRuntime::PlayAttackAnimation(const std::uint32_t hit_info,
                                               const std::uint32_t melee_spell_id) {
  if (owner_.State().IsDead()) {
    return;
  }
  if ((hit_info & kHitInfoNoAnimation) != 0u) {
    return;
  }

  if (melee_spell_id != 0u) {
    return;
  }
  std::uint16_t attack = render::AnimId::kAttackUnarmed;
  if ((hit_info & kHitInfoOffhandSwing) != 0u) {
    const auto off_hand = GetVisibleWeaponMetadataForAnimation(owner_, 1u);
    attack = (!off_hand.has_value() ||
              off_hand->item_class != static_cast<std::uint32_t>(ItemClass::Weapon))
                 ? kAttackUnarmedOffAnimationId
             : off_hand->subclass == kItemSubclassWeaponDagger
                 ? kAttackOffPierceAnimationId
                 : kAttackOffAnimationId;
  } else if (const auto main_hand = GetVisibleWeaponMetadataForAnimation(owner_, 0u);
             main_hand.has_value() &&
             main_hand->item_class == static_cast<std::uint32_t>(ItemClass::Weapon)) {
    const bool has_offhand =
        GetVisibleWeaponMetadataForAnimation(owner_, 1u).has_value();
    switch (main_hand->subclass) {
    case 0u: case 4u: case 7u: case 11u: case 14u:
      attack = render::AnimId::kAttack1H;
      break;
    case 1u: case 5u: case 8u: case 12u:
      attack = has_offhand ? render::AnimId::kAttack1H : render::AnimId::kAttack2H;
      break;
    case 6u: case 10u: case 17u: case 20u:
      attack = render::AnimId::kAttack2HL;
      break;
    case kItemSubclassWeaponDagger:
      attack = kAttack1HPierceAnimationId;
      break;
    default:
      attack = render::AnimId::kAttackUnarmed;
      break;
    }
  }
  RequestPlayback(attack, false, true);
}

void UnitAnimationRuntime::ApplyAttackerStateRecordToVictim(
    const WorldSession &session, const std::uint32_t hit_info) {

  if ((owner_.GetUInt32(UNIT_DYNAMIC_FLAGS) & kUnitDynFlagLootable) != 0u) {
    return;
  }
  if ((hit_info & kHitInfoAffectsVictim) != 0u) {
    PlayWoundReaction(session, (hit_info & kHitInfoCriticalHit) != 0u);
  }
}

void UnitAnimationRuntime::PlayWoundReaction(const WorldSession &session,
                                             const bool critical) {
  if (IsEmoteAnimationStateBlocked() || !IsPrimaryM2ModelStreamedFor(owner_) ||
      IsAnimationUpdateSuppressed()) {
    return;
  }
  const bool has_key_bone_channel = animation_bone_index_ != -1;
  std::uint16_t group = render::AnimId::kStandWound;
  bool upper_channel = has_key_bone_channel;
  if (critical) {
    group = render::AnimId::kCombatCritical;
  } else if (owner_.Interaction().HasCachedUpdateTarget()) {
    group = render::AnimId::kCombatWound;
  } else {
    const auto &movement_info = owner_.GetMovementInfo();
    const std::uint8_t stand_state = GetStandState();
    const bool idle_standing =
        owner_.GetUInt32(UNIT_FIELD_MOUNTDISPLAYID) == 0u &&
        (movement_info.flags & kWoundIdleStandingMovementMask) == 0u &&
        !MatchesSplineAwareMovementGate(
            movement_info, GetActiveMovementSpline(session, owner_),
            kMoveFlagHover) &&
        stand_state != kStandStateSitGround &&
        (stand_state < kStandStateSitChairLow ||
         stand_state > kStandStateSitChairHigh) &&
        ResolveAnimationBehaviorId(owner_, playback_request_.base_animation_id) !=
            kSitGroundUpBehaviorId;
    if (idle_standing) {
      upper_channel = false;
    }
  }

  if (upper_channel == has_key_bone_channel) {
    const std::uint16_t base_row = playback_request_.base_animation_id;
    const std::uint32_t base_behavior =
        base_row == kNoAnimationRow ? kInvalidAnimationBehaviorId
                                    : ResolveAnimationBehaviorId(owner_, base_row);
    if (base_row == render::AnimId::kStand ||
        (base_behavior >= 0x19u && base_behavior <= 0x1Du)) {
      upper_channel = false;
    }
  }
  const std::uint32_t resolved =
      ResolveAnimationId(group, owner_.GetPrimaryM2InstanceId());
  if (resolved >= kInvalidUnitAnimationId ||
      !PrimaryM2ModelContainsAnimation(owner_, resolved)) {
    return;
  }
  SubmitRawPlayback(static_cast<std::uint16_t>(resolved), false,
                    upper_channel && has_key_bone_channel,
                    true);
}

void UnitAnimationRuntime::PlayMeleeContactReaction(
    const std::uint8_t victim_state, const std::uint32_t damage) {
  if (owner_.State().IsDead() || GetStandState() == kStandStateDead) {
    return;
  }
  std::optional<std::uint16_t> animation;
  switch (victim_state) {
  case kVictimStateDodge:
  case kVictimStateDeflect:
    animation = render::AnimId::kDodge;
    break;
  case kVictimStateParry:
    animation = GetWeaponBasedParryAnimationId();
    break;
  default:
    if (damage == 0u) {
      return;
    }
    animation = render::AnimId::kShieldBlock;
    break;
  }

  if (!animation.has_value()) {
    return;
  }
  RequestPlayback(*animation, false, true);
}

void UnitAnimationRuntime::QueueCombatAudioResult(
    const std::uint64_t victim_guid, const std::uint32_t hit_info,
    const std::uint32_t damage, const std::uint32_t overkill,
    const std::uint8_t victim_state) {
  pending_combat_audio_ = {
      .victim_guid = victim_guid,
      .hit_info = hit_info,
      .damage = damage,
      .overkill = overkill,
      .victim_state = victim_state,
      .active = victim_guid != 0u,
  };
}

void UnitAnimationRuntime::HandleCombatAudioAnimationEvent(
    WorldSession &session, const std::uint32_t fourcc,
    const float *position) {
  if (!pending_combat_audio_.active) {
    return;
  }

  const bool is_attack_hit_event =
      fourcc >= unit_combat::kFourCC_AH0 &&
      fourcc <= unit_combat::kFourCC_AH3;
  const bool is_attack_contact_event =
      is_attack_hit_event || fourcc == unit_combat::kFourCC_CAH;
  if (!is_attack_contact_event && fourcc != unit_combat::kFourCC_CSS) {
    return;
  }

  const auto owner_position = owner_.GetPosition();
  const float fallback_position[3] = {
      owner_position.x, owner_position.y, owner_position.z};
  const float *sound_position = position != nullptr ? position : fallback_position;
  const auto local_guid = session.objects().GetLocalPlayerGuid();
  const bool use_listener_priority =
      owner_.GetGuid() == local_guid ||
      ObjectGuid(pending_combat_audio_.victim_guid) == local_guid;

  const std::uint8_t weapon_slot =
      (pending_combat_audio_.hit_info & unit_combat::AttackHitFlags::kOffhand) != 0u
          ? 1u
          : 0u;
  const auto weapon = GetVisibleWeaponMetadataForAnimation(owner_, weapon_slot);

  if (fourcc == unit_combat::kFourCC_CSS) {
    if (weapon.has_value() && owner_.dbc_loader() != nullptr) {
      const auto *display = owner_.dbc_loader()->item_display_info().LookupEntry(
          weapon->display_id);
      if (display != nullptr) {
        (void)PlayWoundDeathSound(
            owner_.sound_runtime(), display->group_sound_index,
            (pending_combat_audio_.hit_info &
             unit_combat::AttackHitFlags::kCriticalHit) != 0u,
            sound_position, pending_combat_audio_.overkill != 0u,
            use_listener_priority);
      }
    }
    pending_combat_audio_ = {};
    return;
  }

  if (auto *const victim = session.objects().GetMutableUnit(
          ObjectGuid(pending_combat_audio_.victim_guid));
      victim != nullptr) {
    victim->Animation().ApplyAttackerStateRecordToVictim(
        session, pending_combat_audio_.hit_info);
  }

  if (is_attack_hit_event) {
    const auto *sound_data = owner_.Sound().ResolveActive(owner_);
    if (sound_data != nullptr) {
      const std::array custom_attacks = {
          sound_data->custom_attack0, sound_data->custom_attack1,
          sound_data->custom_attack2, sound_data->custom_attack3};
      const std::size_t custom_index =
          static_cast<std::size_t>(fourcc - unit_combat::kFourCC_AH0) /
          0x01000000u;
      if (custom_index < custom_attacks.size() &&
          custom_attacks[custom_index] != 0u) {
        audio::SoundKitPlaybackOptions options{};
        options.sound_type = 14;
        if (use_listener_priority) {
          options.playback_priority = 110;
        }
        (void)owner_.sound_runtime().PlaySoundKit(
            custom_attacks[custom_index], sound_position, nullptr, options);
      }
    }
  }

  unit_combat::AttackResultType result_type =
      unit_combat::AttackResultType::kMiss;
  switch (pending_combat_audio_.victim_state) {
    case 1u: result_type = unit_combat::AttackResultType::kWound; break;
    case 2u: result_type = unit_combat::AttackResultType::kDodge; break;
    case 3u: result_type = unit_combat::AttackResultType::kParry; break;
    case 4u: result_type = unit_combat::AttackResultType::kBlock; break;
    case 5u: result_type = unit_combat::AttackResultType::kEvade; break;
    case 6u: result_type = unit_combat::AttackResultType::kImmune; break;
    case 7u:
    case 8u: result_type = unit_combat::AttackResultType::kDeflect; break;
    default: break;
  }

  const unit_combat::AttackResultContext context{
      .active_player_guid = local_guid,
      .attacker_guid = owner_.GetGuid(),
      .victim_guid = ObjectGuid(pending_combat_audio_.victim_guid),
      .hit_flags = pending_combat_audio_.hit_info,
      .damage = static_cast<std::int32_t>(pending_combat_audio_.damage),
      .result_type = result_type,
  };
  const auto action = unit_combat::DetermineAttackResultDisplay(context);

  if (action.play_weapon_impact_sound) {
    audio::WeaponImpactItemData attacker_item{};
    const audio::WeaponImpactItemData *attacker_item_ptr = nullptr;
    if (weapon.has_value() &&
        weapon->item_class == static_cast<std::uint32_t>(ItemClass::Weapon)) {
      attacker_item.item_class = static_cast<std::uint8_t>(weapon->item_class);
      attacker_item.fallback_weapon_subclass_id =
          static_cast<std::uint8_t>(weapon->subclass);
      attacker_item.weapon_subclass_id = weapon->sound_override >= 0
                                             ? static_cast<std::uint8_t>(weapon->sound_override)
                                             : 0xFFu;
      attacker_item.material_id = static_cast<std::uint8_t>(
          std::max(weapon->material, 0));
      attacker_item_ptr = &attacker_item;
    }
    owner_.sound_runtime().PlayWeaponImpactSound(
        audio::ResolveWeaponImpactSelectionForImpactSlot(
            attacker_item_ptr, audio::kWeaponImpactDefaultSlot, 0u),
        action.is_crit, sound_position, use_listener_priority);
  }
  if (action.play_miss_sound) {
    const bool is_one_handed =
        !weapon.has_value() ||
        weapon->inventory_type !=
            static_cast<std::uint32_t>(InventoryType::TwoHand);
    (void)PlayCombatMissSound(owner_.sound_runtime(), is_one_handed,
                              sound_position, use_listener_priority);
  }

  pending_combat_audio_ = {};
}

void UnitAnimationRuntime::HandlePlaybackCompletion(
    const WorldSession &session, const std::uint64_t request_serial,
    const std::uint16_t animation_id) {

  if (playback_request_.serial != request_serial ||
      playback_request_.animation_id != animation_id) {
    return;
  }

  if (pending_protected_playback_.has_value() &&
      !IsLoopingCombatAnimationBehavior(
          ResolveAnimationBehaviorId(owner_, animation_id))) {
    pending_protected_playback_.reset();
  }
  HandleAnimSequenceEnd(session, static_cast<std::uint32_t>(current_anim_group_),
                        animation_id, animation_id, false);
}

bool UnitAnimationRuntime::IsPlayingUsingAnimation() const {
  if (!owner_.Presentation().EnsureModelReady()) {
    return false;
  }

  const std::uint32_t behavior_id = [&]() {
    if (current_anim_group_ < 0) {
      return kInvalidAnimationBehaviorId;
    }

    const std::uint32_t requested_animation_id =
        static_cast<std::uint32_t>(current_anim_group_);
    const std::uint32_t resolved_animation_id =
        ResolveAnimationId(requested_animation_id, owner_.GetPrimaryM2InstanceId());
    if (resolved_animation_id >= kInvalidAnimationBehaviorId) {
      return kInvalidAnimationBehaviorId;
    }
    const auto *const dbc = owner_.dbc_loader();
    const auto *const entry =
        dbc != nullptr
            ? dbc->animation_data().LookupEntry(resolved_animation_id)
            : nullptr;
    return entry != nullptr ? entry->behavior_id
                            : kInvalidAnimationBehaviorId;
  }();

  if (IsUnarmedCombatBehavior(behavior_id)) {
    return true;
  }

  switch (behavior_id) {
  case 10u:
  case 16u:
  case 17u:
  case 18u:
  case 19u:
  case 20u:
  case 21u:
  case 22u:
  case 23u:
  case 24u:
  case 30u:
  case 36u:
  case 57u:
  case 58u:
  case 59u:
  case 85u:
  case 86u:
  case 87u:
  case 88u:
  case 95u:
  case 117u:
  case 118u:
  case 170u:
  case 171u:
  case 172u:
  case 173u:
  case 174u:
  case 175u:
  case 176u:
  case 177u:
  case 178u:
  case 179u:
  case 212u:
    return owner_.State().GetVirtualItemSlotEntry(0) == 0;
  default:
    return false;
  }
}

bool UnitAnimationRuntime::IsAnimationResolutionModelReady(
    const std::uint32_t target_instance_id) const {
  auto *const system = owner_.m2_system();
  if (system == nullptr || target_instance_id == 0u) {
    return false;
  }

  if (animation_model_ready_instance_id_ == target_instance_id) {
    return true;
  }
  const auto readiness = system->QueryInstanceReadiness(target_instance_id);
  if (readiness.status != openwow::render::m2::M2ResultStatus::kReady ||
      !readiness.render_ready) {
    return false;
  }
  animation_model_ready_instance_id_ = target_instance_id;
  return true;
}

std::uint32_t UnitAnimationRuntime::ResolveAnimationId(
    const std::uint32_t anim_id,
    const std::uint32_t override_instance_id) const {
  const std::uint32_t primary_instance_id = owner_.GetPrimaryM2InstanceId();
  if (primary_instance_id == 0) {
    return anim_id;
  }

  const std::uint32_t target_instance_id =
      override_instance_id != 0 ? override_instance_id : primary_instance_id;

  const auto *const dbc = owner_.dbc_loader();
  if (dbc == nullptr) {
    return anim_id;
  }
  if (dbc->animation_data().empty()) {
    return anim_id;
  }

  if (!IsAnimationResolutionModelReady(target_instance_id)) {
    return static_cast<std::uint16_t>(anim_id);
  }

  auto &memo =
      resolved_animation_memo_[anim_id & (kResolvedAnimationMemoSlotCount - 1u)];
  if (memo.valid && memo.anim_id == anim_id &&
      memo.target_instance_id == target_instance_id && memo.dbc == dbc &&
      memo.tier == cached_anim_tier_) {
    return memo.result;
  }

  const std::uint32_t result =
      WalkAnimationDataFallback(anim_id, target_instance_id);
  memo = {
      .valid = true,
      .anim_id = anim_id,
      .target_instance_id = target_instance_id,
      .dbc = dbc,
      .tier = cached_anim_tier_,
      .result = result,
  };
  return result;
}

std::uint32_t UnitAnimationRuntime::WalkAnimationDataFallback(
    const std::uint32_t anim_id, const std::uint32_t target_instance_id) const {
  using namespace unit_animation_resolution;

  const auto &animation_db = owner_.dbc_loader()->animation_data();

  const auto model_supports_animation =
      [system = owner_.m2_system(),
       target_instance_id](const std::uint32_t animation_id) {
        return system->InstanceModelHasAnimation(target_instance_id,
                                                 animation_id);
      };

  std::uint32_t result_id = static_cast<std::uint16_t>(anim_id);
  const std::uint32_t start_anim_id = anim_id;

  std::uint32_t variation = cached_anim_tier_;
  for (;;) {
    bool visited[kMaxAnimationId] = {};
    std::uint32_t current_id = start_anim_id;

    for (;;) {
      std::uint32_t candidate = 0;
      if (AnimationDataDB_FindByBehaviorAndTier(animation_db, current_id,
                                                 variation, candidate)) {
        if (model_supports_animation(candidate)) {
          return candidate;
        }
      }

      const auto *entry = animation_db.LookupEntry(current_id);

      if (current_id >= kMaxAnimationId || visited[current_id] ||
          entry == nullptr || entry->fallback == current_id) {
        std::uint32_t default_id = 0;
        if (AnimationDataDB_FindDefaultForVariation(
                animation_db, model_supports_animation, variation, default_id)) {
          return default_id;
        }

        const std::uint32_t parent_variation =
            variation < kAnimationTierCount ? kAnimationTierParent[variation] : 0u;
        if (parent_variation == variation) {
          return result_id;
        }
        variation = parent_variation;
        break;
      }

      visited[current_id] = true;
      current_id = entry->fallback;
    }
  }
}

bool UnitAnimationRuntime::AnimationSequenceLoops(
    const std::uint32_t animation_id) const {
  auto *const system = owner_.m2_system();
  const std::uint32_t instance_id = owner_.GetPrimaryM2InstanceId();
  if (system != nullptr && instance_id != 0u) {
    const auto model = system->QueryInstanceModel(instance_id);
    if (model.status == render::m2::M2ResultStatus::kReady &&
        model.model_id != 0u) {
      const auto sequence = system->QueryModelAnimationSequence(
          model.model_id, ResolveAnimationId(animation_id, instance_id));
      if (sequence.status == render::m2::M2ResultStatus::kReady &&
          sequence.has_sequence) {
        return (sequence.sequence.flags &
                ::openwow::data::model::kM2SequenceFlagPlayOnce) == 0u;
      }
    }
  }
  return IsLoopingCombatOrReadyStanceBehavior(animation_id) ||
         IsEmoteDance(animation_id);
}

void UnitAnimationRuntime::ClearSelectedStandAnimationState() {
  const auto cleared = selected_stand_animation_id_;
  previous_selected_stand_animation_id_ = selected_stand_animation_id_;
  selected_stand_animation_id_.reset();
  selected_stand_animation_flags_ = 0u;
  if (cleared.has_value() &&
      current_anim_group_ == static_cast<std::int32_t>(*cleared)) {
    current_anim_group_ = -1;
  }
}

void UnitAnimationRuntime::SetSelectedStandAnimationState(
    const std::uint16_t animation_id,
    const std::uint32_t animation_flags) {
  previous_selected_stand_animation_id_ = selected_stand_animation_id_;
  selected_stand_animation_id_ = animation_id;
  selected_stand_animation_flags_ = animation_flags;
}

void UnitAnimationRuntime::ApplySelectedStandAnimation(
    const std::uint16_t animation_id,
    const std::uint32_t animation_flags) {

  if (pending_deferred_animation_id_ != -1 &&
      pending_deferred_animation_id_ == static_cast<std::int32_t>(animation_id)) {
    deferred_animation_satisfied_id_ = static_cast<std::int32_t>(animation_id);
  }

  if (!IsPrimaryM2ModelStreamedFor(owner_)) {
    pending_deferred_animation_id_ = static_cast<std::int32_t>(animation_id);
    return;
  }
  pending_deferred_animation_id_ = -1;
  SetSelectedStandAnimationState(animation_id, animation_flags);

  const bool looping = AnimationSequenceLoops(animation_id);
  RequestPlayback(animation_id, looping, !looping,
                  EmoteAnimationFlagsBypassAliasResolution(animation_flags));
}

void UnitAnimationRuntime::UpdatePendingFallAnimation(
    const std::uint32_t previous_movement_flags,
    const std::uint32_t current_movement_flags) {
  constexpr std::uint32_t kPendingFallAnimation = 0x00002000u;
  const bool was_falling =
      (previous_movement_flags & kMoveFlagFalling) != 0u;
  const bool is_falling = (current_movement_flags & kMoveFlagFalling) != 0u;
  if (!is_falling) {
    emote_internal_flags_ &= ~kPendingFallAnimation;
    return;
  }
  if (!was_falling) {
    if (owner_.GetMovementInfo().HasFallingLaunchVelocity()) {
      emote_internal_flags_ &= ~kPendingFallAnimation;
      return;
    }
    emote_internal_flags_ |= kPendingFallAnimation;
  }
  if ((emote_internal_flags_ & kPendingFallAnimation) != 0u) {
    TryPlayPendingFallAnimation();
  }
}

void UnitAnimationRuntime::TryPlayPendingFallAnimation() {
  constexpr std::uint32_t kPendingFallAnimation = 0x00002000u;
  if ((emote_internal_flags_ & kPendingFallAnimation) == 0u || owner_.State().IsDead()) {
    return;
  }
  const auto *const passenger = owner_.Vehicle().GetVehiclePassengerComponent();
  if (owner_.Vehicle().VehicleSuppressesTransitionAnimation(owner_) ||
      (passenger != nullptr &&
       passenger->GetTransitionState() !=
           VehiclePassengerTransitionType::kExit)) {
    return;
  }
  PlayEmoteAnimation(kFallAnimationId, 0u);
  emote_internal_flags_ &= ~kPendingFallAnimation;
}

bool UnitAnimationRuntime::IsAnimationUpdateSuppressed() const {
  constexpr std::size_t kVehicleRuntimeFlagsOffset = 0x58u;
  constexpr std::uint32_t kVehicleRuntimeAnimOverride = 0x04000000u;
  constexpr std::uint32_t kVehicleEntryAnimControl = 0x00010000u;
  const auto *const vehicle_data =
      static_cast<const std::byte *>(owner_.Vehicle().GetVehicleData());
  if (vehicle_data != nullptr && owner_.Vehicle().GetVehicleEntry() != nullptr) {
    std::uint32_t runtime_flags = 0u;
    std::memcpy(&runtime_flags, vehicle_data + kVehicleRuntimeFlagsOffset,
                sizeof(runtime_flags));
    if ((runtime_flags & kVehicleRuntimeAnimOverride) != 0u ||
        ((owner_.Vehicle().GetVehicleEntry()->flags & kVehicleEntryAnimControl) != 0u &&
         ((emote_internal_flags_ & 0x400u) != 0u ||
          spell_visual_persist_anim_id_ != -1))) {
      return true;
    }
  }
  const auto *const objects = owner_.object_manager();
  const auto *const creature =
      objects != nullptr && owner_.GetEntry() != 0u
          ? objects->query_cache().GetCreatureTemplate(owner_.GetEntry())
          : nullptr;
  if (creature != nullptr) {
    return (creature->type_flags & 0x8u) != 0u;
  }
  const auto current_animation = GetCurrentAnimationId();
  return current_animation.has_value() && *current_animation == 121u;
}

bool UnitAnimationRuntime::IsRangedAttackOrSitSleepBehavior(
    const std::uint32_t animation_id) const {
  const auto *const dbc = owner_.dbc_loader();
  const auto *const entry =
      dbc != nullptr ? dbc->animation_data().LookupEntry(animation_id)
                     : nullptr;
  if (entry == nullptr) {
    return false;
  }
  switch (entry->behavior_id) {
  case 46u:
  case 49u:
  case 105u:
  case 106u:
  case 107u:
  case 108u:
  case 109u:
  case 110u:
  case 111u:
  case 112u:
    return true;
  default:
    return false;
  }
}

bool UnitAnimationRuntime::IsLoopingCombatOrReadyStanceBehavior(
    std::uint32_t animation_id) const {
  const auto *const dbc = owner_.dbc_loader();
  if (dbc == nullptr) {
    return false;
  }
  const auto *entry = dbc->animation_data().LookupEntry(animation_id);
  if (entry == nullptr) {
    return false;
  }
  switch (entry->behavior_id) {
  case 10u:
  case 16u:
  case 17u:
  case 18u:
  case 19u:
  case 20u:
  case 21u:
  case 22u:
  case 23u:
  case 24u:
  case 30u:
  case 36u:
  case 51u:
  case 52u:
  case 57u:
  case 58u:
  case 59u:
  case 85u:
  case 86u:
  case 87u:
  case 88u:
  case 95u:
  case 117u:
  case 118u:
  case 170u:
  case 171u:
  case 172u:
  case 173u:
  case 174u:
  case 175u:
  case 176u:
  case 177u:
  case 178u:
  case 179u:
  case 212u:
    return true;
  default:
    return entry->behavior_id >= 25 && entry->behavior_id <= 29;
  }
}

void UnitAnimationRuntime::RunPendingStandSelectorRefresh(
    const WorldSession &session) {

  if (!stand_selector_refresh_pending_ &&
      !IsRestPoseStaleForFlags(ResolveSelectorMovementFlags())) {
    return;
  }
  stand_selector_refresh_pending_ = false;
  RefreshSelectedStandAnimation(session, 0u, ~0u);
}

bool UnitAnimationRuntime::IsRestPoseStaleForFlags(
    const std::uint32_t movement_flags) const {

  if ((movement_flags & (kMoveFlagFalling | kMoveFlagFallingFar)) != 0u) {
    const auto current = GetCurrentAnimationId();
    const bool preserve =
        current.has_value() &&
        IsMovementStandPreservingBehaviorId(
            ResolveAnimationBehaviorId(owner_, *current));
    return !preserve;
  }

  const bool aquatic =
      (movement_flags & (kMoveFlagSwimming | kMoveFlagFlying)) != 0u;
  const bool has_directional =
      (movement_flags & (kMoveFlagForward | kMoveFlagBackward |
                         kMoveFlagStrafeLeft | kMoveFlagStrafeRight)) != 0u;
  switch (playback_request_.animation_id) {
  case render::AnimId::kWalk:
  case render::AnimId::kRun:
  case render::AnimId::kWalkBackwards:
  case render::AnimId::kStealthWalk:
  case render::AnimId::kSprint:

    return aquatic || !has_directional;
  case render::AnimId::kSwim:
  case render::AnimId::kSwimLeft:
  case render::AnimId::kSwimRight:
  case render::AnimId::kSwimBackwards:
    return !aquatic;
  case render::AnimId::kFly:
    return (movement_flags & kMoveFlagFlying) == 0u;
  case static_cast<std::uint16_t>(kFallAnimationId):

    return true;
  case render::AnimId::kStand:
    return aquatic;
  case render::AnimId::kSwimIdle:
    return !aquatic;
  default:
    return false;
  }
}

bool UnitAnimationRuntime::IsEmoteDance(const std::uint32_t animation_id) const {
  const auto *const dbc = owner_.dbc_loader();
  const auto *const entry =
      dbc != nullptr ? dbc->animation_data().LookupEntry(animation_id)
                     : nullptr;

  return entry != nullptr &&
         (entry->behavior_id == 69u || entry->behavior_id == 211u);
}

bool UnitAnimationRuntime::IsEmoteTalk(const std::uint32_t animation_id) const {
  const auto *const dbc = owner_.dbc_loader();
  const auto *const entry =
      dbc != nullptr ? dbc->animation_data().LookupEntry(animation_id)
                     : nullptr;
  return entry != nullptr && entry->behavior_id >= 8u &&
         entry->behavior_id <= 10u;
}

void UnitAnimationRuntime::UpdateMountAndPassengerAnimations() {
  const auto instance_id = owner_.Mount().OverlayM2InstanceId();
  if (instance_id == 0u) {
    return;
  }
  const std::int32_t animation_id = owner_.State().IsInCombat() ? 18 : 29;
  (void)SetAnimationRecursive(instance_id, animation_id, 0u, 0, 0, 1.0f, 0,
                              1, false);
}

void UnitAnimationRuntime::ApplySplineAnimationTier(const std::uint8_t tier) {
  if (tier == cached_anim_tier_) {
    return;
  }
  const bool was_airborne =
      cached_anim_tier_ == 2u || cached_anim_tier_ == 3u;
  const bool enters_airborne = tier == 2u || tier == 3u;
  if (enters_airborne && cached_anim_tier_ == 0u) {
    ApplySelectedStandAnimation(458u, 0u);
  } else if (tier == 0u && was_airborne) {
    ApplySelectedStandAnimation(460u, 0u);
  }
  cached_anim_tier_ = tier;
}

void UnitAnimationRuntime::ResetAuraAnimationVisualState(const WorldSession &session) {
  owner_.Auras().ClearAnimFlags();
  owner_.State().ClearSpellStateFlags(0x4000u);
  if (owner_.Interaction().AutoAttackType() == 0u) {
    owner_.Interaction().CompleteAutoAttackInteraction(false, true);
  }
  const auto animation_id = GetCurrentAnimationId();
  if (animation_id.has_value() && *animation_id == 161u) {
    return;
  }
  current_anim_group_ = -1;
  RestoreStandAnimationAfterEffect(session);
}

void UnitAnimationRuntime::RestoreStandAnimationAfterEffect(
    const WorldSession &session) {
  UpdateStandAnimation(session, -1, ResolveStandAnimationRequestId());
}

void UnitAnimationRuntime::ApplyRequestedStandState(WorldSession &session,
                                        const std::uint8_t stand_state) {
  const auto previous_state = GetStandState();
  if (previous_state == stand_state) {
    return;
  }
  ValuesUpdate update;
  update.guid = owner_.GetGuid();
  update.fields.field_count = owner_.GetFieldCount();
  update.fields.bitmask.assign(BitmaskBlockCount(update.fields.field_count),
                               0u);
  const auto bytes = owner_.GetUInt32(UNIT_FIELD_BYTES_1);
  SetUpdatedFieldValue(update.fields, UNIT_FIELD_BYTES_1,
                       (bytes & 0xFFFFFF00u) | stand_state);
  owner_.ApplyValuesUpdate(update);

  ApplyRequestedStandStateSideEffects(session, stand_state);

  HandleStandStateTransition(session, previous_state);
  RefreshCameraBoundModelDisplayIfTargeted(session.world_camera());
}

std::uint8_t UnitAnimationRuntime::GetShapeshiftForm() const {
  return static_cast<std::uint8_t>(owner_.GetUInt32(UNIT_FIELD_BYTES_2) >> 24u);
}

bool UnitAnimationRuntime::EmoteStateCheck(const std::uint16_t animation_flags,
                               std::uint32_t *const out_animation_id) const {

  const bool fallback = (animation_flags & kStandSelectorEmoteAnimFlag) == 0u;

  if ((emote_internal_flags_ & kEmoteStateResolveEnabledFlag) == 0u &&
      ((emote_internal_flags_ & kEmoteStateResolveModelGatedFlag) == 0u ||
       owner_.GetUInt32(UNIT_FIELD_MOUNTDISPLAYID) != 0u)) {
    return fallback;
  }
  const auto emote_state = GetEmoteState();
  const auto *const entry =
      emote_state != 0u ? LookupEmoteStateEntry(emote_state) : nullptr;

  const auto *const objects = owner_.object_manager();
  const bool is_npc_interaction_target =
      objects != nullptr && objects->GetNpcGuid().GetRawValue() != 0u &&
      owner_.GetGuid() == objects->GetNpcGuid();
  if (entry == nullptr ||
      (is_npc_interaction_target &&
       (entry->flags & kEmoteRowSkipForNpcInteractionTargetFlag) != 0u)) {
    return fallback;
  }
  if ((animation_flags & kStandSelectorEmoteAnimFlag) != 0u &&
      out_animation_id != nullptr) {
    *out_animation_id = entry->anim_id;
  }
  return true;
}

void UnitAnimationRuntime::RefreshSpellVisualStandAnimationState(
    const WorldSession &session) {
  RefreshSelectedStandAnimation(session, 0u, ~0u);
}

void UnitAnimationRuntime::ApplySpellVisualKitAnimation(
    const WorldSession &session, const std::uint32_t kit_id,
    const std::uint32_t dispatch_type, const std::uint32_t spell_id) {
  const auto *const dbc = owner_.dbc_loader();
  const auto *const kit =
      dbc != nullptr ? dbc->spell_visual_kit().LookupEntry(kit_id) : nullptr;
  if (kit == nullptr) {
    return;
  }

  auto animation_flags =
      MapSpellVisualKitFlagsToEmoteAnimationFlags(kit->flags);

  const auto *const dispatch_spell =
      (spell_id != 0u && dbc != nullptr) ? dbc->spell().LookupEntry(spell_id)
                                         : nullptr;
  if (dispatch_spell != nullptr &&
      (dispatch_spell->attributes & 0x00040000u) != 0u) {
    animation_flags |= kEmoteAnimationFlagSuppressSheatheUpdate;
  }

  const auto *const dispatch_visual =
      (dispatch_spell != nullptr && dispatch_spell->spell_visual[0] != 0u &&
       dbc != nullptr)
          ? dbc->spell_visual().LookupEntry(dispatch_spell->spell_visual[0])
          : nullptr;
  if (dispatch_visual != nullptr && (dispatch_visual->missile_model == -1 ||
                                     dispatch_visual->missile_model == -2)) {
    animation_flags |= kEmoteAnimationFlagSuppressSheatheUpdate;
  }

  const std::int32_t start_animation = kit->start_anim_id;
  const std::int32_t body_animation = kit->anim_id;

  if (dispatch_type == 4u &&
      (start_animation > 0 || body_animation > 0)) {
    SetChannelingActionLock(true);
  }

  if (dispatch_type == 4u && start_animation >= 0) {
    PlayEmoteAnimation(start_animation, animation_flags);
    emote_internal_flags_ |= kEmoteInternalFlagUseSpellVisualStartAnimation;
    return;
  }
  if (body_animation < 0) {
    return;
  }

  const bool selects_start_animation = dispatch_type == 4u;
  const bool cast_animation_already_playing =
      [&]() {
        if (dispatch_visual == nullptr || dbc == nullptr) return false;
        const auto *const cast_kit =
            dbc->spell_visual_kit().LookupEntry(dispatch_visual->cast_kit);
        if (cast_kit == nullptr || cast_kit->anim_id < 1) return false;
        const auto current = GetCurrentAnimationId();
        return current.has_value() &&
               static_cast<std::int32_t>(*current) == cast_kit->anim_id;
      }();
  if (selects_start_animation &&
      (cast_animation_already_playing ||
       !SpellHasPositiveCastDuration(owner_, session, dispatch_spell))) {
    return;
  }

  const auto body_animation_id =
      static_cast<std::uint32_t>(body_animation);
  const auto behavior =
      ResolveAnimationBehaviorId(owner_, body_animation_id);

  if (dispatch_type == 2u) {
    const auto current = GetCurrentAnimationId();
    if (current.has_value() && *current == body_animation_id) {
      return;
    }
    const bool direct_death_family =
        behavior == 1u || behavior == 6u || behavior == 131u ||
        behavior == 132u || behavior == 466u || behavior == 467u ||
        behavior == 468u || behavior == 472u;
    if (direct_death_family) {
      PlayEmoteAnimation(body_animation, animation_flags);
    }
    RefreshSelectedStandAnimation(session, animation_flags, ~0u);
    return;
  }

  if (behavior >= 8u && behavior <= 10u) {

    PlayWoundReaction(session, body_animation_id == 10u);
    return;
  }

  PlayEmoteAnimation(body_animation, animation_flags);
}

void UnitAnimationRuntime::EndSpellVisualStandAnimation(
    const WorldSession &session) {

  emote_internal_flags_ &= ~kEmoteInternalFlagUseSpellVisualStartAnimation;
  ClearSelectedStandAnimationState();
  current_anim_group_ = -1;
  RefreshSelectedStandAnimation(session, 0u, ~0u);
}

void UnitAnimationRuntime::SetMovementAnimData(const std::uint8_t alpha,
                                   const std::uint32_t animation_id,
                                   const std::int32_t start,
                                   const std::int32_t duration,
                                   const std::int32_t flags) {
  if (animation_id >= 12u) {
    return;
  }
  move_anim_alpha_ = alpha;
  move_anim_id_ = animation_id;
  move_anim_timestamp_ = static_cast<std::uint32_t>(start);
  move_anim_end_time_ = static_cast<std::uint32_t>(start + duration);
  move_anim_flags_ = flags;
}

void UnitAnimationRuntime::SetLootTargetAndPlayLootAnimation(const std::uint64_t target_guid) {
  SetStandSelectionInteractionTarget(target_guid);
  if (IsLootTargetAnimatable()) {
    PlayEmoteAnimation(static_cast<std::int32_t>(kLootAnimationId), 0u);
  }
}

void UnitAnimationRuntime::SetStandSelectionInteractionTarget(
    const std::uint64_t target_guid) {
  stand_selection_interaction_target_guid_ = target_guid;
}

void UnitAnimationRuntime::ClearStandSelectionInteractionTargetAndRefresh(
    const WorldSession &session) {
  stand_selection_interaction_target_guid_ = 0u;

  RefreshSelectedStandAnimation(session, 0u, ~0u);
}

void UnitAnimationRuntime::UpdateStandAnimation(const WorldSession &session,
                                    std::int32_t anim_group,
                                    std::uint32_t requested_animation_id) {
  (void)anim_group;
  if (requested_animation_id == kStandAnimationCustomRequestId) {
    return;
  }
  RefreshSelectedStandAnimation(session, 0u, ~0u);
}

void UnitAnimationRuntime::ApplyRequestedStandStateSideEffects(
    WorldSession &session, const std::uint8_t stand_state) {
  if (owner_.GetGuid() != CGObject_C::GetActivePlayerGuid()) {
    return;
  }
  if (stand_state == 0u) {
    if (auto* const input = GetInputControlSingleton(); input != nullptr) {
      input->ProcessMovementNow(openwow::core::GameClock::GetTickCount32(), true);
    }
  } else {
    if (owner_.IsActivePlayer() && !owner_.Casts().GetComboTarget().IsEmpty()) {
      CloseActiveLootWindow(session, CloseLootWindowOptions{
          .send_release = true, .skip_item_check = true,
          .show_interrupted = false, .clear_dead_target = true});
    }
    if (!owner_.Interaction().AutoAttackTarget().IsEmpty()) {
      owner_.Interaction().CompleteAutoAttackInteraction(false, true);
    }
  }
  if (stand_state < 4u || stand_state > 6u) {
    (void)BarberShop::Get().Cancel(session);
  }
}

bool UnitAnimationRuntime::ShouldSuppressStandStateTransitionAnimation() const {
  return owner_.Vehicle().HasAttachedVehiclePassenger() && owner_.Vehicle().VehicleSuppressesTransitionAnimation(owner_);
}

void UnitAnimationRuntime::RefreshCameraBoundModelDisplayIfTargeted(
    openwow::world::WorldCamera *camera) const {
  if (camera != nullptr && camera->bound_object() == owner_.GetGuid().GetRawValue()) {
    ui::game::GameUI_GetUnitModelDisplay(owner_.GetGuid().GetRawValue());
  }
}

void UnitAnimationRuntime::HandleStandStateTransition(
    WorldSession &session, const std::uint8_t previous_stand_state) {
  const auto stand_state = GetStandState();
  if (sheathe_state_ != 0 && stand_state != 0u && stand_state != 2u) {
    ChangeSheatheStateAndNotifyServer(0, true, false);
  }
  if (stand_state == kStandStateDead) {
    owner_.Interaction().CancelSpellCastsOnUnitDeath(session);
    PlayDeadTransitionAnimation(session, false);
    return;
  }
  if (stand_state == kTransitionStandStateId) {
    if (!ShouldSuppressStandStateTransitionAnimation()) {
      PlayEmoteAnimation(kStandTransitionEntryAnimationId, 0u);
    }
    return;
  }
  if (stand_state == 0u && previous_stand_state == kTransitionStandStateId) {

    if (!ShouldSuppressStandStateTransitionAnimation() &&
        IsPrimaryM2ModelStreamedFor(owner_)) {
      PlayEmoteAnimation(
          PrimaryM2ModelContainsAnimation(owner_,
                                          kStandTransitionExitFallbackAnimationId)
              ? kStandTransitionExitFallbackAnimationId
              : kStandTransitionExitAnimationId,
          0u);
    }
    return;
  }

  emote_internal_flags_ |= kEmoteStateResolveModelGatedFlag;
  RefreshSelectedStandAnimation(session, 0u, ~0u);
}

void UnitAnimationRuntime::PlayDeadTransitionAnimation(const WorldSession &session,
                                           const bool force_replay) {
  if (!force_replay && death_transition_played_) {
    return;
  }
  death_transition_played_ = true;
  std::uint16_t animation_id = kDeadTransitionAnimationId;
  const auto *spline = GetActiveMovementSpline(session, owner_);
  if (owner_.GetMovementInfo().HasFlag(kMoveFlagSwimming)) {
    animation_id = kAlternateDeadTransitionAnimationId;
  } else if (spline != nullptr && ShouldUseHoverStandAnimation(session)) {
    const auto support_surface_height = ResolveDeadTransitionSupportSurfaceHeight(owner_);
    if (support_surface_height.has_value()) {
      const auto position = owner_.GetPosition();
      if (*support_surface_height - owner_.Presentation().ModelHeight() * 0.5f + 0.1f >= position.z) {
        animation_id = kAlternateDeadTransitionAnimationId;
      }
    }
  }
  PlayEmoteAnimation(static_cast<std::int32_t>(animation_id), 0u);
}

void UnitAnimationRuntime::HandleMovementOpcodeAnimationSideEffects(
    const WorldSession &session, const std::uint32_t movement_opcode) {
  const auto refresh = [this, &session]() { RefreshSelectedStandAnimation(session, 0u, ~0u); };
  const auto reset_and_refresh = [this, &session, &refresh]() {
    ResetAuraAnimationVisualState(session);
    refresh();
  };
  const auto refresh_if_flags = [this, &refresh]() {
    if ((owner_.State().GetUnitFlags() & kMovementOpcodeStandRefreshUnitFlagMask) != 0u) refresh();
  };
  const auto play_transition = [this](const std::int32_t animation_id) {
    if (!owner_.Vehicle().VehicleSuppressesTransitionAnimation(owner_)) PlayEmoteAnimation(animation_id, 0u);
  };
  switch (movement_opcode) {
  case 181u: case 182u: case 184u: case 185u: case 202u: case 833u: case 857u: case 935u:
    reset_and_refresh(); return;
  case 183u: case 186u: case 188u: case 189u: case 190u: case 197u: case 199u:
  case 203u: case 217u: case 233u: case 236u: case 794u: case 834u: case 858u:
  case 941u: case 1231u: case 1233u: case 1235u: case 1236u:
    refresh(); return;
  case 194u: case 195u: case 227u: case 229u: case 231u: case 731u: case 733u:
  case 898u: case 900u:
    refresh_if_flags(); return;
  case 187u:
    ResetAuraAnimationVisualState(session);
    play_transition(kFlightTransitionTakeoffAnimationId);
    return;
  case 240u:
    play_transition(kFallAnimationId); return;
  case 837u:
    if ((owner_.GetMovementInfo().flags & kMoveFlagCanFly) == 0u)
      emote_internal_flags_ &= ~kEmoteInternalFlagFlightTransitionLock;
    return;
  case 838u:
    if ((owner_.GetMovementInfo().flags & kMoveFlagFlying) != 0u) {
      owner_.Movement().ResetFlightTransitionBodyLeanState();
      if (const auto current = GetCurrentAnimationId();
          current.has_value() && *current == kFallAnimationId) {
        refresh(); return;
      }
      if (const auto mount = owner_.Mount().ModelDefaultAnimationId();
          mount.has_value() && *mount == static_cast<std::uint16_t>(kFallAnimationId)) {
        refresh(); return;
      }
      play_transition(kFlightTransitionTakeoffAnimationId);
      emote_internal_flags_ |= kEmoteInternalFlagFlightTransitionLock;
      return;
    }
    play_transition(kFlightTransitionLandingAnimationId);
    emote_internal_flags_ &= ~kEmoteInternalFlagFlightTransitionLock;
    return;
  default:
    return;
  }
}

void UnitAnimationRuntime::PlayEmoteAnimation(std::int32_t emote_anim_id,
                                  std::uint32_t animation_flags) {
  if (emote_anim_id < 0) {

    current_anim_group_ = -1;
    stand_selector_refresh_pending_ = true;
    return;
  }

  if (!IsPrimaryM2ModelStreamedFor(owner_)) {
    pending_deferred_animation_id_ = emote_anim_id;
    return;
  }

  const bool looping =
      AnimationSequenceLoops(static_cast<std::uint32_t>(emote_anim_id));
  const bool submitted = RequestPlayback(
      static_cast<std::uint16_t>(emote_anim_id), looping, true,
      EmoteAnimationFlagsBypassAliasResolution(animation_flags));
  if (!submitted) {
    return;
  }

  SelectEmoteAnimation(
      (animation_flags & kEmoteAnimationFlagSuppressSheatheUpdate) != 0u);
}

bool UnitAnimationRuntime::IsEmoteAnimationStateBlocked() const {
  if (static_cast<std::int32_t>(owner_.State().GetHealth()) < 1) return true;
  if ((owner_.State().GetUnitFlags2() & kUnitFlags2FeignDeath) != 0u) return true;
  if (GetStandState() == kStandStateDead) return true;
  const auto current = GetCurrentAnimationId();
  return current.has_value() &&
         IsDeadAnimationFamily(ResolveAnimationBehaviorId(owner_, *current));
}

bool UnitAnimationRuntime::CanPlayEmoteAnimationNow(
    const WorldSession &session) const {
  if (IsEmoteAnimationStateBlocked()) return false;
  if (static_cast<std::int32_t>(owner_.State().GetHealth()) <= 0) return false;
  if (GetStandState() == kTransitionStandStateId) return false;

  const auto &movement_info = owner_.GetMovementInfo();
  const auto movement_flags = movement_info.flags;
  if ((movement_flags & kMoveFlagFalling) != 0u &&
      (movement_flags & kMoveFlagFallingFar) != 0u)
    return false;
  if (movement_info.HasFallingLaunchVelocity()) return false;
  if ((movement_flags & kDirectionalMovementMask) == 0u) return false;

  if (HasStandSelectionInteractionState() && IsLootTargetAnimatable()) return false;
  if (owner_.Casts().GetChannelSpellId(owner_) != 0u) return false;
  if (HasChannelingActionLock()) return false;
  if (owner_.Interaction().HasCachedUpdateTarget()) return false;
  if (HasTurnDrivenStandSelectorGate(session)) return false;
  if (sheathe_state_ == kSheatheStateRanged &&
      (emote_internal_flags_ & kEmoteFlagAutoRepeatActive) != 0u)
    return false;
  if (GetStandState() != 0u) return false;
  return previous_stand_state_category_ == 0u;
}

void UnitAnimationRuntime::PlayEmoteOnUnit(std::int32_t emote_dbc_id) {
  const auto *dbc = owner_.dbc_loader();
  if (dbc == nullptr) return;
  const auto *entry = dbc->emotes().LookupEntry(static_cast<std::uint32_t>(emote_dbc_id));
  if (entry == nullptr || entry->anim_id > 505u) return;
  const auto animation_id = static_cast<std::uint16_t>(entry->anim_id);

  const auto current = GetCurrentAnimationId();
  if (current.has_value() && *current == animation_id) return;
  if (owner_.GetUInt32(UNIT_CHANNEL_SPELL) != 0u || HasChannelingActionLock()) return;
  const bool flight_row =
      animation_id == kLiftOffAnimationId || animation_id == kLandAnimationId;
  const bool in_combat =
      (owner_.GetUInt32(UNIT_FIELD_FLAGS) & kUnitFieldFlagsInCombat) != 0u ||
      owner_.Interaction().HasCachedUpdateTarget();
  if (!flight_row && in_combat) return;
  PlayEmoteAnimation(static_cast<std::int32_t>(animation_id), 0u);
}

bool UnitAnimationRuntime::HasStandSelectionInteractionState() const {
  const auto* const objects = owner_.object_manager();
  const auto *player = objects != nullptr ? objects->GetLocalPlayerTyped() : nullptr;
  if (player != nullptr && owner_.GetGuid() == player->GetGuid())
    return stand_selection_interaction_target_guid_ != 0u;
  return (owner_.State().GetUnitFlags() & kUnitFlagLooting) != 0u;
}

bool UnitAnimationRuntime::ResolveLootStandAnimationOverride(
    const WorldSession &session, const std::uint32_t selector_flags,
    std::uint16_t *const out_animation_id) const {
  if (owner_.Vehicle().ResolveAttachedVehicleSeatEntry(owner_, session) != nullptr ||
      (emote_internal_flags_ & kEmoteStateResolveModelGatedFlag) == 0u ||
      !IsLootTargetAnimatable()) {
    return (selector_flags & kLootStandPassiveClaimMask) == 0u;
  }
  const auto current = GetCurrentAnimationId();
  const bool replay_requested = (selector_flags & kLootStandReplayFlag) != 0u;
  if (!HasStandSelectionInteractionState()) {
    if (!current.has_value() || *current != kLootHoldAnimationId)
      return (selector_flags & kLootStandPassiveClaimMask) == 0u;
    if (replay_requested && out_animation_id != nullptr)
      *out_animation_id = kLootUpAnimationId;
    return true;
  }

  const bool already_in_loot_family =
      current.has_value() && (*current == kLootAnimationId ||
                              *current == kLootHoldAnimationId);
  if (replay_requested && !already_in_loot_family && out_animation_id != nullptr)
    *out_animation_id = kLootAnimationId;
  return true;
}

bool UnitAnimationRuntime::TryResolveCachedTargetStandAnimation(
    const std::uint32_t selector_flags, std::uint32_t *out_animation_id,
    const std::uint8_t *anim_flags_ptr) const {
  if ((emote_internal_flags_ & kEmoteInternalFlagCachedTargetMask) == 0u ||
      !owner_.Interaction().HasCachedUpdateTarget())
    return (selector_flags & kCachedTargetStandPassiveMask) == 0u;
  if ((selector_flags & kCachedTargetStandSelectorBit) == 0u) return true;
  if (anim_flags_ptr != nullptr && (*anim_flags_ptr & 0x40u) != 0u) {
    const auto current = GetCurrentAnimationId();
    if (current.has_value() &&
        IsLoopingCombatAnimationBehavior(ResolveAnimationBehaviorId(owner_, *current))) {
      *out_animation_id = *current;
      return true;
    }
  }

  if (pending_protected_playback_.has_value()) {
    *out_animation_id = pending_protected_playback_->animation_id;
    return true;
  }
  if ((owner_.GetMovementInfo().flags & (kMoveFlagSwimming | kMoveFlagFlying)) != 0u) {
    *out_animation_id = render::AnimId::kSwimIdle;
    return true;
  }
  *out_animation_id = GetWeaponBasedReadyAnimationId();
  return true;
}

std::uint32_t UnitAnimationRuntime::GetWeaponBasedReadyAnimationId() const {
  std::optional<VisibleItemTemplateMetadata> visible_weapon;
  for (std::uint32_t slot = 0u; slot <= 1u; ++slot) {
    if (const auto metadata = GetVisibleWeaponMetadataForAnimation(
            owner_, static_cast<std::uint8_t>(slot));
        metadata.has_value() &&
        metadata->item_class == static_cast<std::uint32_t>(ItemClass::Weapon)) {
      visible_weapon = metadata;
      break;
    }
  }
  if (!visible_weapon.has_value()) {
    return render::AnimId::kReadyUnarmed;
  }

  const bool has_offhand =
      GetVisibleWeaponMetadataForAnimation(owner_, 1u).has_value();
  switch (visible_weapon->subclass) {
  case 0u: case 4u: case 7u: case 11u: case 13u: case 14u: case 15u: case 20u:
    return render::AnimId::kReady1H;
  case 1u: case 5u: case 8u: case 12u:
    return has_offhand ? render::AnimId::kReady1H : render::AnimId::kReady2H;
  case 6u: case 10u: case 17u:
    return render::AnimId::kReady2HL;
  default:
    return render::AnimId::kReadyUnarmed;
  }
}

std::optional<std::uint16_t>
UnitAnimationRuntime::GetWeaponBasedParryAnimationId() const {
  const auto main_hand = GetVisibleWeaponMetadataForAnimation(owner_, 0u);
  if (!main_hand.has_value() ||
      main_hand->item_class != static_cast<std::uint32_t>(ItemClass::Weapon) ||
      owner_.State().GetSheathState() == 0u) {
    return render::AnimId::kParryUnarmed;
  }
  const bool has_offhand =
      GetVisibleWeaponMetadataForAnimation(owner_, 1u).has_value();
  switch (main_hand->subclass) {
  case 0u: case 4u: case 7u: case 11u: case 14u: case 15u: case 20u:
    return render::AnimId::kParry1H;
  case 1u: case 5u: case 8u: case 12u:
    return has_offhand ? render::AnimId::kParry1H : render::AnimId::kParry2H;
  case 6u: case 10u: case 17u:
    return render::AnimId::kParry2HL;
  case 13u:
    return render::AnimId::kParryUnarmed;
  default:

    return std::nullopt;
  }
}

bool UnitAnimationRuntime::ResolveRangedAutoRepeatStandAnimation(
    const std::uint32_t selector_flags,
    std::uint16_t *const out_animation_id) const {
  if (sheathe_state_ != kSheatheStateRanged ||
      (emote_internal_flags_ & kEmoteFlagAutoRepeatActive) == 0u)
    return (selector_flags & kRangedHoldStandPassiveClaimMask) == 0u;
  if ((selector_flags & kRangedHoldStandReplayFlag) == 0u ||
      (emote_internal_flags_ & kEmoteInternalFlagRangedHoldSuppressed) != 0u)
    return true;
  if (out_animation_id == nullptr) return true;
  *out_animation_id = render::AnimId::kReadyUnarmed;
  const auto ranged = GetVisibleWeaponMetadataForAnimation(
      owner_, kRangedVisibleWeaponSlot);
  if (!ranged.has_value() ||
      ranged->item_class != static_cast<std::uint32_t>(ItemClass::Weapon))
    return true;
  switch (ranged->subclass) {
  case kItemSubclassWeaponBow:      *out_animation_id = kLoadBowAnimationId; break;
  case kItemSubclassWeaponGun:
  case kItemSubclassWeaponCrossbow: *out_animation_id = kLoadRifleAnimationId; break;
  case kItemSubclassWeaponThrown:   *out_animation_id = kLoadThrownAnimationId; break;
  case kItemSubclassWeaponWand:     *out_animation_id = kHoldThrownAnimationId; break;
  default: break;
  }
  return true;
}

std::optional<UnitAnimationRuntime::SpellVisualStandAnimationRecord>
UnitAnimationRuntime::ResolveActiveSpellVisualStandAnimationRecord() const {
  const auto *dbc = owner_.dbc_loader();
  if (dbc == nullptr) return std::nullopt;
  const auto resolve_for_spell =
      [this, dbc](const std::uint32_t spell_id, const bool use_precast_kit)
          -> std::optional<SpellVisualStandAnimationRecord> {
    if (spell_id == 0u) return std::nullopt;
    const auto *spell = dbc->spell().LookupEntry(spell_id);
    if (spell == nullptr) return std::nullopt;
    auto visual_id = spell->spell_visual[0];
    const auto quality_level = static_cast<std::int32_t>(
        openwow::core::DisplaySettingsController::Instance().GetQualityLevel());
    if (quality_level < 2 && spell->spell_visual[1] != 0u)
      visual_id = spell->spell_visual[1];
    if (visual_id == 0u) return std::nullopt;
    const auto *visual = dbc->spell_visual().LookupEntry(visual_id);
    if (visual == nullptr) return std::nullopt;
    const auto kit_id = use_precast_kit ? visual->precast_kit : visual->channel_kit;
    if (kit_id == 0u) return std::nullopt;
    const auto *kit = dbc->spell_visual_kit().LookupEntry(kit_id);
    if (kit == nullptr) return std::nullopt;
    const bool use_start_animation =
        use_precast_kit &&
        (emote_internal_flags_ &
         kEmoteInternalFlagUseSpellVisualStartAnimation) != 0u &&
        IsUsableSpellVisualAnimationId(kit->start_anim_id);
    const std::int32_t resolved_animation_id =
        use_start_animation ? kit->start_anim_id : kit->anim_id;
    if (!IsUsableSpellVisualAnimationId(resolved_animation_id))
      return std::nullopt;
    return SpellVisualStandAnimationRecord{
        .animation_id = static_cast<std::uint16_t>(resolved_animation_id),
        .kit_flags = kit->flags};
  };
  if (const auto resolved =
          resolve_for_spell(owner_.Casts().GetCurrentCast().spell_id, true);
      resolved.has_value()) return resolved;
  const auto descriptor_channel_spell_id = owner_.Casts().GetChannelSpellId(owner_);
  if (const auto resolved = resolve_for_spell(
          descriptor_channel_spell_id != 0u ? descriptor_channel_spell_id
                                             : owner_.Casts().GetChannelCast().spell_id,
          false);
      resolved.has_value()) return resolved;
  return std::nullopt;
}

bool UnitAnimationRuntime::ResolveStandStateTransitionAnimationOverride(
    const std::uint32_t selector_flags, std::uint16_t *const out_animation_id) const {
  if ((emote_internal_flags_ &
       (kEmoteInternalFlagAnimationBehavior127 | kEmoteInternalFlagAnimationBehavior201)) != 0u)
    return true;
  const bool declined = (selector_flags & ~kStandStateTransitionPassiveClaimMask) == 0u;
  if ((emote_internal_flags_ & kEmoteStateResolveModelGatedFlag) == 0u ||
      owner_.GetUInt32(UNIT_FIELD_MOUNTDISPLAYID) != 0u)
    return declined;
  std::uint16_t resolved_animation_id;
  if (GetStandState() == kTransitionStandStateId) {
    resolved_animation_id = previous_stand_state_category_ == kTransitionStandStateId
                                ? kStandTransitionLoopAnimationId
                                : kStandTransitionEntryAnimationId;
  } else if (previous_stand_state_category_ == kTransitionStandStateId) {
    resolved_animation_id =
        PrimaryM2ModelContainsAnimation(owner_, kStandTransitionExitAnimationId)
            ? kStandTransitionExitAnimationId
            : kStandTransitionExitFallbackAnimationId;
  } else {
    return declined;
  }
  if (!PrimaryM2ModelContainsAnimation(owner_, resolved_animation_id))
    return declined;
  const auto current = GetCurrentAnimationId();
  if ((selector_flags & kStandStateTransitionReplayFlag) != 0u &&
      (!current.has_value() || *current != resolved_animation_id) &&
      out_animation_id != nullptr)
    *out_animation_id = resolved_animation_id;
  return true;
}

bool UnitAnimationRuntime::ResolveSpellVisualStandAnimationOverride(
    const std::uint32_t selector_flags, std::uint32_t *const inout_animation_flags,
    std::uint16_t *const out_animation_id) const {
  const bool passive_claim = ShouldPassivelyClaimStandSelector(selector_flags);
  if (owner_.Casts().GetCurrentCast().spell_id == 0u &&
      owner_.Casts().GetChannelSpellId(owner_) == 0u &&
      owner_.Casts().GetChannelCast().spell_id == 0u && !HasChannelingActionLock())
    return passive_claim;

  const auto resolved = ResolveActiveSpellVisualStandAnimationRecord();
  if (!resolved.has_value()) return passive_claim;
  if (inout_animation_flags != nullptr)
    *inout_animation_flags |= MapSpellVisualKitFlagsToEmoteAnimationFlags(resolved->kit_flags);
  const auto current = GetCurrentAnimationId();
  const bool matches_current = current.has_value() && *current == resolved->animation_id;
  if (matches_current) {

    if (playback_request_.base_animation_id == kNoAnimationRow ||
        *current != playback_request_.base_animation_id) {
      if (out_animation_id != nullptr) *out_animation_id = resolved->animation_id;
      return true;
    }
    if (inout_animation_flags != nullptr &&
        ((*inout_animation_flags & kStandSelectorReplayFlag) != 0u) &&
        (resolved->kit_flags & kSpellVisualKitFlagReplayOnSelectorFlag) != 0u &&
        out_animation_id != nullptr)
      *out_animation_id = resolved->animation_id;
    return true;
  }
  if ((selector_flags & kStandSelectorReplayFlag) == 0u) return true;
  if (out_animation_id != nullptr) *out_animation_id = resolved->animation_id;
  return true;
}

bool UnitAnimationRuntime::HasTurnDrivenStandSelectorGate(const WorldSession &session) const {
  const auto &movement_info = owner_.GetMovementInfo();
  if ((movement_info.flags & (kMoveFlagTurnLeft | kMoveFlagTurnRight)) == 0u &&
      (emote_internal_flags_ & kEmoteInternalFlagStandSelectorTransitionMask) == 0u)
    return false;
  if (MatchesSplineAwareMovementGate(
          movement_info, GetActiveMovementSpline(session, owner_),
          kMoveFlagSwimming | kMoveFlagFlying | kMoveFlagHover))
    return false;
  if (owner_.Vehicle().VehicleSuppressesTransitionAnimation(owner_)) return false;
  if (const auto current = GetCurrentAnimationId();
      current.has_value() && IsStandSelectorBlockedAnimationBehavior(
                                 ResolveAnimationBehaviorId(owner_, *current)))
    return false;
  return (emote_internal_flags_ & kStandSelectorBlockedEmoteInternalFlagMask) == 0u;
}

bool UnitAnimationRuntime::HasMovementDrivenStandAnimationOverride(
    const WorldSession &session) const {
  const auto &movement_info = owner_.GetMovementInfo();

  if ((movement_info.flags & kMoveFlagFalling) != 0u &&
      ((movement_info.flags & kMoveFlagFallingFar) != 0u ||
       movement_info.jump.z_speed != 0.0f)) {
    return true;
  }
  static_cast<void>(session);
  return IsAirborneForAnimationSplit();
}

bool UnitAnimationRuntime::IsAirborneForAnimationSplit() const {
  const auto &movement = owner_.Movement();
  if (movement.HasNonExemptSplineFlag(SplineFlag::kFalling)) return true;
  return owner_.GetMovementInfo().transport.seat < 0 &&
         movement.HasNonExemptSplineFlag(SplineFlag::kParabolic);
}

std::uint32_t UnitAnimationRuntime::ResolveSelectorMovementFlags() const {
  auto flags = owner_.GetMovementInfo().flags;
  if (owner_.Movement().HasActiveSplineLocomotion()) {
    flags |= owner_.Movement().IsSplineLocomotionBackward() ? kMoveFlagBackward
                                                            : kMoveFlagForward;
  }
  return flags;
}

bool UnitAnimationRuntime::ResolveDirectionalLocomotionAnimation(
    const std::uint32_t movement_flags, const std::uint32_t selector_flags,
    std::uint16_t *const out_animation_id) const {

  constexpr std::uint32_t kDirectionalMask =
      kMoveFlagForward | kMoveFlagBackward | kMoveFlagStrafeLeft |
      kMoveFlagStrafeRight;
  if ((movement_flags & kDirectionalMask) == 0u) {
    return (selector_flags & ~kStandSelectorLocomotionPassiveClaimMask) == 0u;
  }

  if ((emote_internal_flags_ & kBaseAnimationStateMask) == 0u ||
      (selector_flags & kStandSelectorLocomotionClaimFlag) == 0u ||
      (emote_internal_flags_ & kEmoteInternalFlagFlightTransitionLock) != 0u ||
      (emote_internal_flags_ & kEmoteInternalFlagAnimationBehavior458) != 0u) {
    return true;
  }
  const auto write = [out_animation_id](const std::uint16_t animation_id) {
    if (out_animation_id != nullptr) *out_animation_id = animation_id;
    return true;
  };

  if ((movement_flags & (kMoveFlagSwimming | kMoveFlagFlying)) != 0u) {
    if ((movement_flags & (kMoveFlagStrafeLeft | kMoveFlagStrafeRight)) != 0u) {
      return write((movement_flags & kMoveFlagStrafeLeft) != 0u
                       ? render::AnimId::kSwimLeft
                       : render::AnimId::kSwimRight);
    }
    return write((movement_flags & kMoveFlagBackward) != 0u
                     ? render::AnimId::kSwimBackwards
                     : render::AnimId::kSwim);
  }

  if (owner_.Movement().HasNonExemptFlyingSpline()) {
    return write(render::AnimId::kFly);
  }
  if ((movement_flags & kMoveFlagBackward) != 0u) {
    return write(render::AnimId::kWalkBackwards);
  }
  if ((owner_.State().GetVisFlags() & kUnitVisFlagCreep) != 0u) {
    return write(render::AnimId::kStealthWalk);
  }

  const float speed = owner_.Movement().ComputeCurrentSpeed();
  if (speed >= kLocomotionSprintSpeedThreshold) {
    return write(render::AnimId::kSprint);
  }
  const float walk_speed = owner_.Movement().Data().GetSpeed(kSpeedWalk);
  return write(speed <= walk_speed + walk_speed ? render::AnimId::kWalk
                                                : render::AnimId::kRun);
}

bool UnitAnimationRuntime::ResolveTurnInPlaceStandAnimation(
    const WorldSession &session, const std::uint32_t selector_flags,
    std::uint16_t *const out_animation_id) const {
  if (!HasTurnDrivenStandSelectorGate(session)) return false;
  if ((emote_internal_flags_ & kBaseAnimationStateMask) == 0u) return true;
  if ((selector_flags & kStandSelectorTurnInPlaceClaimFlag) == 0u) return true;
  const bool turning_left =
      (owner_.GetMovementInfo().flags & kMoveFlagTurnLeft) != 0u ||
      (emote_internal_flags_ & kEmoteFlagTurnInPlaceLeft) != 0u;
  if (out_animation_id != nullptr) {
    *out_animation_id = turning_left ? render::AnimId::kShuffleLeft
                                     : render::AnimId::kShuffleRight;
  }
  return true;
}

bool UnitAnimationRuntime::ResolveIdleStandAnimation(
    const WorldSession &session, std::uint16_t *const out_animation_id,
    const bool keep_settle) const {
  if (owner_.Vehicle().ResolveAttachedVehicleSeatEntry(owner_, session) !=
      nullptr) {
    return false;
  }
  if ((emote_internal_flags_ & (kEmoteInternalFlagFlightTransitionLock |
                                kEmoteInternalFlagAnimationBehavior39)) != 0u) {
    return false;
  }

  const auto write = [out_animation_id](const std::uint16_t animation_id) {
    if (out_animation_id != nullptr) *out_animation_id = animation_id;
    return true;
  };

  if (const auto current = GetCurrentAnimationId();
      keep_settle && current.has_value() &&
      ResolveAnimationBehaviorId(owner_, *current) == kSettleBehaviorId) {
    return write(*current);
  }
  const auto movement_flags = ResolveSelectorMovementFlags();
  if ((movement_flags & (kMoveFlagSwimming | kMoveFlagFlying)) != 0u) {
    return write(render::AnimId::kSwimIdle);
  }
  if ((owner_.State().GetVisFlags() & kUnitVisFlagCreep) != 0u) {
    return write(kStealthStandAnimationId);
  }
  return write(ShouldUseHoverStandAnimation(session)
                   ? kHoverStandAnimationId
                   : render::AnimId::kStand);
}

bool UnitAnimationRuntime::ApplyMovementDrivenStandAnimationOverride(
    const WorldSession &session) {
  if (!HasMovementDrivenStandAnimationOverride(session)) return false;
  const auto current = GetCurrentAnimationId();
  const bool preserve = current.has_value() && IsMovementStandPreservingBehaviorId(
                                                  ResolveAnimationBehaviorId(owner_, *current));
  ApplySelectedStandAnimation(
      preserve ? *current : static_cast<std::uint16_t>(kFallAnimationId), 0u);
  return true;
}

const data::dbc::EmotesEntry *UnitAnimationRuntime::LookupEmoteStateEntry(
    const std::uint32_t emote_state) const {
  const auto *dbc = owner_.dbc_loader();
  return dbc != nullptr ? dbc->emotes().LookupEntry(emote_state) : nullptr;
}

std::uint32_t UnitAnimationRuntime::ResolveStandAnimationRequestId() const {
  constexpr std::uint32_t kStandRequestSuppressMask =
      kEmoteInternalFlagAnimationBehavior39 | kEmoteInternalFlagFlightTransitionLock;
  return (emote_internal_flags_ & kStandRequestSuppressMask) != 0u
             ? kStandAnimationCustomRequestId
             : render::AnimId::kStand;
}

bool UnitAnimationRuntime::ResolveStandStateCategoryAnimation(
    const WorldSession &session, std::uint16_t *const out_animation_id) {
  if (owner_.Vehicle().ResolveAttachedVehicleSeatEntry(owner_, session) != nullptr) {
    return false;
  }
  const std::uint8_t stand_state = GetStandState();
  const std::uint8_t previous = previous_stand_state_category_;
  std::uint16_t resolved_animation_id;
  switch (stand_state) {
  case 0u:
    if (previous == 3u) {
      resolved_animation_id = render::AnimId::kSleepUp;
    } else if (previous == 8u) {
      resolved_animation_id = render::AnimId::kKneelEnd;
    } else if (previous == 1u) {
      resolved_animation_id = render::AnimId::kSitGroundUp;
    } else {
      return false;

    }
    break;
  case 1u:
    if (previous == 0u) {
      resolved_animation_id = render::AnimId::kSitGroundDown;
    } else {

      const auto current = GetCurrentAnimationId();
      resolved_animation_id = (current.has_value() &&
                               *current == render::AnimId::kSitGroundDown)
                                  ? render::AnimId::kSitGroundDown
                                  : render::AnimId::kSitGround;
    }
    break;
  case 3u:
    resolved_animation_id =
        previous != 0u ? render::AnimId::kSleep : render::AnimId::kSleepDown;
    break;
  case 4u: resolved_animation_id = render::AnimId::kSitChairLow; break;
  case 5u: resolved_animation_id = render::AnimId::kSitChairMed; break;
  case 6u: resolved_animation_id = render::AnimId::kSitChairHigh; break;
  case 8u:
    resolved_animation_id =
        previous == 0u ? render::AnimId::kKneelStart : render::AnimId::kKneelLoop;
    break;
  default:
    return false;
  }
  previous_stand_state_category_ = stand_state;
  if (out_animation_id != nullptr) *out_animation_id = resolved_animation_id;
  return true;
}

void UnitAnimationRuntime::RefreshSelectedStandAnimation(
    const WorldSession &session, const std::uint32_t animation_flags,
    const std::uint32_t selector_flags) {

  struct PreviousStandStatePublisher {
    UnitAnimationRuntime *runtime;
    ~PreviousStandStatePublisher() {
      if ((runtime->emote_internal_flags_ & kEmoteStateResolveModelGatedFlag) !=
          0u) {
        runtime->previous_stand_state_category_ = runtime->GetStandState();
      }
    }
  } previous_stand_state_publisher{this};
  std::uint32_t resolved_animation_flags = animation_flags;
  std::uint16_t selected_animation_id = 0u;

  if (ResolveStandStateTransitionAnimationOverride(selector_flags, &selected_animation_id)) {
    if (selected_animation_id != 0u)
      ApplySelectedStandAnimation(selected_animation_id, resolved_animation_flags);
    return;
  }

  if (ApplyMovementDrivenStandAnimationOverride(session)) {
    return;
  }

  std::uint16_t directional_animation_id = 0u;
  if (ResolveDirectionalLocomotionAnimation(ResolveSelectorMovementFlags(),
                                            selector_flags,
                                            &directional_animation_id)) {
    if (directional_animation_id != 0u)
      ApplySelectedStandAnimation(directional_animation_id,
                                  resolved_animation_flags);
    return;
  }
  std::uint16_t loot_animation_id = 0u;
  if (ResolveLootStandAnimationOverride(session, selector_flags, &loot_animation_id)) {
    if (loot_animation_id != 0u)
      ApplySelectedStandAnimation(loot_animation_id, resolved_animation_flags);
    return;
  }

  if (ResolveSpellVisualStandAnimationOverride(
          selector_flags, &resolved_animation_flags, &selected_animation_id)) {
    if (selected_animation_id != 0u)
      ApplySelectedStandAnimation(selected_animation_id, resolved_animation_flags);
    return;
  }

  const auto combat_ready_anim_flags =
      static_cast<std::uint8_t>(resolved_animation_flags & 0xFFu);
  std::uint32_t combat_ready_animation_id = kInvalidUnitAnimationId;
  if (TryResolveCachedTargetStandAnimation(selector_flags,
                                           &combat_ready_animation_id,
                                           &combat_ready_anim_flags)) {
    if (IsValidUnitAnimationId(combat_ready_animation_id))
      ApplySelectedStandAnimation(
          static_cast<std::uint16_t>(combat_ready_animation_id),
          resolved_animation_flags);
    return;
  }

  std::uint16_t turn_in_place_animation_id = 0u;
  if (ResolveTurnInPlaceStandAnimation(session, selector_flags,
                                       &turn_in_place_animation_id)) {
    if (turn_in_place_animation_id != 0u)
      ApplySelectedStandAnimation(turn_in_place_animation_id,
                                  resolved_animation_flags);
    return;
  }

  std::uint16_t ranged_animation_id = 0u;
  if (ResolveRangedAutoRepeatStandAnimation(selector_flags, &ranged_animation_id)) {
    if (ranged_animation_id != 0u)
      ApplySelectedStandAnimation(ranged_animation_id, resolved_animation_flags);
    return;
  }

  if (ResolveStandStateCategoryAnimation(session, &selected_animation_id)) {
    ApplySelectedStandAnimation(selected_animation_id, resolved_animation_flags);
    return;
  }

  std::uint32_t emote_animation_id = 0u;
  if (!EmoteStateCheck(kStandSelectorEmoteAnimFlag, &emote_animation_id) ||
      !IsValidUnitAnimationId(emote_animation_id) || emote_animation_id == 0u) {

    if (pending_deferred_animation_id_ != -1 &&
        IsValidUnitAnimationId(
            static_cast<std::uint32_t>(pending_deferred_animation_id_))) {
      ApplySelectedStandAnimation(
          static_cast<std::uint16_t>(pending_deferred_animation_id_),
          animation_flags);
      return;
    }
    if (deferred_animation_satisfied_id_ != -1) {
      return;
    }

    std::uint16_t idle_animation_id = 0u;
    if (ResolveIdleStandAnimation(session, &idle_animation_id)) {
      ApplySelectedStandAnimation(idle_animation_id, animation_flags);
      return;
    }
    ClearSelectedStandAnimationState();
    return;
  }
  ApplySelectedStandAnimation(static_cast<std::uint16_t>(emote_animation_id), animation_flags);
}

namespace {

[[nodiscard]] constexpr bool IsUpperBodySplitCandidateBehavior(
    const std::uint32_t behavior) noexcept {
  return behavior == 2u || (behavior >= 8u && behavior <= 10u) ||
         (behavior >= 0x0Eu && behavior <= 0x24u) ||
         (behavior >= 0x2Eu && behavior <= 0x31u) ||
         (behavior >= 0x33u && behavior <= 0x4Au) ||
         (behavior >= 0x4Cu && behavior <= 0x4Eu) ||
         (behavior >= 0x50u && behavior <= 0x5Au) ||
         (behavior >= 0x69u && behavior <= 0x71u) ||
         behavior == 0x75u || behavior == 0x76u ||
         (behavior >= 0x7Au && behavior <= 0x7Du) ||
         (behavior >= 0x80u && behavior <= 0x82u) ||
         behavior == 0x85u || behavior == 0x86u ||
         (behavior >= 0x88u && behavior <= 0x8Au) ||
         (behavior >= 0x99u && behavior <= 0x9Cu) ||
         behavior == 0xB9u || behavior == 0xBAu || behavior == 0xC3u ||
         (behavior >= 0xD5u && behavior <= 0xDEu) || behavior == 0xE1u;
}

[[nodiscard]] constexpr bool IsContactReactionBehavior(
    const std::uint32_t behavior) noexcept {
  return behavior == 10u || (behavior >= 0x10u && behavior <= 0x18u) ||
         behavior == 0x1Eu || behavior == 0x24u ||
         (behavior >= 0x39u && behavior <= 0x3Bu) ||
         (behavior >= 0x55u && behavior <= 0x58u) || behavior == 0x5Fu ||
         behavior == 0x75u || behavior == 0x76u ||
         (behavior >= 0xAAu && behavior <= 0xB3u) || behavior == 0xD4u;
}

[[nodiscard]] constexpr bool IsWholeBodyOnlyBehavior(
    const std::uint32_t behavior) noexcept {
  return behavior == 1u || behavior == 6u ||
         (behavior >= 0x83u && behavior <= 0x84u) ||
         (behavior >= 0x1D2u && behavior <= 0x1D4u) || behavior == 0x1D8u;
}

[[nodiscard]] constexpr bool IsReadyStanceBehavior(
    const std::uint32_t behavior) noexcept {
  return behavior >= 0x19u && behavior <= 0x1Du;
}

}

bool UnitAnimationRuntime::IsUpperBodyOnlyAnimation(
    const std::uint32_t incoming_animation_id,
    const std::uint32_t current_animation_id) const {

  if (current_animation_id >= kInvalidUnitAnimationId) return false;

  if (HasActiveSpellVisualStandAnimationSource()) return false;

  const auto incoming_behavior =
      ResolveAnimationBehaviorId(owner_, incoming_animation_id);
  const auto current_behavior =
      ResolveAnimationBehaviorId(owner_, current_animation_id);
  if (IsWholeBodyOnlyBehavior(incoming_behavior)) return false;

  const std::uint32_t movement_flags = owner_.GetMovementInfo().flags;

  const bool action_lock_without_channel =
      owner_.GetUInt32(UNIT_CHANNEL_SPELL) == 0u && HasChannelingActionLock();

  const bool turning_or_transitioning =
      (movement_flags & (kMoveFlagTurnLeft | kMoveFlagTurnRight)) != 0u ||
      (emote_internal_flags_ & kEmoteInternalFlagStandSelectorTransitionMask) !=
          0u;

  const bool airborne =
      ((movement_flags & kMoveFlagFalling) != 0u &&
       ((movement_flags & kMoveFlagFallingFar) != 0u ||
        owner_.GetMovementInfo().jump.z_speed != 0.0f)) ||
      IsAirborneForAnimationSplit();

  constexpr std::uint32_t kJumpStartBehavior = 0x25u;
  constexpr std::uint32_t kJumpLandRunBehavior = 0xBBu;
  const auto resolve_default_arm = [&]() -> bool {
    const bool incoming_is_jump =
        (incoming_behavior >= kJumpStartBehavior &&
         incoming_behavior <= kJumpStartBehavior + 2u) ||
        incoming_behavior == kJumpLandRunBehavior;
    if (incoming_is_jump &&
        (IsContactReactionBehavior(current_behavior) ||
         IsReadyStanceBehavior(current_behavior))) {
      return true;
    }

    if (IsContactReactionBehavior(incoming_behavior) &&
        (movement_flags & kMoveFlagFalling) != 0u) {
      return true;
    }

    constexpr std::uint32_t kLocomotionOrAirborneMovementMask =
        kMoveFlagForward | kMoveFlagBackward | kMoveFlagStrafeLeft |
        kMoveFlagStrafeRight | kMoveFlagSwimming | kMoveFlagAscending |
        kMoveFlagDescending | kMoveFlagFlying;
    return action_lock_without_channel &&
           (movement_flags & kLocomotionOrAirborneMovementMask) != 0u;
  };

  bool upper_only = false;
  if (IsUpperBodySplitCandidateBehavior(incoming_behavior)) {

    constexpr std::uint32_t kNotStandingStillMovementMask = 0x02E000FFu;
    const bool standing_still =
        (emote_internal_flags_ &
         kEmoteInternalFlagStandSelectorTransitionMask) == 0u &&
        (movement_flags & kNotStandingStillMovementMask) == 0u && !airborne &&
        GetStandState() == 0u;
    if (!standing_still) {
      upper_only = true;
    } else if (IsReadyStanceBehavior(incoming_behavior) &&
               owner_.GetUInt32(UNIT_FIELD_MOUNTDISPLAYID) != 0u) {

      upper_only = true;
    } else {

      upper_only = resolve_default_arm();
    }
  } else {
    upper_only = resolve_default_arm();
  }

  if (incoming_behavior == 0x27u || incoming_behavior == kJumpLandRunBehavior) {
    upper_only = false;
  }

  if (incoming_behavior == 0x85u || incoming_behavior == 0x86u) {
    constexpr std::uint32_t kFishingCompatibleStandStates = 0x73u;
    const auto stand_state = GetStandState();
    if (stand_state > 6u ||
        ((1u << (stand_state & 0x1Fu)) & kFishingCompatibleStandStates) == 0u) {
      upper_only = false;
    }
  }

  const bool in_combat =
      action_lock_without_channel ||
      owner_.GetUInt32(UNIT_CHANNEL_SPELL) != 0u ||
      owner_.Interaction().HasCachedUpdateTarget();
  if (!in_combat || !turning_or_transitioning) return upper_only;
  if ((movement_flags & (kMoveFlagStrafeLeft | kMoveFlagStrafeRight)) == 0u) {
    return false;
  }
  return upper_only;
}

UnitAnimationRuntime::SequenceEndFollowUp
UnitAnimationRuntime::ResolveSequenceEndFollowUp(
    const std::uint32_t behavior) const {
  using Kind = SequenceEndFollowUpKind;
  constexpr SequenceEndFollowUp kSelector{Kind::kRunSelector, 0u};
  constexpr SequenceEndFollowUp kNothing{Kind::kNothing, 0u};
  const auto play = [](const std::uint16_t id) {
    return SequenceEndFollowUp{Kind::kPlayRow, id};
  };
  const auto raw = [](const std::uint16_t id) {
    return SequenceEndFollowUp{Kind::kRawSequence, id};
  };

  const std::uint8_t stand_state = GetStandState();

  const bool death_state_blocked = IsEmoteAnimationStateBlocked();

  const bool ranged_hold_model_live = sheathe_state_ == kSheatheStateRanged;
  const bool ranged_hold_driver_live =
      owner_.Interaction().HasCachedUpdateTarget() ||
      (emote_internal_flags_ & kEmoteFlagAutoRepeatActive) != 0u ||
      owner_.GetUInt32(UNIT_CHANNEL_SPELL) != 0u ||
      (emote_internal_flags_ & kEmoteFlagChannelingActionLock) != 0u;
  const bool ranged_hold_continues =
      ranged_hold_model_live && ranged_hold_driver_live;

  switch (behavior) {

  case 1u:

    return death_state_blocked ? raw(render::AnimId::kDead) : kSelector;
  case 6u:
    return death_state_blocked ? kNothing : kSelector;
  case 0x83u:
    return death_state_blocked ? raw(kDrownedAnimationId) : kSelector;
  case 0x84u:
    return death_state_blocked ? kNothing : kSelector;
  case 0x1D2u:
  case 0x1D3u:
    if (!death_state_blocked) return kSelector;

    return play((emote_internal_flags_ &
                 kEmoteInternalFlagAirborneDeathSubmit) != 0u
                    ? kDeathLoopAnimationId
                    : kDeathEndAnimationId);
  case 0x1D4u:
    return death_state_blocked
               ? raw(static_cast<std::uint16_t>(
                     ResolveAnimationId(kDeathEndHoldAnimationId)))
               : kSelector;
  case 0x1D8u:
    return death_state_blocked ? kNothing : kSelector;

  case 0x25u:
  case 0x26u:
    return (playback_movement_flags_ & kMoveFlagFlying) == 0u
               ? play(render::AnimId::kJump)
               : kSelector;
  case 0x27u:
    return kSelector;
  case 0x28u:
    return play(static_cast<std::uint16_t>(kFallAnimationId));

  case 0x32u:
    return play(kLootHoldAnimationId);
  case 0x45u: {

    const bool in_combat =
        (owner_.GetUInt32(UNIT_FIELD_FLAGS) & kUnitFieldFlagsInCombat) != 0u ||
        owner_.Interaction().HasCachedUpdateTarget();
    return in_combat ? kSelector : play(kEmoteDanceAnimationId);
  }

  case 0x60u:
  case 0x61u:
    return play(stand_state == kStandStateSitGround ? kSitGroundAnimationId
                                                    : kSitGroundUpAnimationId);
  case 0x63u:
  case 0x64u:
    return play(stand_state == kStandStateSleep ? kSleepAnimationId
                                                : kSleepUpAnimationId);
  case 0x66u:
    return stand_state == kStandStateSitChairLow ? play(kSitChairLowAnimationId)
                                                 : kSelector;
  case 0x67u:
    return stand_state == kStandStateSitChairMed ? play(kSitChairMedAnimationId)
                                                 : kSelector;
  case 0x68u:
    return stand_state == kStandStateSitChairHigh
               ? play(kSitChairHighAnimationId)
               : kSelector;
  case 0x72u:
  case 0x73u:
    return play(stand_state == kStandStateKneel ? kKneelLoopAnimationId
                                                : kKneelEndAnimationId);

  case 0x69u:
    return play(kHoldBowAnimationId);
  case 0x6Au:
    return play(kHoldRifleAnimationId);
  case 0x6Du:
    return ranged_hold_continues ? play(kHoldBowAnimationId) : kSelector;
  case 0x6Eu:
    return ranged_hold_continues ? play(kHoldRifleAnimationId) : kSelector;
  case 0x6Fu:
  case 0x70u:
    return ranged_hold_continues ? play(kHoldThrownAnimationId) : kSelector;

  case 0x7Fu:
    return kSelector;
  case 0x85u:
    return play(kFishingLoopAnimationId);
  case 0xC9u:
    return play(kSubmergedAnimationId);
  case 0x1CAu:
  case 0x1CBu:
  case 0x1CCu:
    return play(kSettleAnimationId);
  case 0x1D0u:
    return SequenceEndFollowUp{Kind::kIdleResolver, 0u};
  default:
    return kSelector;
  }
}

void UnitAnimationRuntime::HandleAnimSequenceEnd(const WorldSession &session,
                                     std::uint32_t, std::uint32_t,
                                     std::uint32_t emote_state,
                                     bool has_remaining) {

  if (Dance().Get().IsActive()) {
    Dance().Get(owner_, session).ContinuationCheck();
    return;
  }

  const auto finished_behavior = ResolveAnimationBehaviorId(owner_, emote_state);
  ClearSequenceEndFlagBits(finished_behavior,
                           static_cast<std::uint16_t>(emote_state));
  if (previous_selected_stand_animation_id_.has_value() &&
      previous_selected_stand_animation_id_.value() == static_cast<std::uint16_t>(emote_state))
    previous_selected_stand_animation_id_.reset();
  if (has_remaining) {

    if (finished_behavior == kJumpEndBehaviorId ||
        finished_behavior == kJumpLandRunBehaviorId)
      emote_internal_flags_ &= ~kEmoteInternalFlagAnimationBehavior39;
    return;
  }

  emote_internal_flags_ =
      (emote_internal_flags_ & ~kBaseAnimationStateMask) |
      kEmoteStateResolveModelGatedFlag;

  const auto follow_up = ResolveSequenceEndFollowUp(finished_behavior);

  switch (follow_up.kind) {
  case SequenceEndFollowUpKind::kNothing:
    emote_internal_flags_ |= kBaseAnimationStateMask;
    return;
  case SequenceEndFollowUpKind::kRawSequence:

    if (PrimaryM2ModelContainsAnimation(owner_, follow_up.animation_id)) {
      SubmitRawPlayback(follow_up.animation_id, false,
                        false, false);
    }
    emote_internal_flags_ |= kBaseAnimationStateMask;
    return;
  case SequenceEndFollowUpKind::kPlayRow:
    PlayEmoteAnimation(static_cast<std::int32_t>(follow_up.animation_id),
                       kStandSelectorReplayFlag);
    emote_internal_flags_ |= kBaseAnimationStateMask;
    return;
  case SequenceEndFollowUpKind::kIdleResolver: {

    std::uint16_t idle_animation_id = 0u;
    if (ResolveIdleStandAnimation(session, &idle_animation_id,
                                  false)) {
      PlayEmoteAnimation(static_cast<std::int32_t>(idle_animation_id),
                         kStandSelectorReplayFlag);
      emote_internal_flags_ |= kBaseAnimationStateMask;
      return;
    }
    break;
  }
  case SequenceEndFollowUpKind::kRunSelector:
    break;
  }

  RefreshSelectedStandAnimation(session, kStandSelectorReplayFlag, ~0u);
  emote_internal_flags_ |= kBaseAnimationStateMask;

}

void UnitAnimationRuntime::ResetEmoteState() {

  for (auto &slot : emote_slots_) slot = 0u;
  emote_slots_[EmoteQueueEmoteWord(0)] = kEmoteQueueIdleSentinel;
  emote_internal_flags_ &=
      ~(kEmoteInternalFlagEmoteQueueDrained |
        kEmoteInternalFlagUseSpellVisualStartAnimation);
  ClearSelectedStandAnimationState();
  current_anim_group_ = -1;
}

void UnitAnimationRuntime::ResetDeathPlaybackForAliveTransition(
    WorldSession &session) {
  death_transition_played_ = false;
  ResetEmoteState();
  RefreshSelectedStandAnimation(session, 0u, ~0u);
  if (const auto stand_animation = GetCurrentAnimationId();
      stand_animation.has_value()) {
    RequestPlayback(*stand_animation, AnimationSequenceLoops(*stand_animation),
                    true);
    return;
  }

  stand_selector_refresh_pending_ = true;
}

const data::dbc::EmotesEntry *
UnitAnimationRuntime::LookupQueuedEmoteRow(std::uint32_t emote_id) const {

  const auto *const dbc = owner_.dbc_loader();
  return dbc != nullptr ? dbc->emotes().LookupEntry(emote_id) : nullptr;
}

std::uint32_t UnitAnimationRuntime::ResolveAnimationDurationMs(
    std::uint32_t animation_id) const {

  auto *const system = owner_.m2_system();
  const std::uint32_t instance_id = owner_.GetPrimaryM2InstanceId();
  if (system == nullptr || instance_id == 0u) {
    return 0u;
  }
  const auto model = system->QueryInstanceModel(instance_id);
  if (model.status != render::m2::M2ResultStatus::kReady || model.model_id == 0u) {
    return 0u;
  }
  const auto sequence = system->QueryModelAnimationSequence(
      model.model_id, ResolveAnimationId(animation_id, instance_id));
  if (sequence.status != render::m2::M2ResultStatus::kReady ||
      !sequence.has_sequence) {
    return 0u;
  }
  return sequence.sequence.duration_ms;
}

void UnitAnimationRuntime::EmoteQueueHandler(const std::uint32_t *emote_pairs, std::int32_t count) {
  if (emote_pairs == nullptr || count <= 0) {
    return;
  }

  emote_slots_[EmoteQueueEmoteWord(0)] = kEmoteQueueIdleSentinel;
  std::size_t stored = 0;

  const auto pair_count =
      std::min<std::size_t>(static_cast<std::size_t>(count), kEmoteQueueSlotCount);
  for (std::size_t index = 0; index < pair_count; ++index) {
    const std::uint32_t delay = emote_pairs[index * 2];
    const std::uint32_t emote_id = emote_pairs[index * 2 + 1];

    if (emote_id != 0u && stored < kEmoteQueueSlotCount) {

      const auto *const row = LookupQueuedEmoteRow(emote_id);
      if (row != nullptr && row->spec == 0u) {
        emote_slots_[EmoteQueueDelayWord(stored)] = delay;
        emote_slots_[EmoteQueueEmoteWord(stored)] = emote_id;
        ++stored;
      }
    }

    const auto *const slot_row =
        LookupQueuedEmoteRow(emote_slots_[EmoteQueueEmoteWord(index)]);
    if (slot_row != nullptr && slot_row->anim_id != 0u) {

      emote_internal_flags_ &= ~kEmoteInternalFlagEmoteQueueDrained;
    }
  }

  if (stored < kEmoteQueueSlotCount) {
    emote_slots_[EmoteQueueEmoteWord(stored)] = 0u;
  }
  emote_slots_[EmoteQueueDelayWord(0)] += core::GameClock::GetTickCount32();
}

void UnitAnimationRuntime::EmoteSequencePlayer() {
  if (emote_slots_[EmoteQueueEmoteWord(0)] == kEmoteQueueIdleSentinel) {
    return;
  }

  const auto finish_queue = [this]() {
    emote_internal_flags_ |= kEmoteInternalFlagEmoteQueueDrained;
    emote_slots_[EmoteQueueEmoteWord(0)] = kEmoteQueueIdleSentinel;
  };

  std::size_t slot = 0;
  if (emote_slots_[EmoteQueueEmoteWord(0)] == 0u) {

    do {
      ++slot;
      if (slot == kEmoteQueueSlotCount) {
        finish_queue();
        return;
      }
    } while (emote_slots_[EmoteQueueEmoteWord(slot)] == 0u);
  }

  const std::uint32_t due_tick = emote_slots_[EmoteQueueDelayWord(slot)];
  const std::uint32_t now = core::GameClock::GetTickCount32();
  if (due_tick > now) {
    return;
  }

  const auto *const row =
      LookupQueuedEmoteRow(emote_slots_[EmoteQueueEmoteWord(slot)]);

  if (row != nullptr) {
    PlayEmoteAnimation(static_cast<std::int32_t>(row->anim_id), 0u);
  }
  emote_slots_[EmoteQueueEmoteWord(slot)] = 0u;

  const std::size_t next_slot = slot + 1;
  if (slot >= kEmoteQueueSlotCount - 1 ||
      emote_slots_[EmoteQueueEmoteWord(next_slot)] == 0u) {
    finish_queue();
    return;
  }

  emote_slots_[EmoteQueueDelayWord(next_slot)] +=
      kEmoteQueueInterEmoteDelayMs + core::GameClock::GetTickCount32() +
      (row != nullptr ? ResolveAnimationDurationMs(row->anim_id) : 0u);
}

bool UnitAnimationRuntime::IsLootTargetAnimatable() const {
  const auto *const objects = owner_.object_manager();
  const auto *const player =
      objects != nullptr ? objects->GetLocalPlayerTyped() : nullptr;
  if (player == nullptr || owner_.GetGuid() != player->GetGuid()) {
    return (owner_.State().GetUnitFlags() & kUnitFlagLootPoseBlocked) == 0u;
  }
  if (stand_selection_interaction_target_guid_ == 0u) {
    return false;
  }
  const auto *const target = objects->GetObjectByGUID(
      ObjectGuid(stand_selection_interaction_target_guid_));
  if (target == nullptr || target->IsItem()) {
    return false;
  }
  if (target->IsGameObject() &&
      static_cast<const CGGameObject_C *>(target)->GetGoType() ==
          GameObjectType::FishingNode) {
    return false;
  }
  if (target->IsUnit() &&
      static_cast<const CGUnit_C *>(target)->State().GetHealth() > 0u) {
    return false;
  }
  return true;
}

}

#include "openwow/game/character_animation.h"

namespace openwow::game {

namespace {

[[nodiscard]] constexpr bool HasAny(const std::uint32_t flags,
                                    const std::uint32_t mask) noexcept {
  return (flags & mask) != 0u;
}

[[nodiscard]] constexpr std::uint32_t AnimationFourCC(
    const char first, const char second, const char third) noexcept {
  return static_cast<std::uint32_t>('$') |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(first)) << 8u) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(second)) << 16u) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(third)) << 24u);
}

[[nodiscard]] constexpr bool IsOneOf(const std::uint32_t value,
                                     const std::uint32_t first,
                                     const std::uint32_t second,
                                     const std::uint32_t third,
                                     const std::uint32_t fourth,
                                     const std::uint32_t fifth) noexcept {
  return value == first || value == second || value == third ||
         value == fourth || value == fifth;
}

inline constexpr float kSprintSpeedThreshold = 11.0f;

UnitAnimationVisualEventCallback g_unit_animation_visual_event_callback =
    nullptr;
void* g_unit_animation_visual_event_context = nullptr;

}

UnitAnimationEventClassification ClassifyUnitAnimationEvent(
    const std::uint32_t fourcc) noexcept {
  constexpr std::uint32_t kPrefixMask = 0x00FFFFFFu;
  const std::uint32_t prefix = fourcc & kPrefixMask;
  const std::uint8_t suffix = static_cast<std::uint8_t>(fourcc >> 24u);

  if (suffix >= static_cast<std::uint8_t>('0') &&
      suffix <= static_cast<std::uint8_t>('3')) {
    const std::uint8_t variant =
        static_cast<std::uint8_t>(suffix - static_cast<std::uint8_t>('0'));
    if (IsOneOf(prefix, AnimationFourCC('F', 'R', '\0'),
                AnimationFourCC('B', 'R', '\0'),
                AnimationFourCC('R', 'R', '\0'),
                AnimationFourCC('S', 'R', '\0'),
                AnimationFourCC('W', 'R', '\0'))) {
      return {UnitAnimationEventRoute::kGroundContactRight, variant};
    }
    if (IsOneOf(prefix, AnimationFourCC('F', 'L', '\0'),
                AnimationFourCC('B', 'L', '\0'),
                AnimationFourCC('R', 'L', '\0'),
                AnimationFourCC('S', 'L', '\0'),
                AnimationFourCC('W', 'L', '\0'))) {
      return {UnitAnimationEventRoute::kGroundContactLeft, variant};
    }
  }

  if (prefix == AnimationFourCC('F', 'D', '\0')) {
    if (suffix >= static_cast<std::uint8_t>('1') &&
        suffix <= static_cast<std::uint8_t>('9')) {
      return {UnitAnimationEventRoute::kCreatureFidget,
              static_cast<std::uint8_t>(
                  suffix - static_cast<std::uint8_t>('0'))};
    }
    if (suffix == static_cast<std::uint8_t>('X')) {
      return {UnitAnimationEventRoute::kCreatureFidget, 10u};
    }
  }

  if (suffix >= static_cast<std::uint8_t>('0') &&
      suffix <= static_cast<std::uint8_t>('8')) {
    const std::uint8_t variant =
        static_cast<std::uint8_t>(suffix - static_cast<std::uint8_t>('0'));
    if (prefix == AnimationFourCC('V', 'T', '\0')) {
      return {UnitAnimationEventRoute::kVehicleTransition, variant};
    }
    if (prefix == AnimationFourCC('V', 'G', '\0')) {
      return {UnitAnimationEventRoute::kVehicleGesture, variant};
    }
  }

  switch (fourcc) {
    case AnimationFourCC('A', 'H', '0'):
    case AnimationFourCC('A', 'H', '1'):
    case AnimationFourCC('A', 'H', '2'):
    case AnimationFourCC('A', 'H', '3'):
    case AnimationFourCC('D', 'T', 'H'):
    case AnimationFourCC('C', 'A', 'H'):
    case AnimationFourCC('C', 'P', 'P'):
    case AnimationFourCC('B', 'W', 'P'):
    case AnimationFourCC('C', 'S', 'S'):
      return {UnitAnimationEventRoute::kCombat, 0u};
    case AnimationFourCC('F', 'S', 'D'):
      return {UnitAnimationEventRoute::kFootstepSound, 0u};
    case AnimationFourCC('T', 'R', 'D'):
      return {UnitAnimationEventRoute::kTradeSpellSound, 0u};
    case AnimationFourCC('C', 'S', 'D'):
      return {UnitAnimationEventRoute::kCustomSound, 0u};
    case AnimationFourCC('E', 'S', 'D'):
      return {UnitAnimationEventRoute::kEmoteSound, 0u};
    case AnimationFourCC('B', 'R', 'T'):
    case AnimationFourCC('S', 'C', 'D'):
    case AnimationFourCC('S', 'M', 'G'):
    case AnimationFourCC('S', 'M', 'D'):
      return {UnitAnimationEventRoute::kDirectCreatureSound, 0u};
    case AnimationFourCC('W', 'N', 'G'):
    case AnimationFourCC('W', 'G', 'G'):
      return {UnitAnimationEventRoute::kCreatureVocal, 0u};
    case AnimationFourCC('B', 'T', 'H'):
      return {UnitAnimationEventRoute::kBodyThudEffect, 0u};
    case AnimationFourCC('C', 'S', 'R'):
    case AnimationFourCC('C', 'S', 'L'):
    case AnimationFourCC('C', 'S', 'T'):
      return {UnitAnimationEventRoute::kSpellContact, 0u};
    case AnimationFourCC('S', 'H', 'L'):
      return {UnitAnimationEventRoute::kShieldLeft, 0u};
    case AnimationFourCC('S', 'H', 'R'):
      return {UnitAnimationEventRoute::kShieldRight, 0u};
    case AnimationFourCC('B', 'W', 'R'):
      return {UnitAnimationEventRoute::kWeaponContact, 0u};
    default:
      return {};
  }
}

bool DispatchUnitAnimationVisualEvent(
    const UnitAnimationVisualEvent& event) {
  if (g_unit_animation_visual_event_callback == nullptr) {
    return false;
  }
  g_unit_animation_visual_event_callback(
      event, g_unit_animation_visual_event_context);
  return true;
}

void SetUnitAnimationVisualEventCallback(
    const UnitAnimationVisualEventCallback callback, void* const context) {
  g_unit_animation_visual_event_callback = callback;
  g_unit_animation_visual_event_context = context;
}

void ClearUnitAnimationVisualEventCallback() {
  g_unit_animation_visual_event_callback = nullptr;
  g_unit_animation_visual_event_context = nullptr;
}

CharacterLocomotionAnimation ResolveCharacterLocomotionAnimation(
    const CharacterLocomotionState& state) noexcept {
  using namespace render::AnimId;

  if (state.dead) {

    return {.animation_id = kDead, .looping = false};
  }

  const std::uint32_t flags = state.movement_flags;

  if (state.mounted) {
    return {.animation_id = kMount, .looping = true};
  }

  if (HasAny(flags, kMoveFlagFalling | kMoveFlagFallingFar)) {
    return {.animation_id = kFall, .looping = true};
  }

  constexpr std::uint32_t kDirectionalMask =
      kMoveFlagForward | kMoveFlagBackward | kMoveFlagStrafeLeft |
      kMoveFlagStrafeRight;
  const bool backward = HasAny(flags, kMoveFlagBackward);
  const bool strafe_left = HasAny(flags, kMoveFlagStrafeLeft);
  const bool strafe_right = HasAny(flags, kMoveFlagStrafeRight);

  const bool directional = HasAny(flags, kDirectionalMask) &&
                           !state.directional_locomotion_suppressed;
  const bool swim_or_fly = HasAny(flags, kMoveFlagSwimming | kMoveFlagFlying);

  if (!directional) {

    if (!state.turn_in_place_declined) {
      if (HasAny(flags, kMoveFlagTurnLeft) || state.turning_left_latch) {
        return {.animation_id = kShuffleLeft, .looping = true};
      }
      if (HasAny(flags, kMoveFlagTurnRight) || state.turning_right_latch) {
        return {.animation_id = kShuffleRight, .looping = true};
      }
    }
    if (HasAny(flags, kMoveFlagSwimming)) {
      return {.animation_id = kSwimIdle, .looping = true};
    }
    if (HasAny(flags, kMoveFlagFlying)) {
      return {.animation_id = kHover, .looping = true};
    }
    return {.animation_id = kStand, .looping = true};
  }

  if (swim_or_fly) {
    if (strafe_left || strafe_right) {
      return {.animation_id = strafe_left ? kSwimLeft : kSwimRight,
              .looping = true};
    }
    return {.animation_id = backward ? kSwimBackwards : kSwim, .looping = true};
  }

  if (state.flying_spline) {
    return {.animation_id = kFly, .looping = true};
  }
  if (backward) {
    return {.animation_id = kWalkBackwards, .looping = true};
  }

  if (state.stealthed) {
    return {.animation_id = kStealthWalk, .looping = true};
  }

  if (state.current_speed < kSprintSpeedThreshold) {
    return {.animation_id = state.current_speed <=
                                    state.walk_speed + state.walk_speed
                                ? kWalk
                                : kRun,
            .looping = true};
  }
  return {.animation_id = kSprint, .looping = true};
}

}

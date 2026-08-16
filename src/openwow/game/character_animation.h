#pragma once

#include "openwow/game/object_types.h"
#include "openwow/render/models/animation/animation_state.h"

#include <array>
#include <cstdint>

namespace openwow::game {

struct CharacterLocomotionState {
  std::uint32_t movement_flags{0};
  bool dead{false};
  bool mounted{false};

  float current_speed{0.0f};

  float walk_speed{0.0f};

  bool stealthed{false};

  bool flying_spline{false};

  bool directional_locomotion_suppressed{false};

  bool turning_left_latch{false};
  bool turning_right_latch{false};

  bool turn_in_place_declined{false};
};

struct CharacterLocomotionAnimation {
  std::uint16_t animation_id{render::AnimId::kStand};
  bool looping{true};
};

enum class UnitAnimationEventRoute : std::uint8_t {
  kUnknown,
  kGroundContactRight,
  kGroundContactLeft,
  kCreatureFidget,
  kVehicleTransition,
  kVehicleGesture,
  kCombat,
  kFootstepSound,
  kTradeSpellSound,
  kCustomSound,
  kEmoteSound,
  kDirectCreatureSound,
  kCreatureVocal,
  kBodyThudEffect,
  kSpellContact,
  kShieldLeft,
  kShieldRight,
  kWeaponContact,
};

struct UnitAnimationEventClassification {
  UnitAnimationEventRoute route{UnitAnimationEventRoute::kUnknown};

  std::uint8_t variant{0};

  [[nodiscard]] constexpr bool IsRecognized() const noexcept {
    return route != UnitAnimationEventRoute::kUnknown;
  }
};

[[nodiscard]] UnitAnimationEventClassification ClassifyUnitAnimationEvent(
    std::uint32_t fourcc) noexcept;

struct UnitAnimationVisualEvent {
  std::uint64_t unit_guid{0};
  UnitAnimationEventRoute route{UnitAnimationEventRoute::kUnknown};
  std::uint8_t variant{0};
  std::uint32_t event_type{0};
  std::uint32_t fourcc{0};
  std::int32_t event_data{0};
  std::array<float, 3> position{};
  std::int32_t bone_index{0};
  bool has_position{false};
};

using UnitAnimationVisualEventCallback = void (*)(
    const UnitAnimationVisualEvent& event, void* context);

[[nodiscard]] bool DispatchUnitAnimationVisualEvent(
    const UnitAnimationVisualEvent& event);
void SetUnitAnimationVisualEventCallback(
    UnitAnimationVisualEventCallback callback, void* context);
void ClearUnitAnimationVisualEventCallback();

[[nodiscard]] CharacterLocomotionAnimation ResolveCharacterLocomotionAnimation(
    const CharacterLocomotionState& state) noexcept;

}

#pragma once

#include <array>
#include <algorithm>
#include <cstddef>

namespace openwow::game::interaction_range {

inline constexpr float kCombatReachInteractionPadding = 1.3333334f;
inline constexpr float kMinimumUnitInteractionRange = 5.0f;

struct InteractionActionParameters {
  bool enabled;
  float warning_distance_sq;
  bool suppress_range_resolution;
};

inline constexpr std::array<InteractionActionParameters, 12> kInteractionActionParameters = {{
    {false, 0.0f, false},
    {false, 0.0f, false},
    {false, 0.0f, false},
    {true, 0.0f, false},
    {true, 0.0f, false},
    {true, 6400.0f, false},
    {true, 6400.0f, false},
    {true, 6400.0f, false},
    {true, 0.0f, true},
    {true, 6400.0f, false},
    {true, 6400.0f, false},
    {true, 0.0f, false},
}};

[[nodiscard]] inline constexpr const InteractionActionParameters*
LookupInteractionActionParameters(const int action_type) {
  if (action_type < 0 ||
      static_cast<std::size_t>(action_type) >= kInteractionActionParameters.size()) {
    return nullptr;
  }

  return &kInteractionActionParameters[static_cast<std::size_t>(action_type)];
}

[[nodiscard]] inline constexpr bool ExceedsInteractionWarningDistance(const int action_type,
                                                                     const float distance_sq) {
  const auto* parameters = LookupInteractionActionParameters(action_type);
  return parameters != nullptr && parameters->enabled &&
         parameters->warning_distance_sq > 0.0f &&
         distance_sq >= parameters->warning_distance_sq;
}

[[nodiscard]] inline float ComputeUnitInteractionRange(float source_combat_reach,
                                                       float target_combat_reach) {
  return std::max(kMinimumUnitInteractionRange,
                  source_combat_reach + target_combat_reach +
                      kCombatReachInteractionPadding);
}

[[nodiscard]] inline float ComputeUnitInteractionRangeSquared(float source_combat_reach,
                                                              float target_combat_reach) {
  const float range = ComputeUnitInteractionRange(source_combat_reach, target_combat_reach);
  return range * range;
}

[[nodiscard]] inline float ComputeFriendlyInteractApproachStopDistance(
    float target_combat_reach) {
  return (target_combat_reach + 4.0f) * 0.5f;
}

[[nodiscard]] inline float ComputeUnitInteractionApproachStopDistance(
    float source_combat_reach, float target_combat_reach) {
  const float padded =
      source_combat_reach + kCombatReachInteractionPadding + target_combat_reach;
  return padded >= kMinimumUnitInteractionRange
             ? padded - kCombatReachInteractionPadding
             : 3.6666665f;
}

[[nodiscard]] inline float ComputeSpellInteractionRange(float source_combat_reach,
                                                        float target_combat_reach) {
  return ComputeUnitInteractionRange(source_combat_reach, target_combat_reach) -
         kCombatReachInteractionPadding;
}

[[nodiscard]] inline float ComputeSpellInteractionRangeSquared(float source_combat_reach,
                                                               float target_combat_reach) {
  const float range = ComputeSpellInteractionRange(source_combat_reach, target_combat_reach);
  return range * range;
}

}

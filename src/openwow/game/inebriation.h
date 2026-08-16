#pragma once

#include <algorithm>
#include <cstdint>

namespace openwow::game {

inline constexpr float kNormalizedInebriationScale = 0.0099999998f;

[[nodiscard]] inline bool HasActiveInebriation(
    const std::uint8_t drunk_state,
    const std::uint32_t fake_inebriation) {
  return drunk_state != 0 || fake_inebriation != 0;
}

[[nodiscard]] inline std::uint32_t GetEffectiveInebriationValue(
    const std::uint8_t drunk_state,
    const std::uint32_t fake_inebriation) {
  return std::min<std::uint32_t>(
      std::max<std::uint32_t>(drunk_state, fake_inebriation), 100u);
}

[[nodiscard]] inline float ComputeNormalizedInebriation(
    const std::uint8_t drunk_state,
    const std::uint32_t fake_inebriation) {
  return static_cast<float>(
             GetEffectiveInebriationValue(drunk_state, fake_inebriation)) *
         kNormalizedInebriationScale;
}

}

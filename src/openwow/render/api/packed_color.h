#pragma once

#include "openwow/render/api/math/render_math_types.h"

#include <cstdint>

namespace openwow::render {

[[nodiscard]] inline constexpr RenderVec3 PackedArgbToUnitRgb(
    const std::uint32_t argb) noexcept {
  constexpr float kByteToUnit = 1.0f / 255.0f;
  return {
      static_cast<float>((argb >> 16u) & 0xFFu) * kByteToUnit,
      static_cast<float>((argb >> 8u) & 0xFFu) * kByteToUnit,
      static_cast<float>(argb & 0xFFu) * kByteToUnit,
  };
}

[[nodiscard]] inline constexpr std::uint32_t PackedArgbToBgfxRgba(
    const std::uint32_t argb) noexcept {
  return (argb << 8u) | (argb >> 24u);
}

}

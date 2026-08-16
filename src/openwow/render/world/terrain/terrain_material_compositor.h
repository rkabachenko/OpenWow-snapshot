#pragma once

#include <cstdint>

namespace openwow::render {

[[nodiscard]] inline constexpr std::uint8_t
ResolveRetailTerrainShadowVisibilityByte(const std::uint8_t packed_shadow,
                                         const std::uint8_t bit_index) noexcept {
  return (packed_shadow & (1u << (bit_index & 7u))) != 0u ? 0u : 255u;
}

}

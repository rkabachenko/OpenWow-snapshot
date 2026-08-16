#pragma once

#include <algorithm>
#include <cstdint>

namespace openwow::world {

[[nodiscard]] constexpr float RetailLiquidVertexAttenuation(
    const std::uint32_t table_index, const std::uint8_t authored_value) {
  const float value = static_cast<float>(authored_value);
  switch (table_index) {
    case 0u: {
      const float scaled = value * 0.11111111f;
      return 4.6666665f < scaled ? 1.0f : scaled * 0.21428572f;
    }
    case 1u:
      return std::min(value / 255.0f, 1.0f);
    default:
      return 0.0f;
  }
}

}

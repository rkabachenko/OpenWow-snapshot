#pragma once

#include <array>
#include <cstdint>

namespace openwow::render {

[[nodiscard]] inline bool DecalFacetIsUpward(
    const std::array<float, 3>& normal) noexcept {
  return normal[2] >= 0.0f;
}

inline constexpr float kDecalDepthBiasNdc = (0.4f + 0.4f) / 32768.0f;

[[nodiscard]] float ResolveDecalDepthBias();

[[nodiscard]] inline float DecalVerticalFadeCoord(const float world_z,
                                                  const float center_z,
                                                  const float z_half_extent) {
  if (!(z_half_extent > 0.0f)) {
    return 0.5f;
  }
  return 0.5f + (world_z - center_z) / (2.0f * z_half_extent);
}

}

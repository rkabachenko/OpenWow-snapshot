#pragma once

#include <cstdint>

namespace openwow::math::projection {

[[nodiscard]] inline float ComputeAspectPx(const std::int32_t width_px,
                                           const std::int32_t height_px) noexcept {
  if (width_px <= 0 || height_px <= 0) {
    return 1.0f;
  }
  return static_cast<float>(width_px) / static_cast<float>(height_px);
}

inline constexpr float kProjectionFieldOfViewScale = 0.6f;

}

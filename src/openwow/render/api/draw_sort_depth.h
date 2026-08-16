#pragma once

#include "openwow/render/api/math/render_math_types.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace openwow::render {

struct DrawSortDepth {
  RenderVec3 camera_position{};

  float far_clip{0.0f};

  [[nodiscard]] bool usable() const noexcept {
    return std::isfinite(far_clip) && far_clip > 0.0f;
  }

  [[nodiscard]] std::uint32_t ForDistance(const float distance) const noexcept {
    if (!usable() || !std::isfinite(distance)) {
      return 0u;
    }
    const double normalized =
        std::clamp(static_cast<double>(distance) / static_cast<double>(far_clip), 0.0, 1.0);
    constexpr double kSortDepthMax = 4294967295.0;
    return static_cast<std::uint32_t>(normalized * kSortDepthMax);
  }

  [[nodiscard]] std::uint32_t ForPoint(const float x, const float y,
                                       const float z) const noexcept {
    const float dx = x - camera_position[0];
    const float dy = y - camera_position[1];
    const float dz = z - camera_position[2];
    return ForDistance(std::sqrt(dx * dx + dy * dy + dz * dz));
  }

  [[nodiscard]] float DistanceToAabb(const RenderVec3View minimum,
                                     const RenderVec3View maximum) const noexcept {
    float squared = 0.0f;
    for (std::size_t axis = 0; axis < 3u; ++axis) {
      const float nearest =
          std::clamp(camera_position[axis], minimum[axis], maximum[axis]);
      const float delta = camera_position[axis] - nearest;
      squared += delta * delta;
    }
    return std::sqrt(squared);
  }

  [[nodiscard]] std::uint32_t ForAabb(const RenderVec3View minimum,
                                      const RenderVec3View maximum) const noexcept {
    return ForDistance(DistanceToAabb(minimum, maximum));
  }

  [[nodiscard]] std::uint32_t ForBandedDistance(const float distance,
                                                const float band_width) const noexcept {
    if (!std::isfinite(distance) || !std::isfinite(band_width) || band_width <= 0.0f) {
      return ForDistance(distance);
    }
    return ForDistance(std::floor(distance / band_width) * band_width);
  }
};

}

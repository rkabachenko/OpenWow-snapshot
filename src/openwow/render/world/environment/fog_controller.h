#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace openwow::render {

struct FogColor {
  float r{0.5f}, g{0.5f}, b{0.5f}, a{1.0f};

  bool operator==(const FogColor& o) const {
    return r == o.r && g == o.g && b == o.b && a == o.a;
  }
  bool operator!=(const FogColor& o) const { return !(*this == o); }
};

[[nodiscard]] inline std::uint32_t PackFogColorToArgb(
    const FogColor &color) noexcept {
  const auto pack = [](const float value) {
    return static_cast<std::uint32_t>(std::clamp(
        static_cast<int>(std::nearbyint(value * 255.0f)), 0, 255));
  };
  return (pack(color.a) << 24u) | (pack(color.r) << 16u) |
         (pack(color.g) << 8u) | pack(color.b);
}

struct FogBandSample {
  float end_distance{500.0f};
  float start_factor{0.5f};
  FogColor color{0.5f, 0.5f, 0.5f, 1.0f};

  bool operator==(const FogBandSample& o) const {
    return end_distance == o.end_distance && start_factor == o.start_factor
        && color == o.color;
  }
  bool operator!=(const FogBandSample& o) const { return !(*this == o); }
};

struct FogBandOverride {
  FogBandSample band{};

  bool sky_dome_enabled{true};

  bool operator==(const FogBandOverride& o) const {
    return band == o.band && sky_dome_enabled == o.sky_dome_enabled;
  }
  bool operator!=(const FogBandOverride& o) const { return !(*this == o); }
};

}

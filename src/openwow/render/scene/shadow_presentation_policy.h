#pragma once

#include "openwow/world/presentation/world_presentation_snapshot.h"

#include <algorithm>
#include <cstdint>

namespace openwow::render {

inline constexpr std::uint8_t kMinEnvironmentalShadowQuality = 3;

inline constexpr std::uint16_t kEnvironmentalShadowMapLowRes = 1024;
inline constexpr std::uint16_t kEnvironmentalShadowMapHighRes = 2048;
inline constexpr float kEnvironmentalShadowDistanceYards = 640.0f;

[[nodiscard]] inline constexpr bool BlobShadowsEnabled(
    const int ext_shadow_quality) noexcept {
  return ext_shadow_quality < 1;
}

static_assert(BlobShadowsEnabled(0));
static_assert(!BlobShadowsEnabled(1));
static_assert(!BlobShadowsEnabled(5));

[[nodiscard]] inline constexpr world::ShadowPresentationSettings
ResolveShadowPresentationSettings(const int requested_quality, const bool map_shadows,
                                  const bool projected_textures) noexcept {
  const auto quality = static_cast<std::uint8_t>(std::clamp(requested_quality, 0, 5));
  const std::uint16_t resolution = quality >= 4u ? kEnvironmentalShadowMapHighRes
                                                 : kEnvironmentalShadowMapLowRes;
  return {
      .enabled = map_shadows && projected_textures &&
                 quality >= kMinEnvironmentalShadowQuality,
      .quality = quality,
      .map_resolution = resolution,
      .distance = kEnvironmentalShadowDistanceYards,
      .depth_bias = 0.005f,
  };
}

static_assert(!ResolveShadowPresentationSettings(0, true, true).enabled);
static_assert(!ResolveShadowPresentationSettings(1, true, true).enabled);
static_assert(!ResolveShadowPresentationSettings(2, true, true).enabled);
static_assert(ResolveShadowPresentationSettings(3, true, true).enabled);
static_assert(ResolveShadowPresentationSettings(3, true, true).map_resolution ==
              kEnvironmentalShadowMapLowRes);
static_assert(ResolveShadowPresentationSettings(4, true, true).map_resolution ==
              kEnvironmentalShadowMapHighRes);
static_assert(ResolveShadowPresentationSettings(5, true, true).quality == 5u);
static_assert(ResolveShadowPresentationSettings(5, true, true).distance ==
              kEnvironmentalShadowDistanceYards);
static_assert(!ResolveShadowPresentationSettings(5, false, true).enabled);
static_assert(!ResolveShadowPresentationSettings(5, true, false).enabled);

}

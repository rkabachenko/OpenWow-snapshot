#pragma once

#include "openwow/world/presentation/world_presentation_snapshot.h"

#include <algorithm>
#include <cstdint>

namespace openwow::render {

inline constexpr std::uint8_t kMinDynamicShadowQuality = 1;
inline constexpr std::uint8_t kMinEnvironmentalShadowQuality = 3;

inline constexpr std::uint16_t kShadowMapLowResolution = 1024;
inline constexpr std::uint16_t kShadowMapHighResolution = 2048;

[[nodiscard]] inline constexpr bool BlobShadowsEnabled(
    const int ext_shadow_quality) noexcept {
  return ext_shadow_quality < 1;
}

static_assert(BlobShadowsEnabled(0));
static_assert(!BlobShadowsEnabled(1));
static_assert(!BlobShadowsEnabled(5));

[[nodiscard]] inline constexpr world::ShadowPresentationSettings
ResolveShadowPresentationSettings(const int requested_quality, const bool map_shadows,
                                  [[maybe_unused]] const bool projected_textures) noexcept {
  const auto quality = static_cast<std::uint8_t>(std::clamp(requested_quality, 0, 5));
  const std::uint16_t resolution = quality == 2u || quality >= 4u
                                       ? kShadowMapHighResolution
                                       : kShadowMapLowResolution;
  return {
      .enabled = quality >= kMinDynamicShadowQuality,
      .precomputed_terrain_enabled = map_shadows,
      .quality = quality,
      .map_resolution = resolution,
  };
}

static_assert(!ResolveShadowPresentationSettings(0, true, true).enabled);
static_assert(ResolveShadowPresentationSettings(1, true, true).enabled);
static_assert(ResolveShadowPresentationSettings(2, true, true).enabled);
static_assert(ResolveShadowPresentationSettings(3, true, true).enabled);
static_assert(ResolveShadowPresentationSettings(3, true, true).map_resolution ==
              kShadowMapLowResolution);
static_assert(ResolveShadowPresentationSettings(2, true, true).map_resolution ==
              kShadowMapHighResolution);
static_assert(ResolveShadowPresentationSettings(4, true, true).map_resolution ==
              kShadowMapHighResolution);
static_assert(ResolveShadowPresentationSettings(5, true, true).quality == 5u);
static_assert(ResolveShadowPresentationSettings(5, false, true).enabled);
static_assert(ResolveShadowPresentationSettings(5, true, false).enabled);
static_assert(!ResolveShadowPresentationSettings(5, false, true)
                   .precomputed_terrain_enabled);

}

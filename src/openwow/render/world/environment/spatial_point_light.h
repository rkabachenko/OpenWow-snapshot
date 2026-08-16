#pragma once

#include "openwow/render/api/math/render_math_types.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace openwow::render {

struct SpatialPointLight {
  RenderVec3 position{};
  RenderVec3 diffuse{};
  RenderVec3 attenuation{1.0f, 0.0f, 0.0f};
};

struct SpatialPointLightSelection {
  static constexpr std::size_t kCapacity = 4u;
  std::array<const SpatialPointLight*, kCapacity> lights{};
  std::array<float, kCapacity> distance_squared{};
  std::size_t count{0u};
};

inline constexpr std::uint32_t kSceneLightBucketGridSide = 64u;
inline constexpr std::uint32_t kSceneLightBucketIndexMask = kSceneLightBucketGridSide - 1u;
inline constexpr float kSceneLightBucketsPerUnit = 0.05f;
inline constexpr float kSceneLightBucketQuerySlack = 0.5f;

[[nodiscard]] inline std::uint32_t SceneLightBucketIndex(const float scaled) noexcept {
  const float floored = std::floor(scaled);
  if (!(floored >= -2147483648.0f) || !(floored <= 2147483520.0f)) {
    return 0u;
  }
  return static_cast<std::uint32_t>(static_cast<std::int32_t>(floored)) &
         kSceneLightBucketIndexMask;
}

struct SceneLightBucketArc {
  std::uint32_t first{0u};
  std::uint32_t extent{0u};

  [[nodiscard]] constexpr bool Contains(const std::uint32_t bucket) const noexcept {
    return ((bucket - first) & kSceneLightBucketIndexMask) <= extent;
  }
};

[[nodiscard]] inline SceneLightBucketArc SceneLightBucketArcFor(const float center,
                                                                const float radius) noexcept {
  const std::uint32_t first = SceneLightBucketIndex(
      (center - radius) * kSceneLightBucketsPerUnit - kSceneLightBucketQuerySlack);
  const std::uint32_t last = SceneLightBucketIndex(
      (center + radius) * kSceneLightBucketsPerUnit + kSceneLightBucketQuerySlack);
  return {first, (last - first) & kSceneLightBucketIndexMask};
}

[[nodiscard]] inline SpatialPointLightSelection SelectNearestSpatialPointLights(
    const std::span<const SpatialPointLight> lights,
    const RenderVec3& center,
    const float bounds_radius) noexcept {
  SpatialPointLightSelection selection{};

  if (lights.empty()) {
    return selection;
  }

  const SceneLightBucketArc x_arc = SceneLightBucketArcFor(center[0], bounds_radius);
  const SceneLightBucketArc y_arc = SceneLightBucketArcFor(center[1], bounds_radius);

  for (const SpatialPointLight& light : lights) {
    if (!x_arc.Contains(
            SceneLightBucketIndex(light.position[0] * kSceneLightBucketsPerUnit)) ||
        !y_arc.Contains(
            SceneLightBucketIndex(light.position[1] * kSceneLightBucketsPerUnit))) {
      continue;
    }

    const float dx = light.position[0] - center[0];
    const float dy = light.position[1] - center[1];
    const float dz = light.position[2] - center[2];
    const float distance_squared = dx * dx + dy * dy + dz * dz;

    std::size_t insertion = selection.count;
    if (insertion >= selection.kCapacity) {
      if (distance_squared >= selection.distance_squared.back()) {
        continue;
      }
      insertion = selection.kCapacity - 1u;
    }
    while (insertion != 0u &&
           distance_squared <= selection.distance_squared[insertion - 1u]) {
      selection.lights[insertion] = selection.lights[insertion - 1u];
      selection.distance_squared[insertion] =
          selection.distance_squared[insertion - 1u];
      --insertion;
    }
    selection.lights[insertion] = &light;
    selection.distance_squared[insertion] = distance_squared;
    selection.count =
        std::min(selection.count + 1u, selection.kCapacity);
  }
  return selection;
}

}

#pragma once

#include "openwow/render/api/math/render_math_types.h"
#include "openwow/world/environment/sky.h"
#include "openwow/render/world/environment/spatial_point_light.h"
#include "openwow/render/world/environment/world_model_lighting.h"
#include "openwow/render/world/wmo/wmo_material_pipeline.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace openwow::render {

struct WorldEnvironmentSnapshot {
  std::uint64_t generation{0};
  world::SkyColors sky{};
  RenderFogState fog{};
  WorldM2SceneState models{};
  RenderVec3 surface_to_light{0.0f, 0.0f, 1.0f};
  RenderVec3 ambient{};
  RenderVec3 diffuse{};

  RenderVec3 specular{};

  std::vector<SpatialPointLight> point_lights;
  WmoLightingPalette wmo{};
};

[[nodiscard]] inline float LinearFogVisibility(const RenderFogState& fog,
                                               const float positive_view_depth) noexcept {
  const float start = fog.params[0];
  const float end = std::max(start + 1.0e-4f, fog.params[1]);
  return std::clamp((end - std::max(0.0f, positive_view_depth)) / (end - start),
                    0.0f, 1.0f);
}

}

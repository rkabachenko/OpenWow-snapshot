#pragma once

#include "openwow/render/m2/m2_public_types.h"
#include "openwow/render/api/packed_color.h"
#include "openwow/render/api/math/render_math_types.h"
#include "openwow/world/environment/sky.h"

#include <cmath>
#include <cstddef>

namespace openwow::render {

struct WorldM2SceneState {
  RenderVec3 light_direction{0.0f, 0.0f, -1.0f};

  RenderVec3 ambient_color{1.0f, 1.0f, 1.0f};
  RenderVec3 diffuse_color{0.0f, 0.0f, 0.0f};
  RenderFogState fog{};
};

inline RenderVec3 NormalizeRetailModelLightDirection(RenderVec3 direction) {
  constexpr float kNormalizeLengthSquaredEpsilon = 2.3841858e-7f;
  const float length_squared = direction[0] * direction[0] +
                               direction[1] * direction[1] +
                               direction[2] * direction[2];
  if (length_squared <= kNormalizeLengthSquaredEpsilon) {
    return direction;
  }
  const float inverse_length = 1.0f / std::sqrt(length_squared);
  direction[0] *= inverse_length;
  direction[1] *= inverse_length;
  direction[2] *= inverse_length;
  return direction;
}

inline RenderVec3 WorldM2SurfaceToLightDirection(const WorldM2SceneState &state) {
  return {
      -state.light_direction[0],
      -state.light_direction[1],
      -state.light_direction[2],
  };
}

inline WorldM2SceneState BuildWorldM2SceneState(
    const world::SkyColors &sky_colors, const RenderVec3 &finalized_daynight_direction,
    const RenderFogState &fog) {
  WorldM2SceneState state{};
  state.light_direction =
      NormalizeRetailModelLightDirection(finalized_daynight_direction);

  state.ambient_color =
      PackedArgbToUnitRgb(
          sky_colors.colors[static_cast<std::size_t>(world::SkyColorSlot::kGlobalAmbient)]);
  state.diffuse_color =
      PackedArgbToUnitRgb(
          sky_colors.colors[static_cast<std::size_t>(world::SkyColorSlot::kGlobalDiffuse)]);
  state.fog = fog;
  return state;
}

inline void ApplyWorldM2SceneState(
    const WorldM2SceneState &scene,
    m2::M2BatchUniforms *uniforms) {
  if (uniforms == nullptr) {
    return;
  }

  const RenderVec3 surface_to_light = WorldM2SurfaceToLightDirection(scene);
  uniforms->fog_params = scene.fog.params;
  uniforms->fog_color = scene.fog.color;

  uniforms->light_ambient = {scene.ambient_color[0], scene.ambient_color[1],
                             scene.ambient_color[2], 0.0f};
  uniforms->light_count = {1.0f, 0.0f, 0.0f, 0.0f};
  uniforms->light_pos_range[0] = {
      surface_to_light[0], surface_to_light[1], surface_to_light[2], 0.0f};
  uniforms->light_attenuation[0] = {0.0f, 1.0f, 0.0f, 0.0f};
  uniforms->light_color[0] = {scene.diffuse_color[0], scene.diffuse_color[1],
                              scene.diffuse_color[2], 0.0f};
}

}

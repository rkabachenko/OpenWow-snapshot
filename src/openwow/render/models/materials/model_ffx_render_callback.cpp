#include "openwow/render/models/materials/model_ffx_render_callback.h"

#include "openwow/render/models/materials/model_ffx_context.h"
#include "openwow/render/world/environment/world_light_accumulator.h"

#include <array>
#include <cstdint>
#include <utility>

namespace openwow::render {
namespace {

bool HasNonZeroRgb(const std::array<float, 3>& rgb) {
  return rgb[0] != 0.0f || rgb[1] != 0.0f || rgb[2] != 0.0f;
}

void AccumulateBlockLights(const ModelFfxContextBlock& block,
                           ModelRenderCallbackLightingState& state) {
  WorldLightAccumulator accumulator;
  accumulator.Initialize(0u, {});
  bool any_directional = false;
  std::uint32_t light_handle = 1u;

  for (const ModelLightRecord& record : block.lights) {
    if (record.enabled == 0) {

      continue;
    }
    if (record.light_kind == ModelLightRecord::kRetailPointKind) {
      state.point_lights.push_back({
          .position = record.point_position,
          .diffuse_rgb = record.diffuse_rgb,
          .attenuation = record.scene_light_scalars,
      });
      continue;
    }
    accumulator.AccumulateModelLightRecord(light_handle++, record);
    any_directional = true;
  }

  if (!any_directional) {
    return;
  }

  const ProjectedDirectionalLightState directional =
      accumulator.GetProjectedDirectionalState();
  state.has_directional_light = HasNonZeroRgb(directional.ambient_rgb) ||
                                HasNonZeroRgb(directional.projected_diffuse_rgb);
  state.ambient_rgb = directional.ambient_rgb;
  state.diffuse_rgb = directional.projected_diffuse_rgb;
  state.direction = directional.normalized_light_direction;
}

ModelRenderCallbackLightingState BuildGhostLightingState(
    const ModelRenderCallbackGhostLightSample& sample) {
  ModelLightRecord directional;
  directional.Initialize();
  directional.enabled = 1u;
  directional.light_kind = 0u;
  directional.direction = {0.0f, 0.0f, -1.0f};
  directional.ambient_rgb = sample.ambient_rgb;
  directional.diffuse_rgb = sample.diffuse_rgb;

  ModelFfxContextBlock ghost_block;
  ghost_block.lights.push_back(directional);

  ModelRenderCallbackLightingState state;
  AccumulateBlockLights(ghost_block, state);

  state.fog_mode = ModelRenderCallbackFogMode::kDisabled;
  return state;
}

}

void ApplyModelFfxLightingRenderCallback(
    void*, ModelRenderCallbackContext& render_ctx, const void* user_ctx) {
  const auto* live_block = static_cast<const ModelFfxContextBlock*>(user_ctx);
  if (live_block == nullptr) {
    return;
  }

  const ModelFfxContextBlock* active_block = live_block;
  if (live_block->ghost_branch_gate() == 0u &&
      render_ctx.selected_character_is_ghost()) {
    active_block = live_block + 1;
    if (active_block->lights.empty()) {
      render_ctx.ClearLightingState();
      const auto& ghost_light = render_ctx.ghost_light_sample();
      if (ghost_light.enabled) {
        render_ctx.SetLightingState(BuildGhostLightingState(ghost_light));
      }
      return;
    }
  }

  if (active_block->lights.empty()) {
    return;
  }

  ModelRenderCallbackLightingState state;
  if (const auto* prepared = render_ctx.lighting_state(); prepared != nullptr) {
    state = *prepared;
  }

  state.has_directional_light = false;
  state.ambient_rgb = {};
  state.diffuse_rgb = {};
  state.direction = {};

  AccumulateBlockLights(*active_block, state);
  render_ctx.SetLightingState(std::move(state));
}

}

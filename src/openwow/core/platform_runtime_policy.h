#pragma once

#include <cstdint>

namespace openwow::core {

inline constexpr float kWorldRenderScaleMin = 0.5f;
inline constexpr float kWorldRenderScaleMax = 1.0f;

struct PlatformRuntimePolicy {
  bool constrained_mobile_runtime{false};
  std::uint32_t foreground_fps_limit{200u};
  std::uint32_t background_fps_limit{30u};
  float particle_density{1.0f};
  std::uint32_t weather_density{2u};
  float environment_detail{1.0f};
  float far_clip{350.0f};
  bool glow_enabled{true};
  std::uint32_t resource_worker_limit{4u};
  std::uint32_t glue_texture_worker_limit{4u};
  std::uint32_t texture_uploads_per_frame{8u};
  std::uint32_t glue_texture_uploads_per_frame{128u};
  std::uint32_t transient_vertex_buffer_bytes{32u * 1024u * 1024u};
  std::uint32_t transient_index_buffer_bytes{8u * 1024u * 1024u};
  float default_world_render_scale{1.0f};
  std::int32_t world_tile_load_radius{2};
  std::int32_t world_tile_unload_radius{2};
  std::uint32_t terrain_alpha_map_dimension{64u};
  std::uint16_t texture_array_slices{256u};
};

[[nodiscard]] constexpr PlatformRuntimePolicy GetPlatformRuntimePolicy()
    noexcept {
#if defined(OPENWOW_PLATFORM_IOS)
  return {
      .constrained_mobile_runtime = true,
      .foreground_fps_limit = 60u,
      .background_fps_limit = 15u,
      .particle_density = 0.5f,
      .weather_density = 1u,
      .environment_detail = 0.75f,
      .far_clip = 300.0f,
      .glow_enabled = false,
      .resource_worker_limit = 2u,
      .glue_texture_worker_limit = 2u,
      .texture_uploads_per_frame = 4u,
      .glue_texture_uploads_per_frame = 8u,
      .transient_vertex_buffer_bytes = 16u * 1024u * 1024u,
      .transient_index_buffer_bytes = 4u * 1024u * 1024u,
      .default_world_render_scale = 0.75f,
      .world_tile_load_radius = 1,
      .world_tile_unload_radius = 2,
      .terrain_alpha_map_dimension = 32u,
      .texture_array_slices = 64u,
  };
#else
  return {};
#endif
}

}  // namespace openwow::core

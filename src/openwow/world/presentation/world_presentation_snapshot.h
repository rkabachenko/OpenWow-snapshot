#pragma once

#include "openwow/world/streaming/stream_identity.h"
#include "openwow/world/environment/weather.h"
#include "openwow/world/environment/zone_skybox.h"
#include "openwow/world/wmo/wmo_visibility.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::world {

struct CameraSnapshot {
  MapGeneration map_generation{};
  std::array<float, 3> position{};
  std::array<float, 3> forward{0.0f, 0.0f, -1.0f};
  std::array<float, 3> up{0.0f, 0.0f, 1.0f};
  std::array<float, 16> view{};
  std::array<float, 16> projection{};
  std::array<float, 24> frustum_planes{};
  float near_clip{0.5f};
  float far_clip{350.0f};
};

struct WorldPresentationItem {
  std::uint64_t stable_id{0};
  std::string resource_key;
  std::array<float, 16> transform{};
  bool visible{true};
  std::vector<std::uint8_t> visible_subresources;
  std::vector<std::uint16_t> wmo_seed_groups;
  std::vector<WmoVisibleGroupPath> wmo_visible_group_paths;
  WmoSkyVisibility wmo_sky_visibility;
  WmoExteriorPortalFillBatch wmo_portal_fills;
};

struct WorldFogOverride {
  float start_factor{0.5f};
  float end_distance{500.0f};
  std::uint32_t packed_argb{0xff7f7f7fu};

  std::uint32_t sky_dome_enabled{1u};
};

struct WorldEnvironmentPresentation {
  std::array<float, 3> ambient{};
  std::array<float, 3> diffuse{};
  std::array<float, 3> light_direction{0.0f, 0.0f, 1.0f};
  std::array<float, 3> model_light_direction{0.0f, 0.0f, -1.0f};
  std::array<float, 3> model_ambient{1.0f, 1.0f, 1.0f};
  std::array<float, 3> model_diffuse{};
  std::array<float, 3> wmo_outdoor_ambient{};
  std::array<float, 3> wmo_outdoor_diffuse{};
  std::array<float, 3> window_ambient{};
  std::array<float, 3> window_diffuse{};
  std::uint32_t wmo_material_ambient_argb{};
  std::array<float, 4> fog_color{};
  float fog_start{0.0f};
  float fog_end{0.0f};
  float fog_density{0.0f};
  WeatherKind weather{WeatherKind::kNone};
  float weather_density{0.0f};
  bool indoors{false};

  bool sky_visible{true};
  WmoPortalClipRect sky_clip_rect{};
  bool terrain_visible{true};
  WmoPortalClipRect terrain_clip_rect{};

  bool sky_pass_enabled{true};

  std::uint32_t scene_clear_argb{0u};

  WmoExteriorPortalFillBatch portal_fills;

  std::uint32_t portal_fill_argb{0u};
};

struct WorldSkyPresentation {
  std::array<std::uint32_t, 18> colors{};
  float fog_distance{500.0f};
  float fog_multiplier{1.0f};
  float highlight{};
  float scene_visibility{0.5f};
  float water_shallow_alpha{0.5f};
  float water_deep_alpha{1.0f};
  float ocean_shallow_alpha{0.75f};
  float ocean_deep_alpha{1.0f};
};

struct ShadowPresentationSettings {
  bool enabled{true};
  std::uint8_t quality{2};
  std::uint16_t map_resolution{1024};
  float distance{80.0f};
  float depth_bias{0.005f};
};

struct WorldPresentationSnapshot {
  MapGeneration map_generation{};
  std::uint32_t map_id{0};
  bool minimally_valid{false};
  CameraSnapshot camera{};
  WorldEnvironmentPresentation environment{};
  ShadowPresentationSettings shadows{};
  WorldSkyPresentation sky{};
  std::optional<ZoneSkyboxEntry> primary_skybox;
  std::uint32_t spell_visual_tint_argb{0xffffffffu};
  float spell_visual_tint_blend{};
  float time_of_day_hours{12.0f};
  float wmo_night_glow{};
  bool distant_terrain_enabled{false};
  std::uint64_t wmo_camera_room_transition{0};
  std::optional<std::uint64_t> wmo_camera_room_placement;
  std::optional<std::uint32_t> wmo_camera_room_group;
  std::vector<WorldPresentationItem> world_models;

  [[nodiscard]] bool IsPublishable() const noexcept {
    return map_generation.IsValid() && minimally_valid &&
           camera.map_generation == map_generation;
  }
};

}

#pragma once
#include "openwow/render/world/environment/celestial_renderer.h"
#include "openwow/world/environment/sky.h"
#include "openwow/render/world/environment/sky_settings.h"
#include "openwow/world/environment/zone_skybox.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <cstddef>
#include <cstdint>
#include <array>
#include <bgfx/bgfx.h>

namespace openwow::game {
struct DayNightStarsModelState;
}

namespace openwow::render {

namespace m2 {
class M2System;
}

struct ZoneSkyboxAnimationState {
  std::uint32_t sequence_duration_ms{0};
  std::uint32_t current_animation_time_ms{0};
  float animation_speed{1.0f};
  int last_total_minutes{0};
  bool sample_bound{false};
};

struct ZoneSkyboxAnimationSyncResult {
  bool should_set_sample{false};
  std::uint32_t sample_time_ms{0};
  float playback_speed{1.0f};
};

struct SkyDomeRuntimeState {
  std::array<float, 3> camera_forward{1.0f, 0.0f, 0.0f};
  std::uint32_t sky_update_mask{0};
  float fog_band_fade_factor{0.0f};
  std::uint32_t fog_band_fade_color_argb{0};
  std::uint32_t spell_visual_tint_argb{0};
  std::uint8_t spell_visual_tint_blend{0};
};

struct DayNightSkyModelLoadSummary {
  std::size_t active_slot_count{0};
  std::size_t loaded_slot_count{0};
};

struct SkyModelRenderPlan {
  bool render_procedural_dome{true};
  bool render_zone_skybox{false};
  bool render_daynight_sky_models{false};
};

struct StarsModelRenderPlan {
  bool render{false};
  float alpha{0.0f};
  std::array<float, 3> position{};
};

class SkyRenderer {
 public:
  SkyRenderer(TextureManager& texture_manager,
              m2::M2System& m2_system,
              SkySettingsProvider sky_settings);
  ~SkyRenderer();

  SkyRenderer(const SkyRenderer&) = delete;
  SkyRenderer& operator=(const SkyRenderer&) = delete;

  bool Initialize();

  using LoadFileCallback =
      std::function<std::vector<std::uint8_t>(const std::string& path)>;

  void SetFileLoader(LoadFileCallback callback);
  void Update(float dt_seconds, float hour_of_day);
  void UpdateLoading();
  void SetZoneSkybox(std::optional<world::ZoneSkyboxEntry> entry);

  void SetColors(const world::SkyColors& colors);
  void SetDomeRuntimeState(const SkyDomeRuntimeState& state);

  void Render(uint8_t view_id, const float* view_mtx, const float* proj_mtx,
              float camera_x, float camera_y, float camera_z,
              bgfx::Encoder* encoder = nullptr);

  void RenderZoneSkybox(uint8_t view_id, const float* view_mtx,
                        const float* proj_mtx, float camera_x, float camera_y,
                        float camera_z);

  void Shutdown();

  void SetTimeOfDay(float hour);
  [[nodiscard]] float GetTimeOfDay() const { return time_of_day_; }

  static void ComputeSunDirection(float hour, float out[3]);

  void SetIndoorColors(const world::SkyColors& colors);
  void ClearIndoorColors();
  [[nodiscard]] bool IsIndoor() const { return is_indoor_; }

  [[nodiscard]] const world::SkyColors& active_colors() const {
    return is_indoor_ ? indoor_colors_ : current_colors_;
  }
  [[nodiscard]] float fog_distance() const { return active_colors().fog_distance; }
  [[nodiscard]] float fog_multiplier() const { return active_colors().fog_multiplier; }

  [[nodiscard]] CelestialRenderer& GetCelestialRenderer() { return celestial_renderer_; }
  [[nodiscard]] const CelestialRenderer& GetCelestialRenderer() const {
    return celestial_renderer_;
  }

  void Reset();

  [[nodiscard]] const std::optional<world::ZoneSkyboxEntry>& zone_skybox() const {
    return zone_skybox_;
  }

  [[nodiscard]] const ZoneSkyboxAnimationState& zone_skybox_animation_state()
      const {
    return zone_skybox_animation_state_;
  }

  static void SyncZoneSkyboxAnimation(ZoneSkyboxAnimationState& state,
                                      std::uint32_t sequence_duration_ms,
                                      std::uint32_t skybox_flags,
                                      int total_minutes);
  [[nodiscard]] static ZoneSkyboxAnimationSyncResult SyncZoneSkyboxAnimationForSample(
      ZoneSkyboxAnimationState& state, std::uint32_t sequence_duration_ms,
      std::uint32_t skybox_flags, int total_minutes);
  [[nodiscard]] static SkyModelRenderPlan BuildSkyModelRenderPlan(
      const DayNightSkyModelLoadSummary& summary, bool has_zone_skybox);
  [[nodiscard]] static StarsModelRenderPlan BuildStarsModelRenderPlan(
      const openwow::game::DayNightStarsModelState& state);

 private:
  struct ZoneSkyboxResource;
  struct DayNightSkyModelResource;
  struct StarsModelResource;

  void BuildDomeMesh();
  void UpdateRetailDomeVertexColors();
  void UpdateZoneSkyboxAnimation(float dt_seconds);
  void UpdateDayNightSkyModelAnimations(float dt_seconds);
  void AccumulateStarsModelAnimation(float dt_seconds);
  void UnloadZoneSkybox();
  void UnloadDayNightSkyModelResource(DayNightSkyModelResource& resource);
  void UnloadDayNightSkyModelResources();
  void UnloadStarsModel();
  void LoadZoneSkyboxIfNeeded();
  [[nodiscard]] DayNightSkyModelLoadSummary LoadDayNightSkyModelSlotsIfNeeded();
  void LoadStarsModelIfNeeded();
  void RenderStarsModel(uint8_t view_id, const float* view_mtx);
  void RenderLoadedSkyModel(uint8_t view_id, const float* view_mtx,
                            const float* proj_mtx, float camera_x,
                            float camera_y, float camera_z,
                            DayNightSkyModelResource& resource, float alpha,
                            std::uint32_t flags);

  bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
  bgfx::DynamicVertexBufferHandle vb_ = BGFX_INVALID_HANDLE;
  bgfx::IndexBufferHandle ib_ = BGFX_INVALID_HANDLE;
  bgfx::VertexLayout layout_{};
  uint32_t index_count_{0};
  world::SkyColors current_colors_{};
  bool initialized_{false};
  float time_of_day_{12.0f};
  bool is_indoor_{false};
  world::SkyColors indoor_colors_{};
  LoadFileCallback load_file_;
  std::optional<world::ZoneSkyboxEntry> zone_skybox_;
  std::unique_ptr<ZoneSkyboxResource> zone_skybox_resource_;
  std::array<std::unique_ptr<DayNightSkyModelResource>, 4> daynight_sky_model_resources_;
  std::unique_ptr<StarsModelResource> stars_model_resource_;
  ZoneSkyboxAnimationState zone_skybox_animation_state_{};
  SkyDomeRuntimeState dome_runtime_state_{};
  std::vector<std::array<float, 3>> dome_positions_;
  std::vector<std::uint32_t> dome_colors_;
  std::vector<std::uint16_t> dome_indices_;
  bool dome_colors_dirty_{true};

  SkySettingsProvider sky_settings_;
  m2::M2System& m2_system_;
  CelestialRenderer celestial_renderer_;
};

}

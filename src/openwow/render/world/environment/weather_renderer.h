#pragma once

#include "openwow/render/api/draw_encoder.h"
#include "openwow/render/api/math/render_math_types.h"
#include "openwow/render/resources/textures/texture_lease.h"
#include "openwow/world/environment/weather.h"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace openwow::render {

class TextureManager;

class WeatherRenderer {
 public:
  using GroundHeightSampler =
      std::function<std::optional<float>(float x, float y, float z)>;
  using CollisionSampler = std::function<std::optional<world::WeatherCollisionHit>(
      const RenderVec3& origin, const RenderVec3& direction,
      float maximum_distance)>;

  explicit WeatherRenderer(TextureManager& texture_manager);
  ~WeatherRenderer();

  bool Initialize();

  void SetGroundHeightSampler(GroundHeightSampler sampler);
  void SetCollisionSampler(CollisionSampler sampler);
  void Update(float dt, const RenderVec3& camera_position,
              const world::WeatherState& weather,
              float particle_density_scale,
              bool use_weather_shaders);

  void Render(std::uint8_t view_id, const RenderMatrix4x4& view,
              const RenderMatrix4x4& projection,
              const RenderVec3& camera_position,
              const RenderVec4& fog_color,
              bgfx::Encoder* encoder = nullptr);
  void Reset();
  void Shutdown();

  static constexpr std::size_t kMaximumGroundProfileSamples = 64u;

 private:

  struct MistParticle {
    RenderVec3 position{};
    RenderVec3 velocity{};
    float acceleration{};
    float age{};
    float lifetime{};
    std::array<float, kMaximumGroundProfileSamples> ground_profile{};
    std::uint8_t ground_profile_samples{};
  };

  struct PrimaryParticle {
    RenderVec3 position{};
    RenderVec3 velocity{};
    RenderVec3 collision_position{};
    RenderVec3 collision_normal{0.0f, 0.0f, 1.0f};
    float age{};
    float motion_lifetime{};
    float lifetime{};
    bool has_collision{};
  };

  struct RainSplashParticle {
    RenderVec3 position{};
    float age{};
  };

  void SpawnPrimary(float dt, const RenderVec3& camera_position,
                    const world::WeatherState& weather,
                    float particle_density_scale,
                    bool use_weather_shaders);
  void SpawnMist(float dt, const RenderVec3& camera_position,
                const world::WeatherState& weather,
                float particle_density_scale,
                bool use_weather_shaders);

  void AdvancePrimary(float dt, const RenderVec3& camera_position);

  void ActivateWeather(const world::WeatherState& weather);

  [[nodiscard]] bool BuildGroundProfile(MistParticle& particle) const;

  [[nodiscard]] float SampleGroundProfile(const MistParticle& particle) const;

  void RefreshPrimaryTexture(const world::WeatherState& weather);

  void RefreshMistTexture(world::WeatherKind kind);

  void RefreshRainSplashTexture(world::WeatherKind kind);

  TextureManager& texture_manager_;
  GroundHeightSampler ground_height_sampler_;
  CollisionSampler collision_sampler_;
  world::WeatherKind kind_{world::WeatherKind::kNone};
  std::uint32_t active_color_abgr_{0xffffffffu};
  bool retiring_{};
  std::mt19937 random_{0x335a1234u};

  float primary_spawn_credit_{};
  std::vector<PrimaryParticle> primary_particles_;
  bgfx::DynamicVertexBufferHandle primary_vertices_ = BGFX_INVALID_HANDLE;
  std::string primary_bound_texture_path_;
  TextureLease primary_texture_lease_;

  std::vector<RainSplashParticle> rain_splash_particles_;
  bgfx::DynamicVertexBufferHandle rain_splash_vertices_ = BGFX_INVALID_HANDLE;
  TextureLease rain_splash_texture_lease_;

  float mist_spawn_credit_{};
  std::vector<MistParticle> mist_particles_;
  bgfx::DynamicVertexBufferHandle mist_vertices_ = BGFX_INVALID_HANDLE;
  world::WeatherKind mist_bound_kind_{world::WeatherKind::kNone};
  TextureLease mist_texture_lease_;

  bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle sampler_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle weather_params_ = BGFX_INVALID_HANDLE;
  bgfx::VertexLayout layout_{};
  bool initialized_{};
};

}

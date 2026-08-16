#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <bgfx/bgfx.h>

#include "openwow/render/effects/particles/particle_renderer.h"
#include "openwow/render/api/math/render_math_types.h"

namespace openwow::render {

enum class ParticleBlendMode : uint8_t {
  kOpaque = 0,
  kAlphaKey = 1,
  kAlpha = 2,
  kAdd = 3,
  kMod = 4,
  kMod2x = 5,
  kCount
};

enum class EmitterShape : uint8_t {
  kPlane = 0,
  kSphere = 1,
  kSpline = 2
};

struct ParticleEmitterConfig {
  EmitterShape shape = EmitterShape::kPlane;
  ParticleBlendMode blend_mode = ParticleBlendMode::kAlpha;

  float emission_rate = 10.0f;
  float emission_speed = 5.0f;
  float speed_variation = 0.2f;
  float area_width = 1.0f;
  float area_height = 1.0f;

  float gravity = 0.0f;
  float lifespan = 2.0f;
  float lifespan_variation = 0.3f;

  std::array<uint32_t, 3> colors = {0xFFFFFFFF, 0xFFFFFFFF, 0x00FFFFFF};
  std::array<float, 3> color_times = {0.0f, 0.5f, 1.0f};

  std::array<float, 3> scales = {1.0f, 1.0f, 0.0f};

  uint16_t texture_rows = 1;
  uint16_t texture_cols = 1;
  uint16_t head_cell_begin = 0;
  uint16_t head_cell_end = 0;

  bool world_space = false;
  bool no_z_buffer = false;

  uint32_t max_particles = 500;
};

struct Particle {
  float position[3];
  float velocity[3];
  float life;
  float max_life;
  float size;
};

class ParticleEmitter {
 public:
  void Configure(const ParticleEmitterConfig& config);

  void SetPosition(float x, float y, float z);

  void SetDirection(float dx, float dy, float dz);

  void Update(float dt);

  [[nodiscard]] bool IsActive() const { return active_; }
  void SetActive(bool a) { active_ = a; }

  [[nodiscard]] const std::vector<Particle>& particles() const {
    return particles_;
  }
  [[nodiscard]] const ParticleEmitterConfig& config() const { return config_; }

 private:
  void SpawnParticle();

  ParticleEmitterConfig config_;
  std::vector<Particle> particles_;
  float emit_accumulator_ = 0.0f;
  float pos_[3] = {};
  float dir_[3] = {0.0f, 1.0f, 0.0f};
  bool active_ = true;
};

class ParticleSystem {
 public:
  ParticleSystem();
  ~ParticleSystem();

  ParticleSystem(const ParticleSystem&) = delete;
  ParticleSystem& operator=(const ParticleSystem&) = delete;

  bool Initialize();

  uint32_t CreateEmitter(const ParticleEmitterConfig& config);

  ParticleEmitter* GetEmitter(uint32_t id);

  void RemoveEmitter(uint32_t id);

  void Update(float dt);

  void Render(uint8_t view_id, const float* view_mtx, const float* proj_mtx,
              float cam_x, float cam_y, float cam_z);

  void SetFogParams(const RenderFogState& fog);

  void Shutdown();
  void ClearAll();

  [[nodiscard]] size_t emitter_count() const;
  [[nodiscard]] size_t total_particle_count() const;

 private:
  struct EmitterSlot {
    ParticleEmitter emitter;
    uint32_t id = 0;
    bool in_use = false;
  };

  std::vector<EmitterSlot> emitters_;
  uint32_t next_id_ = 1;

  bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
  bgfx::VertexLayout layout_{};
  bgfx::DynamicVertexBufferHandle dvb_ = BGFX_INVALID_HANDLE;
  bgfx::DynamicIndexBufferHandle dib_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_fog_params_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_fog_color_ = BGFX_INVALID_HANDLE;

  bgfx::UniformHandle u_particle_flags_ = BGFX_INVALID_HANDLE;
  RenderFogState fog_{};
  bool initialized_ = false;
  ParticleRenderer batch_planner_{};

  static constexpr uint32_t kMaxTotalParticles = 10000;
};

}

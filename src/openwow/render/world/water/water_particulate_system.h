#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <bgfx/bgfx.h>

#include "openwow/foundation/hashing/retail_adler_seed.h"
#include "openwow/render/resources/textures/texture_manager.h"

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::render {

class WaterParticulateSystem {
 public:
  explicit WaterParticulateSystem(TextureManager& texture_manager);
  ~WaterParticulateSystem();

  WaterParticulateSystem(const WaterParticulateSystem&) = delete;
  WaterParticulateSystem& operator=(const WaterParticulateSystem&) = delete;

  bool Initialize();
  void Shutdown();
  void Reset();

  void BindDbc(const openwow::data::dbc::DbcLoader* dbc);

  void Update(float dt, float camera_x, float camera_y, float camera_z,
              std::uint32_t observed_liquid_type_id,
              bool water_particulates_enabled);

  void Render(std::uint8_t view_id, const float* view_mtx,
              const float* proj_mtx,
              const std::array<float, 4>& fog_params,
              const std::array<float, 4>& fog_color,
              bgfx::Encoder* encoder = nullptr) const;

 private:
  TextureManager& texture_manager_;
  struct Particle {
    std::array<float, 3> offset{};
    float size{0.0f};
  };

  struct RuntimeLiquidState {
    std::uint32_t liquid_type_id{0};

    std::uint32_t flags{0};
    std::uint32_t particle_movement{0};
    std::uint32_t particle_tex_slots{0};
    float size_base{0.0f};
    bool render_enabled{false};
  };

  struct ParticleVertex {
    float position[3];
    std::uint32_t abgr;
    float texcoord[2];
  };

  void EnsureTextureLoaded();
  void EnsureRandomStateSeeded();
  void ApplyObservedLiquidTransition(std::uint32_t observed_liquid_type_id,
                                     bool water_particulates_enabled);
  void ResetParticles(std::uint32_t liquid_type_id);
  void ResetMotion();
  [[nodiscard]] std::array<float, 3> ComputeMotionDelta(float dt);
  [[nodiscard]] float NextUnitFloat();

  const openwow::data::dbc::DbcLoader* dbc_{nullptr};
  std::vector<Particle> particles_;
  RuntimeLiquidState liquid_state_{};
  std::array<float, 3> camera_position_{};
  std::array<float, 3> previous_camera_position_{};
  std::array<float, 3> motion_direction_{};

  bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_particle_flags_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle s_tex_color_ = BGFX_INVALID_HANDLE;

  bgfx::UniformHandle u_fog_params_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_fog_color_ = BGFX_INVALID_HANDLE;
  TextureLease texture_lease_;
  bgfx::TextureHandle texture_ = BGFX_INVALID_HANDLE;
  bgfx::VertexLayout layout_{};

  float motion_frequency_{0.0f};
  float motion_elapsed_{0.0f};
  float motion_amplitude_{0.0f};
  std::uint32_t observed_liquid_type_id_{0};
  bool render_flag_enabled_{true};
  bool initialized_{false};
  bool random_state_seeded_{false};
  openwow::foundation::hashing::AdlerSeedState random_state_{};
};

}

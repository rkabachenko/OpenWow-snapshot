#pragma once

#include "openwow/world/environment/zone_skybox.h"
#include "openwow/render/api/draw_encoder.h"
#include "openwow/render/resources/textures/texture_manager.h"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <string>

namespace openwow::render {

class CelestialRenderer {
 public:
  explicit CelestialRenderer(TextureManager& texture_manager);
  ~CelestialRenderer();

  CelestialRenderer(const CelestialRenderer&) = delete;
  CelestialRenderer& operator=(const CelestialRenderer&) = delete;

  void SetGlareEnabled(bool enabled);
  [[nodiscard]] bool IsGlareEnabled() const { return glare_enabled_; }

  void SetZoneSkybox(const world::ZoneSkyboxEntry& entry);
  void ClearZoneSkybox();
  [[nodiscard]] bool HasZoneSkybox() const;
  [[nodiscard]] const world::ZoneSkyboxEntry& GetZoneSkybox() const;

  bool Initialize();

  void Shutdown();

  void Update();

  void Render(uint8_t view_id, const float* view_mtx, const float* proj_mtx,
              float camera_x, float camera_y, float camera_z,
              bgfx::Encoder* encoder = nullptr);

  void Reset();

 public:
  struct CelestialVertex {
    float x, y, z;
    uint32_t abgr;
    float u, v;
  };

  static constexpr std::size_t kCloudTextureCount = 2;

 private:

  void EnsureCloudTextures(std::uint32_t grid_size);
  void DestroyCloudTextures();

  void SubmitCloudDome(uint8_t view_id, float camera_x, float camera_y,
                       float camera_z, const DrawEncoder& draw);

  TextureManager& texture_manager_;

  bool glare_enabled_{true};

  bool has_zone_skybox_{false};
  world::ZoneSkyboxEntry zone_skybox_;

  bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
  std::array<bgfx::TextureHandle, kCloudTextureCount> cloud_textures_{
      {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
  std::uint32_t cloud_texture_grid_size_{0};
  TextureLease sun_glare_texture_;
  TextureLease moon_glare_texture_;
  bgfx::UniformHandle s_tex_color_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_celestial_params_ = BGFX_INVALID_HANDLE;
  bgfx::VertexLayout vertex_layout_{};
  bool initialized_{false};
};

}

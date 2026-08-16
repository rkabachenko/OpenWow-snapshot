#pragma once

#include "openwow/render/resources/textures/texture_lease.h"
#include "openwow/render/scene/projected_decal_conjugation.h"
#include "openwow/world/collision/collision.h"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace openwow::render {

class TextureManager;

struct SelectionDecal {
  float center_x{0.0f};
  float center_y{0.0f};
  float center_z{0.0f};

  float xy_half_extent{1.2f};

  float z_half_extent{1.2f};

  std::uint32_t packed_argb{0xFFFFFFFFu};
};

class SelectionCircle {
 public:
  SelectionCircle() = default;
  ~SelectionCircle() = default;

  SelectionCircle(const SelectionCircle&) = delete;
  SelectionCircle& operator=(const SelectionCircle&) = delete;

  bool Initialize(TextureManager& texture_manager);

  using FacetGather = std::function<void(
      const std::array<float, 6>& world_bounds,
      const world::CollisionFacetVisitor& visitor)>;

  void BeginFrame();

  void Submit(const SelectionDecal& decal);

  void Render(std::uint8_t view_id, const float* view_mtx,
              const float* proj_mtx, float cam_x, float cam_y, float cam_z,
              const FacetGather& gather);

  void Shutdown();

  [[nodiscard]] bool has_target() const { return !decals_.empty(); }

 private:
  struct DecalVertex {
    float px, py, pz;

    float u, v, fade_s;
    std::uint32_t abgr;
  };

  void SubmitDraw(std::uint8_t view_id, const SelectionDecal& decal,
                  float cam_x, float cam_y, const FacetGather& gather);

  bool initialized_{false};

  bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle s_tex_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_decal_params_ = BGFX_INVALID_HANDLE;
  TextureLease decal_texture_lease_;
  bgfx::VertexLayout layout_;

  std::vector<SelectionDecal> decals_;

  std::vector<DecalVertex> vertices_;

  static constexpr std::size_t kMaxTriangles = 512;

  static constexpr float kMinFootprintExtent = 0.001f;

  static constexpr const char* kSelectionDecalTexturePath =
      "Textures\\UnitSelectTexture.blp";
};

}

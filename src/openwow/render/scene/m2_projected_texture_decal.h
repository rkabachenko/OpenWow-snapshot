#pragma once

#include "openwow/render/m2/m2_public_types.h"
#include "openwow/render/scene/projected_decal_conjugation.h"
#include "openwow/world/collision/collision.h"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <unordered_map>
#include <vector>

namespace openwow::render {

class M2ProjectedTextureDecalRenderer {
 public:
  M2ProjectedTextureDecalRenderer() = default;
  ~M2ProjectedTextureDecalRenderer() = default;

  M2ProjectedTextureDecalRenderer(const M2ProjectedTextureDecalRenderer&) = delete;
  M2ProjectedTextureDecalRenderer& operator=(const M2ProjectedTextureDecalRenderer&) =
      delete;

  bool Initialize();
  void Shutdown();

  using FacetGather = std::function<void(
      const std::array<float, 6>& world_bounds,
      const world::CollisionFacetVisitor& visitor)>;

  void Render(std::uint8_t view_id, const float* view_mtx, const float* proj_mtx,
              std::span<const m2::M2ProjectedTextureDraw> draws,
              const FacetGather& gather);

 private:
  struct DecalVertex {
    float px, py, pz;

    float u, v, fade_s;
    std::uint32_t abgr;
  };

  struct CachedGeometry {
    std::array<float, 6> bounds{};
    std::uint64_t frame_stamp{0};
    std::vector<DecalVertex> vertices;
  };

  void SubmitDraw(std::uint8_t view_id, const m2::M2ProjectedTextureDraw& draw,
                  const FacetGather& gather);

  bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle s_tex_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_decal_params_ = BGFX_INVALID_HANDLE;
  bgfx::VertexLayout layout_;
  bool initialized_{false};

  std::unordered_map<std::uint64_t, CachedGeometry> geometry_;
  std::uint64_t frame_stamp_{0};

  static constexpr float kCacheSlopSquared = 0.01f * 0.01f;

  static constexpr std::size_t kMaxTrianglesPerDecal = 512;
};

}

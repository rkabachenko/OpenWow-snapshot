#pragma once

#include "openwow/game/object_presentation_snapshot.h"
#include "openwow/render/scene/projected_decal_conjugation.h"
#include "openwow/world/collision/collision.h"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace openwow::render {

class ObjectRenderer;

class BlobShadowRenderer {
 public:
  BlobShadowRenderer() = default;
  ~BlobShadowRenderer() = default;

  BlobShadowRenderer(const BlobShadowRenderer&) = delete;
  BlobShadowRenderer& operator=(const BlobShadowRenderer&) = delete;

  bool Initialize();

  void Shutdown();

  using FacetGather = std::function<void(
      const std::array<float, 6>& world_bounds,
      const world::CollisionFacetVisitor& visitor)>;

  void Render(std::uint8_t view_id, const float* view_mtx,
              const float* proj_mtx,
              const game::ObjectPresentationSnapshot& objects,
              const ObjectRenderer* object_renderer,
              const FacetGather& gather);

 private:

  void CreateShadowTexture();

  struct ShadowVertex {
    float px, py, pz;

    float u, v, fade_s;
    std::uint32_t abgr;
  };

  void SubmitProjectedShadow(std::uint8_t view_id, std::uint64_t guid,
                             const std::array<float, 6>& box,
                             std::uint8_t vertex_alpha,
                             const FacetGather& gather);

  struct CachedShadowGeometry {
    std::array<float, 6> box{};
    std::uint8_t vertex_alpha{0};
    std::uint64_t frame_stamp{0};
    std::vector<ShadowVertex> vertices;
  };

  bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle shadow_tex_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle s_tex_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_decal_params_ = BGFX_INVALID_HANDLE;
  bgfx::VertexLayout layout_;

  bool initialized_{false};

  std::unordered_map<std::uint64_t, CachedShadowGeometry> shadow_geometry_;
  std::uint64_t shadow_frame_stamp_{0};

  static constexpr float kShadowCacheSlopSquared = 0.01f * 0.01f;

  static constexpr float kShadowXyHalfExtentClamp = 5.0f;

  static constexpr float kShadowZBelowPivotFactor = 1.6666667f;
  static constexpr float kShadowZAbovePivotFactor = 1.0f;

  static constexpr std::size_t kMaxTrianglesPerShadow = 256;

  static constexpr int kTexSize = 32;
};

}

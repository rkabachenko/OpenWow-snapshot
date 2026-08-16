#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "openwow/render/api/math/render_math_types.h"

namespace openwow::render::occlusion {

inline constexpr bool kOcclusionCullingEnabled = false;

inline constexpr int kOcclusionBufferWidth = 256;
inline constexpr int kOcclusionBufferHeight = 144;

inline constexpr int kOcclusionTileSize = 8;
inline constexpr int kOcclusionTilesX =
    kOcclusionBufferWidth / kOcclusionTileSize;
inline constexpr int kOcclusionTilesY =
    kOcclusionBufferHeight / kOcclusionTileSize;
static_assert(kOcclusionBufferWidth % kOcclusionTileSize == 0);
static_assert(kOcclusionBufferHeight % kOcclusionTileSize == 0);

inline constexpr std::size_t kMaxOccluderPolygonsPerFrame = 6144u;

inline constexpr std::size_t kMaxOccluderPolygonVertices = 8u;

struct OccluderPolygon {
  std::array<RenderVec3, kMaxOccluderPolygonVertices> positions{};
  RenderVec3 outward_normal{};
  std::uint8_t vertex_count{0u};
};

[[nodiscard]] float ResolveProjectionNearDepth(RenderMatrix4x4View projection,
                                               bool homogeneous_depth) noexcept;

class OcclusionDepthBuffer {
 public:

  void BeginFrame(float near_depth);

  [[nodiscard]] bool active() const noexcept { return active_; }

  [[nodiscard]] std::size_t rasterized_polygon_count() const noexcept {
    return rasterized_polygon_count_;
  }
  [[nodiscard]] std::size_t occluded_query_count() const noexcept {
    return occluded_query_count_;
  }
  [[nodiscard]] std::size_t query_count() const noexcept {
    return query_count_;
  }

  [[nodiscard]] float CoveredTexelFraction() const noexcept;
  [[nodiscard]] float TexelInverseDepth(int x, int y) const noexcept;

  void AddOccluders(std::span<const OccluderPolygon> polygons,
                    RenderMatrix4x4View model_view_projection,
                    RenderVec3View eye_in_model_space, bool two_sided);

  [[nodiscard]] bool IsBoundsOccluded(
      RenderVec3View bounds_min, RenderVec3View bounds_max,
      RenderMatrix4x4View model_view_projection) const;

 private:

  struct ScreenVertex {
    float x{0.0f};
    float y{0.0f};
    float inverse_depth{0.0f};
  };

  void RasterizeClipPolygon(std::span<const RenderVec4> clip_polygon);
  void RasterizeScreenPolygon(std::span<const ScreenVertex> screen_polygon);
  [[nodiscard]] float TileMinimum(int tile_x, int tile_y) const;
  [[nodiscard]] bool IsRectOccluded(int min_x, int min_y, int max_x, int max_y,
                                    float nearest_inverse_depth) const;

  std::vector<float> nearest_inverse_depth_;

  mutable std::vector<float> tile_minimum_;
  mutable std::vector<std::uint8_t> tile_dirty_;

  float near_depth_{0.0f};
  std::size_t rasterized_polygon_count_{0u};
  mutable std::size_t query_count_{0u};
  mutable std::size_t occluded_query_count_{0u};
  bool active_{false};
};

}

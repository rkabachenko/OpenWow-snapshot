#include "openwow/render/scene/occlusion/occlusion_depth_buffer.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "openwow/render/api/math/render_matrix_math.h"

namespace openwow::render::occlusion {

namespace {

constexpr float kEdgeConservativeInsetTexels = 0.05f;

constexpr float kDepthConservativeRelativeMargin = 1.0e-4f;

constexpr float kMinimumOccluderScreenArea2 = 2.0f;

constexpr std::size_t kMaxClippedPolygonVertices =
    kMaxOccluderPolygonVertices + 5u;

struct Edge {
  float a{0.0f};
  float b{0.0f};
  float c{0.0f};
};

[[nodiscard]] bool IsFinite(const RenderVec4& v) noexcept {
  return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]) &&
         std::isfinite(v[3]);
}

template <typename SignedDistance>
[[nodiscard]] std::size_t ClipPolygonAgainstPlane(
    const std::array<RenderVec4, kMaxClippedPolygonVertices>& input,
    const std::size_t input_count,
    std::array<RenderVec4, kMaxClippedPolygonVertices>& output,
    const SignedDistance& distance) noexcept {
  std::size_t output_count = 0u;
  for (std::size_t index = 0; index < input_count; ++index) {
    const RenderVec4& current = input[index];
    const RenderVec4& next = input[(index + 1u) % input_count];
    const float current_distance = distance(current);
    const float next_distance = distance(next);
    const bool current_inside = current_distance >= 0.0f;
    const bool next_inside = next_distance >= 0.0f;
    if (current_inside && output_count < output.size()) {
      output[output_count++] = current;
    }
    if (current_inside != next_inside && output_count < output.size()) {
      const float denominator = current_distance - next_distance;
      if (!(std::abs(denominator) > 0.0f)) {
        continue;
      }
      const float t = current_distance / denominator;
      RenderVec4 crossing{};
      for (std::size_t axis = 0; axis < crossing.size(); ++axis) {
        crossing[axis] = current[axis] + (next[axis] - current[axis]) * t;
      }
      output[output_count++] = crossing;
    }
  }
  return output_count;
}

}

float ResolveProjectionNearDepth(const RenderMatrix4x4View projection,
                                 const bool homogeneous_depth) noexcept {

  const float z_scale = projection[10];
  const float z_offset = projection[14];
  const float w_scale = projection[11];
  if (!std::isfinite(z_scale) || !std::isfinite(z_offset) ||
      !std::isfinite(w_scale) || !(w_scale > 0.0f)) {
    return 0.0f;
  }
  const float denominator = homogeneous_depth ? z_scale + w_scale : z_scale;
  if (!(denominator > 0.0f)) {
    return 0.0f;
  }
  const float near_depth = -z_offset / denominator;
  return std::isfinite(near_depth) && near_depth > 0.0f ? near_depth : 0.0f;
}

void OcclusionDepthBuffer::BeginFrame(const float near_depth) {
  rasterized_polygon_count_ = 0u;
  query_count_ = 0u;
  occluded_query_count_ = 0u;
  active_ = false;
  if constexpr (!kOcclusionCullingEnabled) {
    return;
  }
  if (!std::isfinite(near_depth) || !(near_depth > 0.0f)) {
    return;
  }
  near_depth_ = near_depth;
  const std::size_t texel_count = static_cast<std::size_t>(
      kOcclusionBufferWidth * kOcclusionBufferHeight);
  const std::size_t tile_count =
      static_cast<std::size_t>(kOcclusionTilesX * kOcclusionTilesY);
  if (nearest_inverse_depth_.size() != texel_count) {
    nearest_inverse_depth_.assign(texel_count, 0.0f);
    tile_minimum_.assign(tile_count, 0.0f);
    tile_dirty_.assign(tile_count, 0u);
  } else {
    std::fill(nearest_inverse_depth_.begin(), nearest_inverse_depth_.end(),
              0.0f);
    std::fill(tile_minimum_.begin(), tile_minimum_.end(), 0.0f);
    std::fill(tile_dirty_.begin(), tile_dirty_.end(), 0u);
  }
  active_ = true;
}

void OcclusionDepthBuffer::AddOccluders(
    const std::span<const OccluderPolygon> polygons,
    const RenderMatrix4x4View model_view_projection,
    const RenderVec3View eye_in_model_space, const bool two_sided) {
  if (!active_) {
    return;
  }
  for (const OccluderPolygon& polygon : polygons) {
    if (rasterized_polygon_count_ >= kMaxOccluderPolygonsPerFrame) {
      return;
    }
    const std::size_t vertex_count = polygon.vertex_count;
    if (vertex_count < 3u || vertex_count > kMaxOccluderPolygonVertices) {
      continue;
    }
    if (!two_sided) {

      const RenderVec3& base = polygon.positions[0];
      const float facing =
          polygon.outward_normal[0] * (eye_in_model_space[0] - base[0]) +
          polygon.outward_normal[1] * (eye_in_model_space[1] - base[1]) +
          polygon.outward_normal[2] * (eye_in_model_space[2] - base[2]);
      if (!(facing > 0.0f)) {
        continue;
      }
    }

    std::array<RenderVec4, kMaxClippedPolygonVertices> clip{};
    bool projected = true;
    for (std::size_t corner = 0; corner < vertex_count; ++corner) {
      const std::array<float, 4> homogeneous{polygon.positions[corner][0],
                                             polygon.positions[corner][1],
                                             polygon.positions[corner][2],
                                             1.0f};
      clip[corner] = TransformRowVector4x4(RenderVec4View{homogeneous},
                                           model_view_projection);
      projected = projected && IsFinite(clip[corner]);
    }
    if (!projected) {
      continue;
    }
    ++rasterized_polygon_count_;
    RasterizeClipPolygon(
        std::span<const RenderVec4>{clip.data(), vertex_count});
  }
}

void OcclusionDepthBuffer::RasterizeClipPolygon(
    const std::span<const RenderVec4> clip_polygon) {
  std::array<RenderVec4, kMaxClippedPolygonVertices> front{};
  std::array<RenderVec4, kMaxClippedPolygonVertices> back{};
  std::size_t count = std::min(clip_polygon.size(), front.size());
  std::copy_n(clip_polygon.begin(), count, front.begin());

  const float near_depth = near_depth_;
  count = ClipPolygonAgainstPlane(
      front, count, back,
      [near_depth](const RenderVec4& v) { return v[3] - near_depth; });
  std::swap(front, back);
  if (count < 3u) {
    return;
  }

  const std::array<float (*)(const RenderVec4&), 4> side_planes{
      [](const RenderVec4& v) { return v[0] + v[3]; },
      [](const RenderVec4& v) { return v[3] - v[0]; },
      [](const RenderVec4& v) { return v[1] + v[3]; },
      [](const RenderVec4& v) { return v[3] - v[1]; },
  };
  for (const auto& plane : side_planes) {
    count = ClipPolygonAgainstPlane(front, count, back, plane);
    std::swap(front, back);
    if (count < 3u) {
      return;
    }
  }

  std::array<ScreenVertex, kMaxClippedPolygonVertices> screen{};
  for (std::size_t index = 0; index < count; ++index) {
    const RenderVec4& clip = front[index];
    if (!(clip[3] > 0.0f)) {
      return;
    }
    const float inverse_w = 1.0f / clip[3];
    screen[index].x = (clip[0] * inverse_w * 0.5f + 0.5f) *
                      static_cast<float>(kOcclusionBufferWidth);

    screen[index].y = (0.5f - clip[1] * inverse_w * 0.5f) *
                      static_cast<float>(kOcclusionBufferHeight);
    screen[index].inverse_depth = inverse_w;
    if (!std::isfinite(screen[index].x) || !std::isfinite(screen[index].y)) {
      return;
    }
  }

  RasterizeScreenPolygon(std::span<const ScreenVertex>{screen.data(), count});
}

void OcclusionDepthBuffer::RasterizeScreenPolygon(
    const std::span<const ScreenVertex> screen_polygon) {
  const std::size_t count = screen_polygon.size();
  if (count < 3u || count > kMaxClippedPolygonVertices) {
    return;
  }

  float area2 = 0.0f;
  for (std::size_t index = 0; index < count; ++index) {
    const ScreenVertex& current = screen_polygon[index];
    const ScreenVertex& next = screen_polygon[(index + 1u) % count];
    area2 += current.x * next.y - next.x * current.y;
  }
  const float orientation = area2 >= 0.0f ? 1.0f : -1.0f;
  area2 = std::abs(area2);
  if (!(area2 >= kMinimumOccluderScreenArea2)) {
    return;
  }

  float min_x_f = screen_polygon[0].x;
  float max_x_f = screen_polygon[0].x;
  float min_y_f = screen_polygon[0].y;
  float max_y_f = screen_polygon[0].y;
  for (const ScreenVertex& vertex : screen_polygon) {
    min_x_f = std::min(min_x_f, vertex.x);
    max_x_f = std::max(max_x_f, vertex.x);
    min_y_f = std::min(min_y_f, vertex.y);
    max_y_f = std::max(max_y_f, vertex.y);
  }
  const int min_x = std::max(0, static_cast<int>(std::floor(min_x_f)));
  const int max_x = std::min(kOcclusionBufferWidth - 1,
                             static_cast<int>(std::ceil(max_x_f)));
  const int min_y = std::max(0, static_cast<int>(std::floor(min_y_f)));
  const int max_y = std::min(kOcclusionBufferHeight - 1,
                             static_cast<int>(std::ceil(max_y_f)));
  if (min_x > max_x || min_y > max_y) {
    return;
  }

  std::array<Edge, kMaxClippedPolygonVertices> edges{};
  for (std::size_t index = 0; index < count; ++index) {
    const ScreenVertex& from = screen_polygon[index];
    const ScreenVertex& to = screen_polygon[(index + 1u) % count];
    Edge& edge = edges[index];
    edge.a = -(to.y - from.y) * orientation;
    edge.b = (to.x - from.x) * orientation;
    edge.c = ((to.y - from.y) * from.x - (to.x - from.x) * from.y) * orientation;
    edge.c -= (0.5f + kEdgeConservativeInsetTexels) *
              (std::abs(edge.a) + std::abs(edge.b));
  }

  std::size_t pivot = 1u;
  float pivot_area2 = 0.0f;
  for (std::size_t index = 2; index < count; ++index) {
    const float candidate = std::abs(
        (screen_polygon[index - 1u].x - screen_polygon[0].x) *
            (screen_polygon[index].y - screen_polygon[0].y) -
        (screen_polygon[index - 1u].y - screen_polygon[0].y) *
            (screen_polygon[index].x - screen_polygon[0].x));
    if (candidate > pivot_area2) {
      pivot_area2 = candidate;
      pivot = index;
    }
  }
  if (!(pivot_area2 > 0.0f)) {
    return;
  }
  const ScreenVertex& base = screen_polygon[0];
  const ScreenVertex& first = screen_polygon[pivot - 1u];
  const ScreenVertex& second = screen_polygon[pivot];
  const float delta_x1 = first.x - base.x;
  const float delta_y1 = first.y - base.y;
  const float delta_x2 = second.x - base.x;
  const float delta_y2 = second.y - base.y;
  const float delta_depth1 = first.inverse_depth - base.inverse_depth;
  const float delta_depth2 = second.inverse_depth - base.inverse_depth;
  const float determinant = delta_x1 * delta_y2 - delta_y1 * delta_x2;
  const float gradient_x =
      (delta_depth1 * delta_y2 - delta_depth2 * delta_y1) / determinant;
  const float gradient_y =
      (delta_x1 * delta_depth2 - delta_x2 * delta_depth1) / determinant;
  if (!std::isfinite(gradient_x) || !std::isfinite(gradient_y)) {
    return;
  }
  const float depth_at_origin =
      base.inverse_depth - gradient_x * base.x - gradient_y * base.y;
  const float texel_depth_slack =
      0.5f * (std::abs(gradient_x) + std::abs(gradient_y));

  const auto is_inside = [&edges, count](const float sample_x,
                                         const float sample_y) {
    for (std::size_t index = 0; index < count; ++index) {
      if (edges[index].a * sample_x + edges[index].b * sample_y +
              edges[index].c <
          0.0f) {
        return false;
      }
    }
    return true;
  };

  for (int y = min_y; y <= max_y; ++y) {
    const float sample_y = static_cast<float>(y) + 0.5f;
    const float row_depth = depth_at_origin + gradient_y * sample_y;
    const std::size_t row_offset =
        static_cast<std::size_t>(y) *
        static_cast<std::size_t>(kOcclusionBufferWidth);

    float span_low = static_cast<float>(min_x);
    float span_high = static_cast<float>(max_x);
    bool row_empty = false;
    for (std::size_t index = 0; index < count; ++index) {
      const Edge& edge = edges[index];
      const float rest = edge.b * sample_y + edge.c;
      if (edge.a > 0.0f) {
        span_low = std::max(span_low, -rest / edge.a - 0.5f);
      } else if (edge.a < 0.0f) {
        span_high = std::min(span_high, -rest / edge.a - 0.5f);
      } else if (rest < 0.0f) {
        row_empty = true;
        break;
      }
    }
    if (row_empty) {
      continue;
    }
    int begin_x = std::max(min_x, static_cast<int>(std::ceil(span_low)));
    int end_x = std::min(max_x, static_cast<int>(std::floor(span_high)));
    while (begin_x <= end_x &&
           !is_inside(static_cast<float>(begin_x) + 0.5f, sample_y)) {
      ++begin_x;
    }
    while (end_x >= begin_x &&
           !is_inside(static_cast<float>(end_x) + 0.5f, sample_y)) {
      --end_x;
    }

    for (int x = begin_x; x <= end_x; ++x) {
      const float sample_x = static_cast<float>(x) + 0.5f;
      const float depth = gradient_x * sample_x + row_depth;

      const float texel_depth =
          (depth - texel_depth_slack) *
          (1.0f - kDepthConservativeRelativeMargin);
      if (!(texel_depth > 0.0f)) {
        continue;
      }
      float& stored = nearest_inverse_depth_[row_offset +
                                             static_cast<std::size_t>(x)];
      if (texel_depth > stored) {
        stored = texel_depth;
        tile_dirty_[static_cast<std::size_t>(y / kOcclusionTileSize) *
                        static_cast<std::size_t>(kOcclusionTilesX) +
                    static_cast<std::size_t>(x / kOcclusionTileSize)] = 1u;
      }
    }
  }
}

float OcclusionDepthBuffer::CoveredTexelFraction() const noexcept {
  if (nearest_inverse_depth_.empty()) {
    return 0.0f;
  }
  std::size_t covered = 0u;
  for (const float depth : nearest_inverse_depth_) {
    covered += depth > 0.0f ? 1u : 0u;
  }
  return static_cast<float>(covered) /
         static_cast<float>(nearest_inverse_depth_.size());
}

float OcclusionDepthBuffer::TexelInverseDepth(const int x,
                                             const int y) const noexcept {
  if (x < 0 || y < 0 || x >= kOcclusionBufferWidth ||
      y >= kOcclusionBufferHeight || nearest_inverse_depth_.empty()) {
    return 0.0f;
  }
  return nearest_inverse_depth_[static_cast<std::size_t>(y) *
                                    static_cast<std::size_t>(
                                        kOcclusionBufferWidth) +
                                static_cast<std::size_t>(x)];
}

float OcclusionDepthBuffer::TileMinimum(const int tile_x,
                                        const int tile_y) const {
  const std::size_t tile_index =
      static_cast<std::size_t>(tile_y) *
          static_cast<std::size_t>(kOcclusionTilesX) +
      static_cast<std::size_t>(tile_x);
  if (tile_dirty_[tile_index] != 0u) {
    float minimum = nearest_inverse_depth_
        [static_cast<std::size_t>(tile_y * kOcclusionTileSize) *
             static_cast<std::size_t>(kOcclusionBufferWidth) +
         static_cast<std::size_t>(tile_x * kOcclusionTileSize)];
    for (int y = 0; y < kOcclusionTileSize; ++y) {
      const std::size_t row_offset =
          static_cast<std::size_t>(tile_y * kOcclusionTileSize + y) *
          static_cast<std::size_t>(kOcclusionBufferWidth);
      for (int x = 0; x < kOcclusionTileSize; ++x) {
        minimum = std::min(
            minimum,
            nearest_inverse_depth_[row_offset +
                                   static_cast<std::size_t>(
                                       tile_x * kOcclusionTileSize + x)]);
      }
    }
    tile_minimum_[tile_index] = minimum;
    tile_dirty_[tile_index] = 0u;
  }
  return tile_minimum_[tile_index];
}

bool OcclusionDepthBuffer::IsRectOccluded(
    const int min_x, const int min_y, const int max_x, const int max_y,
    const float nearest_inverse_depth) const {
  for (int tile_y = min_y / kOcclusionTileSize;
       tile_y <= max_y / kOcclusionTileSize; ++tile_y) {
    const int tile_min_y = tile_y * kOcclusionTileSize;
    const int tile_max_y = tile_min_y + kOcclusionTileSize - 1;
    const bool rows_fully_covered = min_y <= tile_min_y && max_y >= tile_max_y;
    for (int tile_x = min_x / kOcclusionTileSize;
         tile_x <= max_x / kOcclusionTileSize; ++tile_x) {
      const int tile_min_x = tile_x * kOcclusionTileSize;
      const int tile_max_x = tile_min_x + kOcclusionTileSize - 1;
      if (rows_fully_covered && min_x <= tile_min_x && max_x >= tile_max_x) {

        if (!(nearest_inverse_depth < TileMinimum(tile_x, tile_y))) {
          return false;
        }
        continue;
      }
      const int begin_y = std::max(min_y, tile_min_y);
      const int end_y = std::min(max_y, tile_max_y);
      const int begin_x = std::max(min_x, tile_min_x);
      const int end_x = std::min(max_x, tile_max_x);
      for (int y = begin_y; y <= end_y; ++y) {
        const std::size_t row_offset =
            static_cast<std::size_t>(y) *
            static_cast<std::size_t>(kOcclusionBufferWidth);
        for (int x = begin_x; x <= end_x; ++x) {
          if (!(nearest_inverse_depth <
                nearest_inverse_depth_[row_offset +
                                       static_cast<std::size_t>(x)])) {
            return false;
          }
        }
      }
    }
  }
  return true;
}

bool OcclusionDepthBuffer::IsBoundsOccluded(
    const RenderVec3View bounds_min, const RenderVec3View bounds_max,
    const RenderMatrix4x4View model_view_projection) const {
  if (!active_ || rasterized_polygon_count_ == 0u) {
    return false;
  }
  ++query_count_;

  float min_x = std::numeric_limits<float>::max();
  float max_x = std::numeric_limits<float>::lowest();
  float min_y = std::numeric_limits<float>::max();
  float max_y = std::numeric_limits<float>::lowest();
  float min_depth = std::numeric_limits<float>::max();
  for (int corner = 0; corner < 8; ++corner) {
    const std::array<float, 4> local{
        (corner & 1) != 0 ? bounds_max[0] : bounds_min[0],
        (corner & 2) != 0 ? bounds_max[1] : bounds_min[1],
        (corner & 4) != 0 ? bounds_max[2] : bounds_min[2], 1.0f};
    const RenderVec4 clip =
        TransformRowVector4x4(RenderVec4View{local}, model_view_projection);
    if (!IsFinite(clip)) {
      return false;
    }

    if (!(clip[3] >= near_depth_)) {
      return false;
    }
    const float inverse_w = 1.0f / clip[3];
    const float screen_x = (clip[0] * inverse_w * 0.5f + 0.5f) *
                           static_cast<float>(kOcclusionBufferWidth);
    const float screen_y = (0.5f - clip[1] * inverse_w * 0.5f) *
                           static_cast<float>(kOcclusionBufferHeight);
    if (!std::isfinite(screen_x) || !std::isfinite(screen_y)) {
      return false;
    }
    min_x = std::min(min_x, screen_x);
    max_x = std::max(max_x, screen_x);
    min_y = std::min(min_y, screen_y);
    max_y = std::max(max_y, screen_y);
    min_depth = std::min(min_depth, clip[3]);
  }

  const int rect_min_x =
      std::max(0, static_cast<int>(std::floor(min_x)) - 1);
  const int rect_max_x = std::min(kOcclusionBufferWidth - 1,
                                  static_cast<int>(std::ceil(max_x)) + 1);
  const int rect_min_y =
      std::max(0, static_cast<int>(std::floor(min_y)) - 1);
  const int rect_max_y = std::min(kOcclusionBufferHeight - 1,
                                  static_cast<int>(std::ceil(max_y)) + 1);
  if (rect_min_x > rect_max_x || rect_min_y > rect_max_y) {

    return false;
  }

  const float nearest_inverse_depth =
      (1.0f / min_depth) * (1.0f + kDepthConservativeRelativeMargin);
  const bool occluded = IsRectOccluded(rect_min_x, rect_min_y, rect_max_x,
                                       rect_max_y, nearest_inverse_depth);
  if (occluded) {
    ++occluded_query_count_;
  }
  return occluded;
}

}

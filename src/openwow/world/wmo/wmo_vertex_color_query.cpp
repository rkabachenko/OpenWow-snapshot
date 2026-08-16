#include "openwow/world/wmo/wmo_vertex_color_query.h"

#include "openwow/foundation/math/get_dominant_axis.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace openwow::world {
namespace {

[[nodiscard]] data::wmo::WmoVertexColor UnpackBgra(
    const std::uint32_t packed) noexcept {
  return data::wmo::WmoVertexColor{
      .b = static_cast<std::uint8_t>(packed),
      .g = static_cast<std::uint8_t>(packed >> 8u),
      .r = static_cast<std::uint8_t>(packed >> 16u),
      .a = static_cast<std::uint8_t>(packed >> 24u),
  };
}

[[nodiscard]] std::optional<std::int32_t> RetailFixedWeight(
    const float weight) noexcept {

  const float rounded = std::nearbyint(weight * 256.0f - 0.5f);
  if (!std::isfinite(rounded) ||
      static_cast<double>(rounded) <
          static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
      static_cast<double>(rounded) >
          static_cast<double>(std::numeric_limits<std::int32_t>::max())) {
    return std::nullopt;
  }
  return static_cast<std::int32_t>(rounded);
}

[[nodiscard]] std::uint8_t PreparedChannel(const std::uint8_t channel,
                                           const std::uint8_t alpha) noexcept {
  const std::uint32_t prepared =
      (((static_cast<std::uint32_t>(channel) * alpha) >> 6u) + channel) >> 1u;
  return static_cast<std::uint8_t>(std::min(prepared, 0xffu));
}

[[nodiscard]] float Dot(const data::wmo::Vec3f& a,
                        const data::wmo::Vec3f& b) noexcept {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] data::wmo::Vec3f Subtract(const data::wmo::Vec3f& a,
                                        const data::wmo::Vec3f& b) noexcept {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}

[[nodiscard]] data::wmo::Vec3f Cross(const data::wmo::Vec3f& a,
                                     const data::wmo::Vec3f& b) noexcept {
  return {a.y * b.z - a.z * b.y,
          a.z * b.x - a.x * b.z,
          a.x * b.y - a.y * b.x};
}

[[nodiscard]] float LengthSquared(const data::wmo::Vec3f& value) noexcept {
  return Dot(value, value);
}

[[nodiscard]] float DistanceToSegment(const data::wmo::Vec3f& point,
                                      const data::wmo::Vec3f& a,
                                      const data::wmo::Vec3f& b) noexcept {
  const data::wmo::Vec3f edge = Subtract(b, a);
  const float edge_length_squared = LengthSquared(edge);
  if (edge_length_squared <= 1.0e-12f) {
    return std::sqrt(LengthSquared(Subtract(point, a)));
  }
  const float t = std::clamp(Dot(Subtract(point, a), edge) /
                                 edge_length_squared,
                             0.0f, 1.0f);
  const data::wmo::Vec3f closest{a.x + edge.x * t, a.y + edge.y * t,
                                a.z + edge.z * t};
  return std::sqrt(LengthSquared(Subtract(point, closest)));
}

[[nodiscard]] bool PointInsidePortal(
    const data::wmo::Vec3f& point, const data::wmo::Vec3f& normal,
    const std::span<const data::wmo::Vec3f> vertices) noexcept {
  if (vertices.size() < 3u) {
    return false;
  }
  float winding_sign = 0.0f;
  for (std::size_t i = 0u; i < vertices.size(); ++i) {
    const auto& a = vertices[i];
    const auto& b = vertices[(i + 1u) % vertices.size()];
    const float side = Dot(Cross(Subtract(b, a), Subtract(point, a)), normal);
    if (std::abs(side) <= 1.0e-5f) {
      continue;
    }
    if (winding_sign == 0.0f) {
      winding_sign = side;
    } else if ((side < 0.0f) != (winding_sign < 0.0f)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] float SignedDistanceToPortal(
    const data::wmo::Vec3f& point, const data::wmo::WmoPortal& portal,
    const std::span<const data::wmo::Vec3f> vertices,
    const std::int16_t side) noexcept {
  const data::wmo::Vec3f normal{portal.normal[0], portal.normal[1],
                               portal.normal[2]};
  const float normal_length = std::sqrt(LengthSquared(normal));
  if (normal_length <= 1.0e-12f || vertices.empty()) {
    return std::numeric_limits<float>::infinity();
  }
  const float plane_distance =
      (Dot(point, normal) + portal.distance) / normal_length;
  const data::wmo::Vec3f projected{
      point.x - normal.x * plane_distance / normal_length,
      point.y - normal.y * plane_distance / normal_length,
      point.z - normal.z * plane_distance / normal_length};
  float magnitude = std::abs(plane_distance);
  if (!PointInsidePortal(projected, normal, vertices)) {
    float edge_distance = std::numeric_limits<float>::infinity();
    for (std::size_t i = 0u; i < vertices.size(); ++i) {
      edge_distance = std::min(
          edge_distance,
          DistanceToSegment(point, vertices[i],
                            vertices[(i + 1u) % vertices.size()]));
    }
    magnitude = edge_distance;
  }
  const float oriented = side == 1 ? plane_distance : -plane_distance;
  return std::signbit(oriented) ? -magnitude : magnitude;
}

}

WmoVertexColorPreparation BuildWmoVertexColorPreparation(
    const std::span<const data::wmo::WmoRenderBatch> batches,
    const std::uint16_t transparent_batch_count,
    const std::uint32_t mohd_flags) noexcept {
  WmoVertexColorPreparation result{};

  result.enabled = (mohd_flags & data::wmo::kWmoFlagHasVertexColor) == 0u;
  if (!result.enabled || transparent_batch_count == 0u || batches.empty()) {
    return result;
  }

  const std::size_t last_transparent_batch =
      std::min<std::size_t>(transparent_batch_count, batches.size()) - 1u;
  result.transparent_vertex_count =
      static_cast<std::size_t>(batches[last_transparent_batch].lastVertex) + 1u;
  return result;
}

data::wmo::WmoVertexColor PrepareWmoVertexColor(
    const data::wmo::WmoVertexColor &source, const std::size_t vertex_index,
    const WmoVertexColorPreparation &preparation) noexcept {
  if (!preparation.enabled) {
    return source;
  }

  data::wmo::WmoVertexColor result = source;
  if (vertex_index < preparation.transparent_vertex_count) {
    result.b >>= 1u;
    result.g >>= 1u;
    result.r >>= 1u;
    return result;
  }

  result.b = PreparedChannel(source.b, source.a);
  result.g = PreparedChannel(source.g, source.a);
  result.r = PreparedChannel(source.r, source.a);
  result.a = 0xffu;
  return result;
}

data::wmo::WmoVertexColor PrepareWmoPortalVertexColor(
    const data::wmo::WmoRoot& root, const data::wmo::WmoGroup& group,
    const data::wmo::Vec3f& position,
    const data::wmo::WmoVertexColor& prepared_color,
    const std::size_t vertex_index,
    const WmoVertexColorPreparation& preparation) noexcept {
  if ((root.header.flags & data::wmo::kWmoFlagNoPortalAtten) != 0u ||
      vertex_index >= preparation.transparent_vertex_count) {
    return prepared_color;
  }

  float portal_weight = 0.0f;
  const std::size_t ref_begin = group.header.portalStart;
  const std::size_t ref_end = std::min(
      root.portalRefs.size(), ref_begin + group.header.portalCount);
  for (std::size_t ref_index = ref_begin; ref_index < ref_end; ++ref_index) {
    const auto& ref = root.portalRefs[ref_index];
    if (ref.portalIndex >= root.portals.size()) {
      continue;
    }
    const auto& portal = root.portals[ref.portalIndex];
    const std::size_t vertex_begin = portal.startVertex;
    const std::size_t vertex_count = portal.nVertices;
    if (vertex_begin > root.portalVertices.size() ||
        vertex_count > root.portalVertices.size() - vertex_begin) {
      continue;
    }
    const float distance = SignedDistanceToPortal(
        position, portal,
        std::span<const data::wmo::Vec3f>(root.portalVertices)
            .subspan(vertex_begin, vertex_count),
        ref.side);
    const std::uint32_t adjacent_flags =
        ref.groupIndex < root.groupInfos.size()
            ? root.groupInfos[ref.groupIndex].flags
            : 0u;
    if ((adjacent_flags & 0x48u) == 0u) {
      if (distance > -1.0f && distance < 1.0f) {
        portal_weight = 0.0f;
        break;
      }
      continue;
    }
    const float contribution =
        1.0f - std::max(distance, 0.0f) / 6.6666665f;
    if (contribution > 0.001f) {
      portal_weight += contribution;
    }
  }
  portal_weight = std::clamp(portal_weight, 0.0f, 1.0f);
  const auto blend_channel = [portal_weight](const std::uint8_t value) {
    return static_cast<std::uint8_t>(std::clamp(
        std::lround(static_cast<float>(value) +
                    (127.0f - static_cast<float>(value)) * portal_weight),
        0l, 255l));
  };
  return {
      .b = blend_channel(prepared_color.b),
      .g = blend_channel(prepared_color.g),
      .r = blend_channel(prepared_color.r),
      .a = static_cast<std::uint8_t>(std::clamp(
          std::lround(portal_weight * 255.0f), 0l, 255l)),
  };
}

std::optional<WmoVertexColorQueryResult> InterpolateWmoVertexColor(
    const WmoVertexColorQueryView &view,
    const std::array<float, 3> &local_position,
    const std::uint16_t triangle_index) noexcept {
  const std::size_t triangle = triangle_index;
  if (triangle >= view.triangle_materials.size()) {
    return WmoVertexColorQueryResult{
        .color = UnpackBgra(view.ambient_color),
        .outdoor = false,
    };
  }

  const std::size_t first_index = triangle * 3u;
  if (first_index + 2u >= view.indices.size()) {
    return std::nullopt;
  }
  const std::array<std::uint16_t, 3> vertex_indices{
      view.indices[first_index], view.indices[first_index + 1u],
      view.indices[first_index + 2u]};
  for (const std::uint16_t vertex_index : vertex_indices) {
    if (vertex_index >= view.vertices.size() ||
        vertex_index >= view.vertex_colors.size()) {
      return std::nullopt;
    }
  }

  const data::wmo::Vec3f &v0 = view.vertices[vertex_indices[0]];
  const data::wmo::Vec3f &v1 = view.vertices[vertex_indices[1]];
  const data::wmo::Vec3f &v2 = view.vertices[vertex_indices[2]];
  const std::array<float, 3> p0{v0.x, v0.y, v0.z};
  const std::array<float, 3> p1{v1.x, v1.y, v1.z};
  const std::array<float, 3> p2{v2.x, v2.y, v2.z};
  const float normal[3]{
      (p2[2] - p0[2]) * (p1[1] - p0[1]) -
          (p2[1] - p0[1]) * (p1[2] - p0[2]),
      (p2[0] - p0[0]) * (p1[2] - p0[2]) -
          (p1[0] - p0[0]) * (p2[2] - p0[2]),
      (p2[1] - p0[1]) * (p1[0] - p0[0]) -
          (p2[0] - p0[0]) * (p1[1] - p0[1]),
  };
  const int dominant_axis = openwow::math::GetDominantAxis(normal);
  constexpr int kProjectionAxes[3][2]{{1, 2}, {2, 0}, {0, 1}};
  const int axis_a = kProjectionAxes[dominant_axis][0];
  const int axis_b = kProjectionAxes[dominant_axis][1];

  const float query_a = local_position[axis_a] - p0[axis_a];
  const float query_b = local_position[axis_b] - p0[axis_b];
  const float v1_a = p1[axis_a] - p0[axis_a];
  const float v1_b = p1[axis_b] - p0[axis_b];
  const float v2_a = p2[axis_a] - p0[axis_a];
  const float v2_b = p2[axis_b] - p0[axis_b];
  const float denominator = v2_b * v1_a - v2_a * v1_b;
  if (denominator == 0.0f || !std::isfinite(denominator)) {
    return std::nullopt;
  }
  const float inverse_denominator = 1.0f / denominator;
  const auto fixed_v1 = RetailFixedWeight(
      (v2_b * query_a - v2_a * query_b) * inverse_denominator);
  const auto fixed_v2 = RetailFixedWeight(
      (query_b * v1_a - query_a * v1_b) * inverse_denominator);
  if (!fixed_v1.has_value() || !fixed_v2.has_value()) {
    return std::nullopt;
  }

  std::int32_t weight_v1 = *fixed_v1;
  std::int32_t weight_v2 = *fixed_v2;
  std::int32_t weight_v0 = 0x100 - weight_v1 - weight_v2;

  if (weight_v1 < 0) {
    const std::int32_t denominator_v1 = weight_v2 + weight_v0;
    if (denominator_v1 == 0) {
      return std::nullopt;
    }
    const std::int32_t transfer = static_cast<std::int32_t>(
        (static_cast<std::int64_t>(weight_v1) * weight_v2) /
        denominator_v1);
    const std::int32_t remaining = weight_v1 - transfer;
    weight_v1 = 0;
    weight_v2 += transfer;
    weight_v0 += remaining;
  }
  if (weight_v2 < 0) {
    const std::int32_t denominator_v2 = weight_v1 + weight_v0;
    if (denominator_v2 == 0) {
      return std::nullopt;
    }
    const std::int32_t transfer = static_cast<std::int32_t>(
        (static_cast<std::int64_t>(weight_v1) * weight_v2) /
        denominator_v2);
    weight_v1 += transfer;
    weight_v0 += weight_v2 - transfer;
    weight_v2 = 0;
  }
  if (weight_v0 < 0) {
    const std::int32_t denominator_v0 = weight_v1 + weight_v2;
    if (denominator_v0 == 0) {
      return std::nullopt;
    }
    const std::int32_t transfer = static_cast<std::int32_t>(
        (static_cast<std::int64_t>(weight_v1) * weight_v0) /
        denominator_v0);
    weight_v1 += transfer;
    weight_v2 += weight_v0 - transfer;
    weight_v0 = 0;
  }

  const data::wmo::WmoVertexColor c0 = PrepareWmoVertexColor(
      view.vertex_colors[vertex_indices[0]], vertex_indices[0],
      view.preparation);
  const data::wmo::WmoVertexColor c1 = PrepareWmoVertexColor(
      view.vertex_colors[vertex_indices[1]], vertex_indices[1],
      view.preparation);
  const data::wmo::WmoVertexColor c2 = PrepareWmoVertexColor(
      view.vertex_colors[vertex_indices[2]], vertex_indices[2],
      view.preparation);
  const auto interpolate = [&](const std::uint8_t channel0,
                               const std::uint8_t channel1,
                               const std::uint8_t channel2) {
    return static_cast<std::uint8_t>(
        (static_cast<std::uint32_t>(channel1) * weight_v1 +
         static_cast<std::uint32_t>(channel2) * weight_v2 +
         static_cast<std::uint32_t>(channel0) * weight_v0) >>
        8u);
  };

  WmoVertexColorQueryResult result{
      .color = data::wmo::WmoVertexColor{
          .b = interpolate(c0.b, c1.b, c2.b),
          .g = interpolate(c0.g, c1.g, c2.g),
          .r = interpolate(c0.r, c1.r, c2.r),
          .a = interpolate(c0.a, c1.a, c2.a),
      },
      .outdoor = (view.triangle_materials[triangle].flags & 1u) != 0u,
  };

  const data::wmo::WmoVertexColor ambient = UnpackBgra(view.ambient_color);
  const bool add_ambient =
      (view.mohd_flags & data::wmo::kWmoFlagUnifiedRender) != 0u;
  result.color.b = static_cast<std::uint8_t>(std::min(
      static_cast<std::uint32_t>(result.color.b) * 2u +
          (add_ambient ? ambient.b : 0u),
      0xffu));
  result.color.g = static_cast<std::uint8_t>(std::min(
      static_cast<std::uint32_t>(result.color.g) * 2u +
          (add_ambient ? ambient.g : 0u),
      0xffu));
  result.color.r = static_cast<std::uint8_t>(std::min(
      static_cast<std::uint32_t>(result.color.r) * 2u +
          (add_ambient ? ambient.r : 0u),
      0xffu));
  return result;
}

std::optional<WmoVertexColorQueryResult> InterpolateWmoGroupVertexColor(
    const data::wmo::WmoRoot &root, const data::wmo::WmoGroup &group,
    const std::array<float, 3> &local_position,
    const std::uint16_t triangle_index) noexcept {
  return InterpolateWmoVertexColor(
      WmoVertexColorQueryView{
          .triangle_materials = group.triangleMaterials,
          .indices = group.indices,
          .vertices = group.vertices,
          .vertex_colors = group.vertexColors,
          .preparation = BuildWmoVertexColorPreparation(
              group.renderBatches, group.header.transBatchCount,
              root.header.flags),
          .mohd_flags = root.header.flags,
          .ambient_color = root.header.ambientColor,
      },
      local_position, triangle_index);
}

}

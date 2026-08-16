#include "openwow/render/scene/occlusion/occluder_polygon_builder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace openwow::render::occlusion {

namespace {

struct SourceFace {
  std::array<RenderVec3, 3> positions{};
  RenderVec3 unit_normal{};
  float area{0.0f};
};

struct PlaneKey {
  std::int32_t normal_x{0};
  std::int32_t normal_y{0};
  std::int32_t normal_z{0};
  std::int32_t offset{0};

  [[nodiscard]] bool operator==(const PlaneKey&) const noexcept = default;
};

struct PlaneKeyHash {
  [[nodiscard]] std::size_t operator()(const PlaneKey& key) const noexcept {
    std::size_t hash = 1469598103934665603ull;
    for (const std::int32_t component :
         {key.normal_x, key.normal_y, key.normal_z, key.offset}) {
      hash =
          (hash ^ static_cast<std::size_t>(
                      static_cast<std::uint32_t>(component))) *
          1099511628211ull;
    }
    return hash;
  }
};

constexpr float kNormalQuantum = 1024.0f;

constexpr float kOffsetQuantum = 64.0f;

constexpr float kHullAreaTolerance = 1.0e-6f;

constexpr float kCoplanarityTolerance = 1.0e-4f;

[[nodiscard]] RenderVec3 Subtract(const RenderVec3& lhs,
                                  const RenderVec3& rhs) noexcept {
  return {lhs[0] - rhs[0], lhs[1] - rhs[1], lhs[2] - rhs[2]};
}

[[nodiscard]] RenderVec3 Cross(const RenderVec3& lhs,
                               const RenderVec3& rhs) noexcept {
  return {lhs[1] * rhs[2] - lhs[2] * rhs[1], lhs[2] * rhs[0] - lhs[0] * rhs[2],
          lhs[0] * rhs[1] - lhs[1] * rhs[0]};
}

[[nodiscard]] float Dot(const RenderVec3& lhs, const RenderVec3& rhs) noexcept {
  return lhs[0] * rhs[0] + lhs[1] * rhs[1] + lhs[2] * rhs[2];
}

[[nodiscard]] RenderVec3 ReadVec3(const std::byte* const base,
                                  const std::size_t offset) noexcept {
  std::array<float, 3> value{};
  std::memcpy(value.data(), base + offset, sizeof(value));
  return {value[0], value[1], value[2]};
}

[[nodiscard]] std::vector<std::size_t> BuildPlanarConvexHull(
    const std::vector<std::array<float, 2>>& points) {
  std::vector<std::size_t> order(points.size());
  for (std::size_t index = 0; index < order.size(); ++index) {
    order[index] = index;
  }
  std::sort(order.begin(), order.end(),
            [&points](const std::size_t lhs, const std::size_t rhs) {
              if (points[lhs][0] != points[rhs][0]) {
                return points[lhs][0] < points[rhs][0];
              }
              return points[lhs][1] < points[rhs][1];
            });
  const auto cross = [&points](const std::size_t origin, const std::size_t a,
                               const std::size_t b) {
    return (points[a][0] - points[origin][0]) *
               (points[b][1] - points[origin][1]) -
           (points[a][1] - points[origin][1]) *
               (points[b][0] - points[origin][0]);
  };
  std::vector<std::size_t> hull;
  hull.reserve(order.size() * 2u);
  for (const std::size_t index : order) {
    while (hull.size() >= 2u &&
           cross(hull[hull.size() - 2u], hull[hull.size() - 1u], index) <=
               0.0f) {
      hull.pop_back();
    }
    hull.push_back(index);
  }
  const std::size_t lower_size = hull.size() + 1u;
  for (std::size_t reverse = order.size(); reverse-- > 0;) {
    const std::size_t index = order[reverse];
    while (hull.size() >= lower_size &&
           cross(hull[hull.size() - 2u], hull[hull.size() - 1u], index) <=
               0.0f) {
      hull.pop_back();
    }
    hull.push_back(index);
  }
  if (!hull.empty()) {
    hull.pop_back();
  }
  return hull;
}

}

float OccluderPolygonArea(const OccluderPolygon& polygon) noexcept {
  if (polygon.vertex_count < 3u) {
    return 0.0f;
  }

  float area = 0.0f;
  for (std::size_t index = 2; index < polygon.vertex_count; ++index) {
    const RenderVec3 edge0 =
        Subtract(polygon.positions[index - 1u], polygon.positions[0]);
    const RenderVec3 edge1 =
        Subtract(polygon.positions[index], polygon.positions[0]);
    const RenderVec3 normal = Cross(edge0, edge1);
    area += 0.5f * std::sqrt(Dot(normal, normal));
  }
  return area;
}

std::vector<OccluderPolygon> BuildOccluderPolygons(
    const OccluderSourceMesh& mesh, const std::size_t first_index,
    const std::size_t index_count, const float minimum_area) {
  std::vector<OccluderPolygon> polygons;
  if (mesh.vertex_data == nullptr || mesh.vertex_count == 0u ||
      mesh.vertex_stride == 0u || index_count < 3u ||
      first_index + index_count > mesh.indices.size()) {
    return polygons;
  }

  const auto vertex = [&mesh](const std::size_t index) {
    return mesh.vertex_data + index * mesh.vertex_stride;
  };

  std::unordered_map<PlaneKey, std::vector<SourceFace>, PlaneKeyHash> planes;
  const std::size_t end_index = first_index + index_count;
  for (std::size_t index = first_index; index + 2u < end_index; index += 3u) {
    const std::size_t i0 = mesh.indices[index];
    const std::size_t i1 = mesh.indices[index + 1u];
    const std::size_t i2 = mesh.indices[index + 2u];
    if (i0 >= mesh.vertex_count || i1 >= mesh.vertex_count ||
        i2 >= mesh.vertex_count) {
      continue;
    }
    SourceFace face{};
    face.positions = {ReadVec3(vertex(i0), mesh.position_offset),
                      ReadVec3(vertex(i1), mesh.position_offset),
                      ReadVec3(vertex(i2), mesh.position_offset)};
    const RenderVec3 winding_normal =
        Cross(Subtract(face.positions[1], face.positions[0]),
              Subtract(face.positions[2], face.positions[0]));
    const float winding_length = std::sqrt(Dot(winding_normal, winding_normal));
    if (!(winding_length > 0.0f) || !std::isfinite(winding_length)) {
      continue;
    }
    face.unit_normal = {winding_normal[0] / winding_length,
                        winding_normal[1] / winding_length,
                        winding_normal[2] / winding_length};
    face.area = 0.5f * winding_length;

    const RenderVec3 normal0 = ReadVec3(vertex(i0), mesh.normal_offset);
    const RenderVec3 normal1 = ReadVec3(vertex(i1), mesh.normal_offset);
    const RenderVec3 normal2 = ReadVec3(vertex(i2), mesh.normal_offset);
    const RenderVec3 shading_normal{normal0[0] + normal1[0] + normal2[0],
                                    normal0[1] + normal1[1] + normal2[1],
                                    normal0[2] + normal1[2] + normal2[2]};
    if (!(Dot(face.unit_normal, shading_normal) > 0.0f)) {
      continue;
    }

    const float offset = Dot(face.unit_normal, face.positions[0]);
    const PlaneKey key{
        static_cast<std::int32_t>(
            std::lround(face.unit_normal[0] * kNormalQuantum)),
        static_cast<std::int32_t>(
            std::lround(face.unit_normal[1] * kNormalQuantum)),
        static_cast<std::int32_t>(
            std::lround(face.unit_normal[2] * kNormalQuantum)),
        static_cast<std::int32_t>(std::lround(offset * kOffsetQuantum))};
    planes[key].push_back(face);
  }

  const auto emit_face = [&polygons, minimum_area](const SourceFace& face) {
    if (!(face.area >= minimum_area)) {
      return;
    }
    OccluderPolygon polygon{};
    polygon.positions[0] = face.positions[0];
    polygon.positions[1] = face.positions[1];
    polygon.positions[2] = face.positions[2];
    polygon.outward_normal = face.unit_normal;
    polygon.vertex_count = 3u;
    polygons.push_back(polygon);
  };

  std::vector<std::array<float, 2>> plane_points;
  std::vector<std::size_t> point_face;
  for (const auto& [key, faces] : planes) {
    static_cast<void>(key);
    if (faces.size() == 1u) {
      emit_face(faces.front());
      continue;
    }
    float summed_area = 0.0f;
    for (const SourceFace& face : faces) {
      summed_area += face.area;
    }
    if (!(summed_area >= minimum_area)) {
      continue;
    }

    const RenderVec3& normal = faces.front().unit_normal;
    RenderVec3 basis_u{1.0f, 0.0f, 0.0f};
    if (std::abs(normal[0]) > std::abs(normal[1])) {
      basis_u = {0.0f, 1.0f, 0.0f};
    }
    basis_u = Cross(normal, basis_u);
    const float basis_length = std::sqrt(Dot(basis_u, basis_u));
    if (!(basis_length > 0.0f)) {
      continue;
    }
    basis_u = {basis_u[0] / basis_length, basis_u[1] / basis_length,
               basis_u[2] / basis_length};
    const RenderVec3 basis_v = Cross(normal, basis_u);
    const RenderVec3& origin = faces.front().positions[0];

    plane_points.clear();
    point_face.clear();
    for (std::size_t face_index = 0; face_index < faces.size(); ++face_index) {
      for (const RenderVec3& position : faces[face_index].positions) {
        const RenderVec3 relative = Subtract(position, origin);
        plane_points.push_back(
            {Dot(relative, basis_u), Dot(relative, basis_v)});
        point_face.push_back(face_index);
      }
    }

    bool merged = false;
    const std::vector<std::size_t> hull = BuildPlanarConvexHull(plane_points);
    if (hull.size() >= 3u && hull.size() <= kMaxOccluderPolygonVertices) {
      float hull_area2 = 0.0f;
      for (std::size_t index = 0; index < hull.size(); ++index) {
        const auto& current = plane_points[hull[index]];
        const auto& next = plane_points[hull[(index + 1u) % hull.size()]];
        hull_area2 += current[0] * next[1] - next[0] * current[1];
      }
      const float hull_area = std::abs(hull_area2) * 0.5f;

      float maximum_deviation = 0.0f;
      for (const SourceFace& face : faces) {
        for (const RenderVec3& position : face.positions) {
          maximum_deviation = std::max(
              maximum_deviation,
              std::abs(Dot(normal, Subtract(position, origin))));
        }
      }
      if (hull_area > 0.0f && maximum_deviation <= kCoplanarityTolerance &&
          std::abs(hull_area - summed_area) <= kHullAreaTolerance * hull_area) {
        OccluderPolygon polygon{};
        polygon.outward_normal = normal;
        polygon.vertex_count = static_cast<std::uint8_t>(hull.size());
        for (std::size_t index = 0; index < hull.size(); ++index) {
          const std::size_t point = hull[index];
          polygon.positions[index] =
              faces[point_face[point]].positions[point % 3u];
        }
        polygons.push_back(polygon);
        merged = true;
      }
    }
    if (!merged) {
      for (const SourceFace& face : faces) {
        emit_face(face);
      }
    }
  }
  return polygons;
}

}

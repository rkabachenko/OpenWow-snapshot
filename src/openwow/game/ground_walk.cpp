#include "openwow/game/ground_walk.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "openwow/game/movement/retail_fall_kinematics.h"

namespace openwow::game {
namespace {

using Constants = MovementCollisionConstants;

constexpr std::size_t kMaximumPreparedFacets = 1u << 20;
constexpr float kFacetCacheGrowth = 1.0f / 6.0f;

[[nodiscard]] bool Finite(const float value) {
  return std::isfinite(value);
}

[[nodiscard]] bool Finite(const C3Vector& value) {
  return Finite(value.x) && Finite(value.y) && Finite(value.z);
}

template <std::size_t Size>
[[nodiscard]] bool Finite(const std::array<float, Size>& values) {
  return std::all_of(values.begin(), values.end(),
                     [](const float value) { return Finite(value); });
}

[[nodiscard]] C3Vector Add(const C3Vector& lhs, const C3Vector& rhs) {
  return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

[[nodiscard]] C3Vector Subtract(const C3Vector& lhs,
                                const C3Vector& rhs) {
  return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

[[nodiscard]] C3Vector Scale(const C3Vector& value, const float scalar) {
  return {value.x * scalar, value.y * scalar, value.z * scalar};
}

[[nodiscard]] float Dot(const C3Vector& lhs, const C3Vector& rhs) {
  return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

[[nodiscard]] C3Vector Cross(const C3Vector& lhs, const C3Vector& rhs) {
  return {lhs.y * rhs.z - lhs.z * rhs.y,
          lhs.z * rhs.x - lhs.x * rhs.z,
          lhs.x * rhs.y - lhs.y * rhs.x};
}

[[nodiscard]] float LengthSquared(const C3Vector& value) {
  return Dot(value, value);
}

[[nodiscard]] float Length(const C3Vector& value) {
  return std::sqrt(LengthSquared(value));
}

[[nodiscard]] C3Vector NormalizeOr(const C3Vector& value,
                                   const C3Vector& fallback) {
  const float length = Length(value);
  if (length < Constants::kTiny || !Finite(length)) {
    return fallback;
  }
  return Scale(value, 1.0f / length);
}

[[nodiscard]] C3Vector TransformVector(const std::array<float, 9>& matrix,
                                       const C3Vector& value) {
  return {matrix[0] * value.x + matrix[1] * value.y + matrix[2] * value.z,
          matrix[3] * value.x + matrix[4] * value.y + matrix[5] * value.z,
          matrix[6] * value.x + matrix[7] * value.y + matrix[8] * value.z};
}

[[nodiscard]] std::array<C3Vector, 9> BuildRetailHull(
    const MovementCollisionBody& body, float hull_height);

[[nodiscard]] std::array<C3Vector, 9> BuildRetailHull(
    const MovementCollisionBody& body) {
  return BuildRetailHull(body, body.height);
}

[[nodiscard]] CollisionAabb SweptWorldBounds(
    const MovementCollisionBody& body, const C3Vector& displacement) {
  std::array<C3Vector, 9> hull = BuildRetailHull(body);
  C3Vector delta = displacement;
  if (body.parent.has_value()) {
    for (C3Vector& vertex : hull) {
      vertex = body.parent->ToWorldPoint(vertex);
    }
    delta = body.parent->ToWorldVector(delta);
  }

  CollisionAabb bounds{.min = hull.front(), .max = hull.front()};
  for (const C3Vector& vertex : hull) {
    const C3Vector end = Add(vertex, delta);
    bounds.min.x = std::min({bounds.min.x, vertex.x, end.x});
    bounds.min.y = std::min({bounds.min.y, vertex.y, end.y});
    bounds.min.z = std::min({bounds.min.z, vertex.z, end.z});
    bounds.max.x = std::max({bounds.max.x, vertex.x, end.x});
    bounds.max.y = std::max({bounds.max.y, vertex.y, end.y});
    bounds.max.z = std::max({bounds.max.z, vertex.z, end.z});
  }
  if (body.mode == MovementCollisionMode::kFalling && delta.z < 0.0f) {

    const float lateral_recovery =
        -delta.z * Constants::kFallingLateralRecovery;
    bounds.min.x -= lateral_recovery;
    bounds.min.y -= lateral_recovery;
    bounds.max.x += lateral_recovery;
    bounds.max.y += lateral_recovery;
  }
  return bounds;
}

void Expand(CollisionAabb& bounds, const float amount) {
  bounds.min.x -= amount;
  bounds.min.y -= amount;
  bounds.min.z -= amount;
  bounds.max.x += amount;
  bounds.max.y += amount;
  bounds.max.z += amount;
}

[[nodiscard]] CollisionAabb Union(const CollisionAabb& lhs,
                                  const CollisionAabb& rhs) {
  return {
      .min = {std::min(lhs.min.x, rhs.min.x),
              std::min(lhs.min.y, rhs.min.y),
              std::min(lhs.min.z, rhs.min.z)},
      .max = {std::max(lhs.max.x, rhs.max.x),
              std::max(lhs.max.y, rhs.max.y),
              std::max(lhs.max.z, rhs.max.z)},
  };
}

[[nodiscard]] std::array<C3Vector, 9> BuildRetailHull(
    const MovementCollisionBody& body, const float hull_height) {
  const float x = body.position.x;
  const float y = body.position.y;
  const float z = body.position.z;
  const float radius = body.radius;
  const float lower_z = z + radius * Constants::kLowerRingScale;
  const float upper_z = z + hull_height;
  return {{{x, y, z},
           {x - radius, y - radius, lower_z},
           {x - radius, y + radius, lower_z},
           {x + radius, y + radius, lower_z},
           {x + radius, y - radius, lower_z},
           {x - radius, y - radius, upper_z},
           {x - radius, y + radius, upper_z},
           {x + radius, y + radius, upper_z},
           {x + radius, y - radius, upper_z}}};
}

constexpr std::array<std::array<std::uint8_t, 2>, 16> kHullEdges{{
    {{0, 1}}, {{0, 2}}, {{0, 3}}, {{0, 4}},
    {{1, 2}}, {{2, 3}}, {{3, 4}}, {{4, 1}},
    {{1, 5}}, {{2, 6}}, {{3, 7}}, {{4, 8}},
    {{5, 6}}, {{6, 7}}, {{7, 8}}, {{8, 5}},
}};

constexpr std::array<C3Vector, 9> kHullFaceNormals{{
    {-1.0f, 0.0f, 0.0f},
    {1.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f},
    {0.0f, -1.0f, 0.0f},
    {0.0f, 0.0f, 1.0f},
    {-Constants::kLowerPlaneHorizontal, 0.0f,
     -Constants::kLowerPlaneVertical},
    {Constants::kLowerPlaneHorizontal, 0.0f,
     -Constants::kLowerPlaneVertical},
    {0.0f, Constants::kLowerPlaneHorizontal,
     -Constants::kLowerPlaneVertical},
    {0.0f, -Constants::kLowerPlaneHorizontal,
     -Constants::kLowerPlaneVertical},
}};

[[nodiscard]] bool AabbOverlaps(const CollisionAabb& lhs,
                                const CollisionAabb& rhs) {
  return lhs.min.x <= rhs.max.x && lhs.max.x >= rhs.min.x &&
         lhs.min.y <= rhs.max.y && lhs.max.y >= rhs.min.y &&
         lhs.min.z <= rhs.max.z && lhs.max.z >= rhs.min.z;
}

[[nodiscard]] CollisionAabb FacetBounds(
    const MovementCollisionFacet& facet) {
  CollisionAabb bounds{.min = facet.vertices[0], .max = facet.vertices[0]};
  for (std::size_t index = 1; index < facet.vertices.size(); ++index) {
    const C3Vector& vertex = facet.vertices[index];
    bounds.min.x = std::min(bounds.min.x, vertex.x);
    bounds.min.y = std::min(bounds.min.y, vertex.y);
    bounds.min.z = std::min(bounds.min.z, vertex.z);
    bounds.max.x = std::max(bounds.max.x, vertex.x);
    bounds.max.y = std::max(bounds.max.y, vertex.y);
    bounds.max.z = std::max(bounds.max.z, vertex.z);
  }
  return bounds;
}

template <std::size_t Size>
void Project(const std::array<C3Vector, Size>& vertices,
             const C3Vector& axis, float& minimum, float& maximum) {
  minimum = Dot(vertices[0], axis);
  maximum = minimum;
  for (std::size_t index = 1; index < Size; ++index) {
    const float projection = Dot(vertices[index], axis);
    minimum = std::min(minimum, projection);
    maximum = std::max(maximum, projection);
  }
}

struct FacetSweep {
  bool hit{false};
  float fraction{1.0f};
  C3Vector normal{0.0f, 0.0f, 1.0f};

  float depth{0.0f};
  std::array<C3Vector, 7> generated_normals{};
  std::uint8_t generated_normal_count{0};
  float generated_contact_distance{0.0f};
  bool generated_contact_found{false};
};

struct HullPlane {
  C3Vector normal{};
  float offset{0.0f};
};

constexpr std::array<std::array<std::uint8_t, 4>, 4> kLowerFaceIndices{{
    {{0, 1, 2, 0}},
    {{0, 3, 4, 0}},
    {{0, 2, 3, 0}},
    {{0, 4, 1, 0}},
}};

constexpr std::array<std::array<std::uint8_t, 4>, 5> kOuterFaceIndices{{
    {{1, 2, 6, 5}},
    {{3, 4, 8, 7}},
    {{2, 3, 7, 6}},
    {{4, 1, 5, 8}},
    {{5, 6, 7, 8}},
}};

[[nodiscard]] HullPlane HullPlaneFromFace(
    const std::array<C3Vector, 9>& hull, const std::size_t face_index) {
  const auto& face_vertices =
      face_index < kOuterFaceIndices.size()
          ? kOuterFaceIndices[face_index]
          : kLowerFaceIndices[face_index - kOuterFaceIndices.size()];
  return {.normal = kHullFaceNormals[face_index],
          .offset = -Dot(kHullFaceNormals[face_index],
                         hull[face_vertices[0]])};
}

struct GeneratedFaceContact {
  bool hit{false};
  float distance{0.0f};
};

constexpr std::size_t kMaximumClippedPolygonVertices = 15u;

[[nodiscard]] bool ClipPolygonByRetailPlane(
    std::array<C3Vector, kMaximumClippedPolygonVertices>& polygon,
    std::size_t& polygon_count, const HullPlane& plane) {
  if (polygon_count == 0u) {
    return false;
  }

  std::array<float, kMaximumClippedPolygonVertices> distances{};
  float minimum_distance = std::numeric_limits<float>::max();
  float maximum_distance = -std::numeric_limits<float>::max();
  for (std::size_t index = 0; index < polygon_count; ++index) {
    const float distance =
        -(Dot(plane.normal, polygon[index]) + plane.offset);
    distances[index] = distance;
    minimum_distance = std::min(minimum_distance, distance);
    maximum_distance = std::max(maximum_distance, distance);
  }

  if (-Constants::kPlaneEpsilon < minimum_distance) {
    return polygon_count >= 3u;
  }
  if (maximum_distance < Constants::kPlaneEpsilon &&
      !std::isnan(maximum_distance)) {
    polygon_count = 0u;
    return false;
  }

  const std::array<C3Vector, kMaximumClippedPolygonVertices> input = polygon;
  const std::size_t input_count = polygon_count;
  std::array<C3Vector, kMaximumClippedPolygonVertices> clipped{};
  std::size_t clipped_count = 0u;
  const auto append = [&](const C3Vector& value) {
    if (clipped_count < clipped.size()) {
      clipped[clipped_count++] = value;
    }
  };

  for (std::size_t index = 0; index < input_count; ++index) {
    const std::size_t previous_index =
        (index + input_count - 1u) % input_count;
    const float previous_distance = distances[previous_index];
    const float current_distance = distances[index];
    const bool previous_inside = previous_distance >= 0.0f;
    const bool current_inside = current_distance >= 0.0f;

    if (previous_inside) {
      if (current_inside) {
        append(input[index]);
      } else if (Constants::kPlaneEpsilon < previous_distance) {
        const float denominator = current_distance - previous_distance;
        if (std::fabs(denominator) >= Constants::kTiny) {
          const float fraction = previous_distance / denominator;
          append(Subtract(input[previous_index],
                          Scale(Subtract(input[index], input[previous_index]),
                                fraction)));
        }
      }
    } else if (current_inside) {
      if (Constants::kPlaneEpsilon < current_distance) {
        const float denominator = current_distance - previous_distance;
        if (std::fabs(denominator) >= Constants::kTiny) {
          const float fraction = previous_distance / denominator;
          append(Subtract(input[previous_index],
                          Scale(Subtract(input[index], input[previous_index]),
                                fraction)));
        }
      }
      append(input[index]);
    }
  }

  polygon = clipped;
  polygon_count = clipped_count;
  if (polygon_count < 3u) {
    polygon_count = 0u;
    return false;
  }
  return true;
}

[[nodiscard]] GeneratedFaceContact GeneratedFaceContactAtDistance(
    const std::array<C3Vector, 9>& hull,
    const MovementCollisionFacet& facet, const C3Vector& displacement,
    const C3Vector& face_normal,
    const std::array<std::uint8_t, 4>& face_indices,
    const std::size_t face_vertex_count, const std::size_t face_index,
    const float maximum_distance) {
  const float displacement_length = Length(displacement);
  if (!(displacement_length >= Constants::kTiny) ||
      std::isnan(displacement_length)) {
    return {};
  }
  const C3Vector sweep_direction =
      Scale(displacement, 1.0f / displacement_length);
  const float face_speed = Dot(face_normal, sweep_direction);
  if (!(face_speed > 0.0f) || std::isnan(face_speed)) {
    return {};
  }

  const HullPlane face_plane{
      .normal = face_normal,
      .offset = -Dot(face_normal, hull[face_indices[0]])};
  std::array<C3Vector, kMaximumClippedPolygonVertices> polygon{};
  polygon[0] = facet.vertices[0];
  polygon[1] = facet.vertices[1];
  polygon[2] = facet.vertices[2];
  std::size_t polygon_count = 3;

  std::array<HullPlane, 4> side_planes{};
  for (std::size_t edge_index = 0; edge_index < face_vertex_count;
       ++edge_index) {
    const C3Vector& edge_start =
        hull[face_indices[edge_index]];
    const C3Vector& edge_end =
        hull[face_indices[(edge_index + 1) % face_vertex_count]];
    C3Vector side_normal = Cross(Subtract(edge_end, edge_start),
                                 sweep_direction);
    const float side_length_squared = LengthSquared(side_normal);
    if (side_length_squared < Constants::kTraceEpsilon ||
        std::isnan(side_length_squared)) {
      return {};
    }
    side_normal = Scale(side_normal, 1.0f / std::sqrt(side_length_squared));
    float side_offset = -Dot(side_normal, edge_start);
    const C3Vector& reference_vertex =
        hull[face_indices[(edge_index + face_vertex_count - 1u) %
                          face_vertex_count]];
    if (Dot(side_normal, Subtract(reference_vertex, edge_start)) > 0.0f) {
      side_normal = Scale(side_normal, -1.0f);
      side_offset = -side_offset;
    }
    side_planes[edge_index] = {.normal = side_normal,
                               .offset = side_offset};
    if (!ClipPolygonByRetailPlane(polygon, polygon_count,
                                  side_planes[edge_index])) {
      return {};
    }
  }

  const auto evaluate_face = [&](const float fallback_distance_epsilon,
                                 bool& requires_hull_fallback) {
    float contact_distance = std::numeric_limits<float>::max();
    requires_hull_fallback = true;
    const float face_speed_abs = std::fabs(face_speed);

    if (Constants::kTiny <= face_speed_abs || std::isnan(face_speed_abs)) {
      for (std::size_t index = 0; index < polygon_count; ++index) {
        float distance =
            (Dot(face_plane.normal, polygon[index]) + face_plane.offset) /
            face_speed;
        if (distance < contact_distance) {
          contact_distance = distance;
          if (distance <= 0.0f) {
            contact_distance = 0.0f;
          }
        }
        if (-fallback_distance_epsilon < distance) {
          requires_hull_fallback = false;
        }
      }
    } else {
      for (std::size_t index = 0; index < polygon_count; ++index) {
        const float plane_distance =
            Dot(face_plane.normal, polygon[index]) + face_plane.offset;
        if (plane_distance < contact_distance) {
          contact_distance = plane_distance <= 0.0f ? 0.0f : plane_distance;
        }
        if (-fallback_distance_epsilon < plane_distance) {
          requires_hull_fallback = false;
        }
      }
    }
    return contact_distance;
  };

  bool requires_hull_fallback = false;

  float contact_distance =
      evaluate_face(Constants::kTraceEpsilon, requires_hull_fallback);
  if (requires_hull_fallback) {

    for (std::size_t hull_face_index = 0;
         hull_face_index < kHullFaceNormals.size(); ++hull_face_index) {
      if (hull_face_index == face_index) {
        continue;
      }
      const HullPlane hull_plane =
          HullPlaneFromFace(hull, hull_face_index);
      if (!ClipPolygonByRetailPlane(polygon, polygon_count, hull_plane)) {
        return {};
      }
    }
    contact_distance = evaluate_face(Constants::kMinimumHullSweepDistance,
                                     requires_hull_fallback);
    if (requires_hull_fallback) {
      return {};
    }
  }

  if (!std::isfinite(contact_distance) ||
      contact_distance > maximum_distance) {
    return {};
  }
  return {.hit = true,
          .distance = std::max(0.0f, contact_distance)};
}

[[nodiscard]] FacetSweep SweepFacet(
    const std::array<C3Vector, 9>& hull,
    const MovementCollisionFacet& facet,
    const C3Vector& displacement, const float maximum_distance) {
  const float displacement_length = Length(displacement);
  if (!(displacement_length >= Constants::kTiny) ||
      std::isnan(displacement_length)) {
    return {};
  }
  const C3Vector sweep_direction =
      Scale(displacement, 1.0f / displacement_length);

  if (Dot(facet.normal, sweep_direction) > -1.0e-5f) {
    return {};
  }

  std::array<C3Vector, 7> generated_normals{};
  std::uint8_t generated_normal_count = 0;
  float generated_contact_distance = maximum_distance;
  bool generated_contact_found = false;

  const auto consider_generated_face =
      [&](const std::size_t face_index,
          const std::array<std::uint8_t, 4>& face_vertices,
          const std::size_t face_vertex_count) {
        const GeneratedFaceContact candidate = GeneratedFaceContactAtDistance(
            hull, facet, displacement, kHullFaceNormals[face_index],
            face_vertices, face_vertex_count, face_index, maximum_distance);
        if (!candidate.hit) {
          return;
        }
        generated_contact_found = true;
        const float previous_distance = generated_contact_distance;
        if (previous_distance - Constants::kPlaneEpsilon <=
            candidate.distance) {
          if (candidate.distance <
                  previous_distance + Constants::kPlaneEpsilon &&
              generated_normal_count < generated_normals.size()) {
            generated_normals[generated_normal_count++] =
                kHullFaceNormals[face_index];
          }
        } else {
          generated_normals[0] = kHullFaceNormals[face_index];
          generated_normal_count = 1;
        }
        if (candidate.distance < previous_distance) {
          generated_contact_distance = candidate.distance;
          if (generated_normal_count > 1u) {
            const C3Vector newest =
                generated_normals[generated_normal_count - 1u];
            for (std::size_t index = generated_normal_count - 1u; index > 0u;
                 --index) {
              generated_normals[index] = generated_normals[index - 1u];
            }
            generated_normals[0] = newest;
          }
        }
      };

  for (std::size_t index = 0; index < kLowerFaceIndices.size(); ++index) {
    consider_generated_face(index + 5u, kLowerFaceIndices[index], 3u);
  }
  for (std::size_t index = 0; index < kOuterFaceIndices.size(); ++index) {
    consider_generated_face(index, kOuterFaceIndices[index], 4u);
  }

  if (!generated_contact_found) {
    return {};
  }
  const float retail_contact_distance =
      generated_contact_distance >= Constants::kPlaneEpsilon
          ? generated_contact_distance
          : 0.0f;
  return {.hit = true,
          .fraction = std::clamp(retail_contact_distance / maximum_distance,
                                 0.0f, 1.0f),
          .normal = NormalizeOr(facet.normal, {0.0f, 0.0f, 1.0f}),
          .generated_normals = generated_normals,
          .generated_normal_count = generated_normal_count,
          .generated_contact_distance = retail_contact_distance,
          .generated_contact_found = generated_contact_found};
}

[[nodiscard]] FacetSweep StaticOverlapFacet(
    const std::array<C3Vector, 9>& hull,
    const MovementCollisionFacet& facet) {
  std::array<C3Vector, 61> axes{};
  std::size_t axis_count = 0;
  for (const C3Vector& axis : kHullFaceNormals) {
    axes[axis_count++] = axis;
  }
  axes[axis_count++] = facet.normal;

  const std::array<C3Vector, 3> triangle_edges{{
      Subtract(facet.vertices[1], facet.vertices[0]),
      Subtract(facet.vertices[2], facet.vertices[1]),
      Subtract(facet.vertices[0], facet.vertices[2]),
  }};
  for (const auto& edge : kHullEdges) {
    const C3Vector hull_edge = Subtract(hull[edge[1]], hull[edge[0]]);
    for (const C3Vector& triangle_edge : triangle_edges) {
      axes[axis_count++] = Cross(hull_edge, triangle_edge);
    }
  }

  float min_overlap = std::numeric_limits<float>::max();
  C3Vector min_axis{0.0f, 0.0f, 1.0f};
  bool have_axis = false;

  for (std::size_t index = 0; index < axis_count; ++index) {
    const float axis_length_squared = LengthSquared(axes[index]);
    if (axis_length_squared < Constants::kTiny) {
      continue;
    }
    const C3Vector axis = Scale(axes[index], 1.0f / std::sqrt(axis_length_squared));
    float hull_min = 0.0f;
    float hull_max = 0.0f;
    float facet_min = 0.0f;
    float facet_max = 0.0f;
    Project(hull, axis, hull_min, hull_max);
    Project(facet.vertices, axis, facet_min, facet_max);

    const float overlap =
        std::min(hull_max, facet_max) - std::max(hull_min, facet_min);
    if (overlap < -Constants::kPlaneEpsilon) {

      return {};
    }
    if (overlap < min_overlap) {
      min_overlap = overlap;

      min_axis = ((hull_min + hull_max) < (facet_min + facet_max))
                     ? Scale(axis, -1.0f)
                     : axis;
      have_axis = true;
    }
  }
  if (!have_axis) {
    return {};
  }
  return {.hit = true, .fraction = 0.0f, .normal = min_axis, .depth = min_overlap};
}

[[nodiscard]] bool Walkable(const MovementCollisionBody& body,
                            const C3Vector& normal) {
  const float threshold = body.permissive_walkable_slope
                              ? Constants::kPermissiveNormalZ
                              : Constants::kWalkableNormalZ;
  return normal.z > threshold;
}

[[nodiscard]] bool HoverWalkable(const MovementCollisionBody& body,
                                 const C3Vector& normal) {
  const float threshold =
      (body.collision_mask & 0x2000u) != 0u
          ? Constants::kPermissiveNormalZ
          : Constants::kWalkableNormalZ;
  return normal.z > threshold;
}

[[nodiscard]] bool ContactTouchesTop(
    const MovementCollisionBody& body,
    const MovementCollisionContact& contact) {
  const C3Vector normal =
      NormalizeOr(contact.surface_normal, contact.normal);
  if (LengthSquared(normal) < Constants::kTiny ||
      !Finite(contact.vertices[0])) {
    return false;
  }
  const float plane_offset = -Dot(normal, contact.vertices[0]);
  const float top_z = body.position.z + body.height;
  const std::array<C3Vector, 4> top_corners{{
      {body.position.x - body.radius, body.position.y - body.radius, top_z},
      {body.position.x + body.radius, body.position.y - body.radius, top_z},
      {body.position.x - body.radius, body.position.y + body.radius, top_z},
      {body.position.x + body.radius, body.position.y + body.radius, top_z},
  }};
  return std::any_of(
      top_corners.begin(), top_corners.end(),
      [&normal, plane_offset](const C3Vector& corner) {
        return std::fabs(Dot(normal, corner) + plane_offset) <
               Constants::kPlaneEpsilon;
      });
}

[[nodiscard]] float StepAllowance(const MovementCollisionBody& body);

[[nodiscard]] C3Vector HoverNormalResponse(
    const C3Vector& displacement,
    const MovementCollisionContact& contact) {
  const C3Vector normal =
      NormalizeOr(contact.surface_normal, contact.normal);
  const float into_plane = Dot(displacement, normal);
  if (into_plane < 0.0f) {
    return Add(displacement,
               Scale(normal, -into_plane + Constants::kContactPush));
  }
  return displacement;
}

[[nodiscard]] C3Vector HoverWallResponse(
    const C3Vector& displacement,
    const MovementCollisionContact& contact) {
  const C3Vector normal =
      NormalizeOr(contact.surface_normal, contact.normal);
  if (normal.z < 0.0f && -normal.z > Constants::kWalkableNormalZ) {
    return {};
  }

  const float horizontal_normal_length =
      std::sqrt(normal.x * normal.x + normal.y * normal.y);
  C3Vector correction{};
  if (horizontal_normal_length >= Constants::kTiny) {
    const C3Vector horizontal_normal{
        normal.x / horizontal_normal_length,
        normal.y / horizontal_normal_length,
        0.0f};
    const float penetration =
        -Dot(displacement, normal) + Constants::kContactPush;
    correction = Scale(horizontal_normal, penetration);
  }
  const float horizontal_limit =
      std::sqrt(displacement.x * displacement.x +
                displacement.y * displacement.y);
  C3Vector corrected_horizontal = Add(displacement, correction);
  corrected_horizontal.z = 0.0f;
  const float corrected_length = Length(corrected_horizontal);
  if (corrected_length > horizontal_limit &&
      corrected_length >= Constants::kTiny) {
    corrected_horizontal =
        Scale(corrected_horizontal, horizontal_limit / corrected_length);
  }
  return {corrected_horizontal.x - displacement.x,
          corrected_horizontal.y - displacement.y, 0.0f};
}

[[nodiscard]] C3Vector AirborneEdgeResponseNormal(
    const MovementCollisionContact& contact, const C3Vector& direction) {
  const C3Vector surface =
      NormalizeOr(contact.surface_normal, contact.normal);
  const std::array<C3Vector, 3> edges{{
      Subtract(contact.vertices[1], contact.vertices[0]),
      Subtract(contact.vertices[2], contact.vertices[1]),
      Subtract(contact.vertices[0], contact.vertices[2]),
  }};
  C3Vector selected{};
  float selected_metric = std::numeric_limits<float>::max();
  bool have_selected = false;
  for (const C3Vector& edge : edges) {
    const float length_squared = LengthSquared(edge);
    if (length_squared < Constants::kTiny) {
      continue;
    }
    const C3Vector normalized = Scale(edge, 1.0f / std::sqrt(length_squared));

    if (!have_selected || normalized.y < selected_metric) {
      selected = normalized;
      selected_metric = normalized.y;
      have_selected = true;
    }
  }
  if (!have_selected) {
    return surface;
  }
  C3Vector response = Cross(selected, surface);

  if (Dot(response, direction) < 0.0f) {
    response = Scale(response, -1.0f);
  }
  return response;
}

[[nodiscard]] bool FacetTouchesLowerHull(
    const MovementCollisionBody& body,
    const MovementCollisionContact& contact) {
  const C3Vector normal =
      NormalizeOr(contact.surface_normal, contact.normal);
  if (LengthSquared(normal) < Constants::kTiny ||
      !Finite(contact.vertices[0])) {
    return false;
  }
  const float plane_offset = -Dot(normal, contact.vertices[0]);
  const float lower_z = body.position.z +
                        body.radius * Constants::kLowerRingScale;
  const std::array<C3Vector, 5> lower_hull{{
      {body.position.x, body.position.y, body.position.z},
      {body.position.x - body.radius, body.position.y - body.radius, lower_z},
      {body.position.x - body.radius, body.position.y + body.radius, lower_z},
      {body.position.x + body.radius, body.position.y + body.radius, lower_z},
      {body.position.x + body.radius, body.position.y - body.radius, lower_z},
  }};
  return std::any_of(
      lower_hull.begin(), lower_hull.end(),
      [&normal, plane_offset](const C3Vector& point) {
        return std::fabs(Dot(normal, point) + plane_offset) >=
                   Constants::kPlaneEpsilon
                   ? false
                   : true;
      });
}

[[nodiscard]] C3Vector AirborneGeneratedResponseNormal(
    const MovementCollisionBody& body,
    const MovementCollisionContact& contact) {
  const C3Vector surface =
      NormalizeOr(contact.surface_normal, contact.normal);
  if (contact.generated_normal_count == 1u) {

    return surface;
  }
  if (contact.generated_normal_count != 2u) {
    return surface;
  }
  const C3Vector first = NormalizeOr(contact.generated_normals[0], surface);
  const C3Vector second = NormalizeOr(contact.generated_normals[1], surface);
  C3Vector response = Cross(first, second);
  const float length_squared = LengthSquared(response);
  if (length_squared < Constants::kTiny) {
    return surface;
  }
  response = Scale(response, 1.0f / std::sqrt(length_squared));

  const float response_facet_dot = Dot(response, surface);
  if ((!std::isnan(response.z) && std::fabs(response.z) < Constants::kTraceEpsilon) ||
      (!std::isnan(response_facet_dot) &&
       std::fabs(response_facet_dot) < Constants::kTraceEpsilon)) {
    return surface;
  }

  if (std::fabs(response.z - 1.0f) >= Constants::kTraceEpsilon &&
      FacetTouchesLowerHull(body, contact)) {
    return surface;
  }

  if (Dot(response, first) > 0.0f) {
    response = Scale(response, -1.0f);
  }
  return response;
}

[[nodiscard]] C3Vector Deflect(
    const MovementCollisionBody& body, const C3Vector& direction,
    const float contact_distance,
    const float total_distance, const MovementCollisionContact& contact) {
  const C3Vector surface =
      NormalizeOr(contact.surface_normal, contact.normal);
  const bool edge_response =
      surface.z > Constants::kWalkableNormalZ || std::isnan(surface.z) ||
      (surface.z < 0.0f && -surface.z > Constants::kWalkableNormalZ);
  const C3Vector response =
      edge_response
          ? AirborneEdgeResponseNormal(contact, direction)
          : AirborneGeneratedResponseNormal(body, contact);
  const float horizontal_length =
      std::sqrt(response.x * response.x + response.y * response.y);
  if (horizontal_length < Constants::kTiny) {
    return {};
  }
  const C3Vector horizontal_unit{response.x / horizontal_length,
                                 response.y / horizontal_length, 0.0f};

  float correction = -Dot(response, direction) *
                     (total_distance - contact_distance);
  const float response_projection = Dot(response, horizontal_unit);
  if (std::fabs(response_projection) >= Constants::kTiny) {
    correction /= response_projection;
  }
  correction += Constants::kContactPush;
  return Scale(horizontal_unit, correction);
}

[[nodiscard]] float AirborneAllowedContactTime(
    const MovementCollisionStep& step, const float vertical_distance,
    const bool force_landing) {

  const float terminal_velocity =
      step.safe_fall ? PhysicsConstants::SafeFallTerminalVelocity
                     : PhysicsConstants::TerminalVelocity;
  float current_velocity = terminal_velocity;
  if (step.vertical_speed <= terminal_velocity) {
    current_velocity = step.vertical_speed;
  }

  if (std::fabs(current_velocity) >= Constants::kTiny ||
      std::isnan(std::fabs(current_velocity))) {
    float root = current_velocity * current_velocity +
                 vertical_distance * (2.0f * Constants::kGravity);
    root = root > 0.0f ? std::sqrt(root) : 0.0f;
    float time = (root - current_velocity) / Constants::kGravity;
    const float terminal_segment =
        (terminal_velocity - current_velocity) / Constants::kGravity;
    const float negative_root =
        (-current_velocity - root) / Constants::kGravity;
    if (terminal_segment < time) {
      time = (vertical_distance -
              (current_velocity +
               terminal_segment * Constants::kGravity * 0.5f) *
                  terminal_segment) /
                 terminal_velocity +
             terminal_segment;
    }

    if (force_landing) {
      time = negative_root >= 0.0f ? negative_root : 0.0f;
    }
    return time;
  }

  const float terminal_time = terminal_velocity / Constants::kGravity;
  const float terminal_distance =
      terminal_velocity * 0.5f * terminal_time;
  if (terminal_distance <= vertical_distance) {
    return (vertical_distance - terminal_distance) / terminal_velocity +
           terminal_time;
  }
  if (vertical_distance > 0.0f) {
    return std::sqrt((vertical_distance + vertical_distance) /
                     Constants::kGravity);
  }
  return 0.0f;
}

[[nodiscard]] float AdjustAirborneContactTime(
    const MovementCollisionStep& step, const C3Vector& pre_contact_position,
    const float contact_time_seconds,
    const float remaining_duration_seconds, const float initial_horizontal,
    const bool generated_falling_far,
    C3Vector& contact_vector, float& contact_distance) {

  bool force_landing = generated_falling_far;
  if (!force_landing && step.falling &&
      step.vertical_speed < 0.0f) {
    const float surface_time = -step.vertical_speed / Constants::kGravity;
    if (contact_time_seconds + remaining_duration_seconds < surface_time) {
      force_landing = true;
    } else if (contact_time_seconds <= surface_time) {
      const float horizontal_time =
          (surface_time - contact_time_seconds) * step.movement_speed;
      const float response_horizontal =
          contact_vector.x * contact_vector.x +
          contact_vector.y * contact_vector.y;
      force_landing = response_horizontal <
                      horizontal_time * horizontal_time;
    }
  }

  float horizontal_response_duration = 0.0f;
  if (initial_horizontal > Constants::kTraceEpsilon) {
    const float response_horizontal =
        std::sqrt(contact_vector.x * contact_vector.x +
                  contact_vector.y * contact_vector.y);
    horizontal_response_duration =
        (response_horizontal / initial_horizontal) *
        remaining_duration_seconds;
  }

  const float allowed_time = AirborneAllowedContactTime(
      step,
      (step.fall_start_z - pre_contact_position.z) - contact_vector.z,
      force_landing);
  if (contact_time_seconds < allowed_time) {
    const float delta = allowed_time - contact_time_seconds;
    if (delta <= remaining_duration_seconds) {
      if (delta < horizontal_response_duration) {
        const float ratio = delta / horizontal_response_duration;
        contact_vector.x *= ratio;
        contact_vector.y *= ratio;
        contact_distance = Length(contact_vector);
      }

      return delta;
    }
    return remaining_duration_seconds;
  }

  contact_vector = {};
  contact_distance = 0.0f;
  return 0.0f;
}

[[nodiscard]] C3Vector GroundWalkableResponse(
    MovementCollisionBody& body,
    const C3Vector& displacement,
    const MovementCollisionContact& contact) {
  C3Vector normal =
      NormalizeOr(contact.surface_normal, contact.normal);

  if (body.stepping && normal.z <= Constants::kWalkableNormalZ &&
      !std::isnan(normal.z) &&
      LengthSquared(normal) >= Constants::kTiny) {
    normal = Scale(normal, -1.0f);
  }
  const float distance = Length(displacement);
  if (distance < Constants::kTiny ||
      std::fabs(normal.z) < Constants::kTiny) {
    return displacement;
  }
  C3Vector direction = Scale(displacement, 1.0f / distance);
  float lift = -Dot(normal, direction) * distance / normal.z;

  if (!body.stepping || lift >= 0.0f || std::isnan(lift)) {
    const float upward_allowance = StepAllowance(body);
    const float downward_allowance = body.step_height;
    if (lift > upward_allowance) {
      direction = Scale(direction, upward_allowance / lift);
      lift = upward_allowance;
    } else if (lift < -downward_allowance) {
      direction = Scale(direction, downward_allowance / -lift);
      lift = -downward_allowance;
    }
    return Add(Scale(direction, distance), {0.0f, 0.0f, lift});
  }

  return {0.0f, 0.0f, StepAllowance(body)};
}

[[nodiscard]] C3Vector SimpleCollisionCorrection(
    const C3Vector& remaining,
    const MovementCollisionContact& contact) {
  const C3Vector normal =
      NormalizeOr(contact.surface_normal, contact.normal);
  const float correction_distance =
      -Dot(normal, remaining) + Constants::kContactPush;
  return Scale(normal, correction_distance);
}

[[nodiscard]] bool WithinFacetFootprintTolerance(
    const C3Vector& position, const std::array<C3Vector, 3>& vertices) {
  constexpr float kFootprintTolerance = 0.083333336f;

  constexpr std::uint8_t kEdgeA[3] = {0, 1, 2};
  constexpr std::uint8_t kEdgeB[3] = {1, 2, 0};
  constexpr std::uint8_t kOpposite[3] = {2, 0, 1};
  for (std::size_t edge = 0; edge < 3; ++edge) {
    const C3Vector& a = vertices[kEdgeA[edge]];
    const C3Vector& b = vertices[kEdgeB[edge]];
    const C3Vector& opposite = vertices[kOpposite[edge]];

    C3Vector normal{-(b.y - a.y), b.x - a.x, 0.0f};
    const float length = std::sqrt(normal.x * normal.x + normal.y * normal.y);
    if (length < Constants::kTiny) {

      continue;
    }
    normal = Scale(normal, 1.0f / length);
    if (Dot(Subtract(opposite, a), normal) > 0.0f) {
      normal = Scale(normal, -1.0f);
    }
    const float signed_distance = Dot(Subtract(position, a), normal);
    if (signed_distance > kFootprintTolerance) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool AirborneGeneratedContactTriggersFallingFar(
    const MovementCollisionBody& body,
    const MovementCollisionStep& step,
    const C3Vector& pre_contact_position,
    const C3Vector& incoming_direction,
    const float contact_distance,
    const float contact_time_seconds,
    const MovementCollisionContact& contact) {
  const std::uint8_t generated_count = contact.generated_normal_count;
  if (generated_count == 0u) {
    return false;
  }

  const C3Vector surface =
      NormalizeOr(contact.surface_normal, contact.normal);
  const float slope_threshold = body.permissive_walkable_slope
                                    ? Constants::kPermissiveNormalZ
                                    : Constants::kWalkableNormalZ;
  const C3Vector pre_response_contact =
      Add(pre_contact_position, Scale(incoming_direction, contact_distance));
  const bool steep_surface = surface.z <= slope_threshold;
  if (!steep_surface &&
      WithinFacetFootprintTolerance(pre_response_contact, contact.vertices)) {
    return false;
  }
  if (!(incoming_direction.z > 0.0f)) {
    return false;
  }

  const float surface_time =
      step.falling && step.vertical_speed < 0.0f
          ? -step.vertical_speed / Constants::kGravity
          : 0.0f;
  if (!(surface_time >= contact_time_seconds) ||
      !(contact_distance <=
        (surface_time - contact_time_seconds) * step.movement_speed)) {
    return false;
  }

  const float first_vertical_delta =
      std::fabs(contact.generated_normals[0].z - 1.0f);
  if (std::isnan(first_vertical_delta) ||
      first_vertical_delta < Constants::kTraceEpsilon) {
    return false;
  }
  for (std::size_t index = 1u; index < generated_count; ++index) {
    const float vertical_delta =
        std::fabs(contact.generated_normals[index].z - 1.0f);
    if (std::isnan(vertical_delta) ||
        vertical_delta < Constants::kTraceEpsilon) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool AirborneLandingAdmission(
    const MovementCollisionBody& body,
    const C3Vector& pre_response_contact,
    const MovementCollisionContact& contact) {
  const C3Vector surface =
      NormalizeOr(contact.surface_normal, contact.normal);
  const float threshold = body.permissive_walkable_slope
                              ? Constants::kPermissiveNormalZ
                              : Constants::kWalkableNormalZ;
  return surface.z > threshold &&
         WithinFacetFootprintTolerance(pre_response_contact, contact.vertices);
}

[[nodiscard]] float StepAllowance(const MovementCollisionBody& body) {
  float allowance = body.step_height;
  if (body.stepping) {
    allowance -= body.position.z - body.step_reference_z;
  }
  return std::max(allowance, 0.0f);
}

[[nodiscard]] C3Vector HorizontalWallDeflect(
    const C3Vector& displacement,
    const MovementCollisionContactSet& contacts) {
  const C3Vector surface =
      NormalizeOr(contacts.front().surface_normal, contacts.front().normal);
  if (surface.z < 0.0f && -surface.z > Constants::kWalkableNormalZ) {
    return {};
  }
  const float horizontal_length =
      std::sqrt(surface.x * surface.x + surface.y * surface.y);
  if (horizontal_length < Constants::kTiny) {
    return {};
  }
  const float penetration =
      -Dot(displacement, surface) + Constants::kContactPush;
  C3Vector result = Add(
      displacement,
      Scale({surface.x / horizontal_length, surface.y / horizontal_length,
             0.0f},
            penetration));
  result.z = 0.0f;
  const float limit = std::sqrt(displacement.x * displacement.x +
                                displacement.y * displacement.y);
  const float length = Length(result);
  if (length > limit && length >= Constants::kTiny) {
    result = Scale(result, limit / length);
  }
  return result;
}

}

bool CollisionAabb::Contains(const CollisionAabb& other) const {
  return min.x <= other.min.x && min.y <= other.min.y && min.z <= other.min.z &&
         max.x >= other.max.x && max.y >= other.max.y && max.z >= other.max.z;
}

C3Vector MovementParentTransform::ToWorldPoint(const C3Vector& point) const {
  return Add(TransformVector(parent_to_world, point), parent_origin_world);
}

C3Vector MovementParentTransform::ToWorldVector(const C3Vector& vector) const {
  return TransformVector(parent_to_world, vector);
}

C3Vector MovementParentTransform::ToParentPoint(const C3Vector& point) const {
  return TransformVector(world_to_parent, Subtract(point, parent_origin_world));
}

C3Vector MovementParentTransform::ToParentVector(const C3Vector& vector) const {
  return TransformVector(world_to_parent, vector);
}

C3Vector MovementParentTransform::WorldNormalToParent(
    const C3Vector& normal) const {
  return {parent_to_world[0] * normal.x + parent_to_world[3] * normal.y +
              parent_to_world[6] * normal.z,
          parent_to_world[1] * normal.x + parent_to_world[4] * normal.y +
              parent_to_world[7] * normal.z,
          parent_to_world[2] * normal.x + parent_to_world[5] * normal.y +
              parent_to_world[8] * normal.z};
}

MovementCollisionSolver::MovementCollisionSolver(
    MovementCollisionCallbacks callbacks)
    : callbacks_(std::move(callbacks)) {}

void MovementCollisionSolver::SetCallbacks(
    MovementCollisionCallbacks callbacks) {
  callbacks_ = std::move(callbacks);
  InvalidateFacets();
}

bool MovementCollisionSolver::IsBound() const {
  return static_cast<bool>(callbacks_.query_facets);
}

void MovementCollisionSolver::InvalidateFacets() {
  cache_ = {};
}

void MovementCollisionSolver::Reset() {
  InvalidateFacets();
}

std::shared_ptr<MovementCollisionSolver>
MovementCollisionSolver::CreateIndependentSolver() const {
  return std::make_shared<MovementCollisionSolver>(callbacks_);
}

std::size_t MovementCollisionSolver::CachedFacetCount() const {
  return cache_.facets.size();
}

std::optional<CollisionAabb> MovementCollisionSolver::CachedBounds() const {
  if (!cache_.valid) {
    return std::nullopt;
  }
  return cache_.world_bounds;
}

bool MovementCollisionSolver::RefreshFacets(
    const MovementCollisionBody& body, const C3Vector& displacement) {
  if (!callbacks_.query_facets) {
    return false;
  }
  if (callbacks_.cancelled && callbacks_.cancelled()) {
    return false;
  }

  CollisionAabb requested = SweptWorldBounds(body, displacement);
  const std::uint64_t source_revision =
      callbacks_.facet_revision ? callbacks_.facet_revision() : 0;
  const std::uint64_t parent_revision =
      body.parent.has_value() ? body.parent->revision : 0;
  const bool source_compatible =
      cache_.valid && cache_.collision_mask == body.collision_mask &&
      cache_.has_secondary == body.include_secondary_facets &&
      cache_.source_revision == source_revision && cache_.complete;

  const auto prepare_for_parent =
      [&body, this](const std::vector<MovementCollisionFacet>& source) {
        std::vector<MovementCollisionFacet> prepared;
        prepared.reserve(source.size());
        for (MovementCollisionFacet facet : source) {
          if (body.parent.has_value()) {
            for (C3Vector& vertex : facet.vertices) {
              vertex = body.parent->ToParentPoint(vertex);
            }
            facet.normal = body.parent->WorldNormalToParent(facet.normal);
          }
          if (LengthSquared(facet.normal) < Constants::kTiny) {
            facet.normal = Cross(Subtract(facet.vertices[1], facet.vertices[0]),
                                 Subtract(facet.vertices[2], facet.vertices[0]));
          }
          facet.normal =
              NormalizeOr(facet.normal, {0.0f, 0.0f, 1.0f});
          if (facet.secondary) {
            facet.normal = Scale(facet.normal, -1.0f);
          }
          facet.plane_offset = -Dot(facet.normal, facet.vertices[0]);
          prepared.push_back(facet);
        }
        cache_.facets = std::move(prepared);
        cache_.parent_revision =
            body.parent.has_value() ? body.parent->revision : 0;
      };

  if (source_compatible && cache_.complete &&
      cache_.world_bounds.Contains(requested) &&
      cache_.parent_revision == parent_revision) {
    return true;
  }

  CollisionAabb query_bounds = requested;
  if (source_compatible && cache_.parent_revision == parent_revision) {
    query_bounds = Union(cache_.world_bounds, requested);
    Expand(query_bounds, kFacetCacheGrowth);
  } else {

    Expand(query_bounds, kFacetCacheGrowth);
  }

  auto primary = callbacks_.query_facets(
      query_bounds, body.collision_mask, MovementCollisionLayer::kPrimary);
  if (!primary.has_value() ||
      primary->facets.size() > kMaximumPreparedFacets) {
    return false;
  }

  std::optional<MovementCollisionFacetBatch> secondary;
  if (body.include_secondary_facets) {
    secondary = callbacks_.query_facets(
        query_bounds, body.collision_mask, MovementCollisionLayer::kSecondary);

    if (secondary.has_value() &&
        secondary->facets.size() > kMaximumPreparedFacets -
                                       primary->facets.size()) {
      return false;
    }
  }
  if (callbacks_.cancelled && callbacks_.cancelled()) {
    return false;
  }

  std::vector<MovementCollisionFacet> world_facets;
  world_facets.reserve(primary->facets.size() +
                       (secondary.has_value() ? secondary->facets.size() : 0u));
  const auto append = [&](const MovementCollisionFacetBatch& batch,
                          const bool secondary_layer) {
    for (MovementCollisionFacet facet : batch.facets) {
      if (!Finite(facet.vertices[0]) || !Finite(facet.vertices[1]) ||
          !Finite(facet.vertices[2])) {
        continue;
      }
      if (LengthSquared(facet.normal) < Constants::kTiny) {
        facet.normal = Cross(Subtract(facet.vertices[1], facet.vertices[0]),
                             Subtract(facet.vertices[2], facet.vertices[0]));
      }
      facet.normal = NormalizeOr(facet.normal, {0.0f, 0.0f, 1.0f});
      facet.secondary = secondary_layer;
      facet.plane_offset = -Dot(facet.normal, facet.vertices[0]);
      world_facets.push_back(facet);
    }
  };
  append(*primary, false);
  if (secondary.has_value()) {
    append(*secondary, true);
  }

  cache_.world_bounds = query_bounds;
  cache_.world_facets = std::move(world_facets);
  cache_.collision_mask = body.collision_mask;
  cache_.primary_revision = primary->revision;
  cache_.secondary_revision = secondary.has_value() ? secondary->revision : 0;
  cache_.source_revision = source_revision;
  cache_.primary_facet_count = primary->facets.size();
  cache_.has_secondary = body.include_secondary_facets;
  cache_.complete =
      primary->completeness == MovementCollisionFacetCompleteness::kComplete;
  cache_.valid = true;
  prepare_for_parent(cache_.world_facets);
  return true;
}

MovementCollisionTrace MovementCollisionSolver::SweepPrepared(
    const MovementCollisionBody& body,
    const C3Vector& displacement,
    const float requested_distance) const {
  MovementCollisionTrace result;
  const float geometry_distance = Length(displacement);
  result.distance = requested_distance;
  if (geometry_distance < Constants::kTraceEpsilon ||
      requested_distance < Constants::kTraceEpsilon) {
    return result;
  }

  const auto hull = BuildRetailHull(body);

  const auto liquid_hull =
      body.include_secondary_facets
          ? BuildRetailHull(body, body.height *
                                       Constants::kSwimLiquidHullHeightScale)
          : hull;
  CollisionAabb local_sweep{
      .min = {body.position.x - body.radius,
              body.position.y - body.radius, body.position.z},
      .max = {body.position.x + body.radius,
              body.position.y + body.radius,
              body.position.z + body.height},
  };
  local_sweep.min.x = std::min(local_sweep.min.x,
                               local_sweep.min.x + displacement.x);
  local_sweep.min.y = std::min(local_sweep.min.y,
                               local_sweep.min.y + displacement.y);
  local_sweep.min.z = std::min(local_sweep.min.z,
                               local_sweep.min.z + displacement.z);
  local_sweep.max.x = std::max(local_sweep.max.x,
                               local_sweep.max.x + displacement.x);
  local_sweep.max.y = std::max(local_sweep.max.y,
                               local_sweep.max.y + displacement.y);
  local_sweep.max.z = std::max(local_sweep.max.z,
                               local_sweep.max.z + displacement.z);
  Expand(local_sweep, Constants::kPlaneEpsilon);

  const auto sweep_pass = [&](const bool secondary_layer,
                              const std::array<C3Vector, 9>& pass_hull) {
    MovementCollisionTrace pass_result;
    pass_result.distance = requested_distance;
    float pass_earliest = requested_distance;
    for (const MovementCollisionFacet& facet : cache_.facets) {
      if (facet.secondary != secondary_layer ||
          !AabbOverlaps(local_sweep, FacetBounds(facet))) {
        continue;
      }
      const FacetSweep sweep =
          SweepFacet(pass_hull, facet, displacement, requested_distance);
      if (!sweep.hit) {
        continue;
      }

      const float contact_distance = sweep.generated_contact_distance;
      if (contact_distance <= pass_earliest) {
        pass_earliest = contact_distance;
        pass_result.contacts.clear();
        pass_result.contacts.push_back({.normal = sweep.normal,
                                        .surface_normal = facet.normal,
                                        .distance = contact_distance,
                                        .vertices = facet.vertices,
                                        .owner_id = facet.owner_id,
                                        .facet_id = facet.facet_id,
                                        .owner_guid = facet.owner_guid,
                                        .secondary = facet.secondary,
                                        .generated_normals =
                                            sweep.generated_normals,
                                        .generated_normal_count =
                                            sweep.generated_normal_count});
      }
    }
    if (!pass_result.contacts.empty()) {
      pass_result.hit = true;
      pass_result.distance = std::max(0.0f, pass_earliest);
    }
    return pass_result;
  };

  const bool use_secondary_pass =
      body.include_secondary_facets && body.allow_secondary_pass &&
      !body.primary_contact_seen;
  result = sweep_pass(false, hull);
  if (result.hit || !use_secondary_pass) {
    return result;
  }
  return sweep_pass(true, liquid_hull);
}

MovementCollisionTrace MovementCollisionSolver::SweepHull(
    const MovementCollisionBody& body, const C3Vector& displacement,
    bool* query_ok) {
  const float requested_distance = Length(displacement);
  if (requested_distance < Constants::kTraceEpsilon) {
    if (query_ok != nullptr) {
      *query_ok = true;
    }
    return {};
  }
  const float geometry_distance =
      std::max(requested_distance, Constants::kMinimumHullSweepDistance);
  const C3Vector geometry_displacement =
      Scale(displacement, geometry_distance / requested_distance);
  const bool refreshed = RefreshFacets(body, geometry_displacement);
  if (query_ok != nullptr) {
    *query_ok = refreshed;
  }
  if (!refreshed) {
    return {};
  }
  return SweepPrepared(body, geometry_displacement, requested_distance);
}

MovementCollisionTrace MovementCollisionSolver::QueryStaticOverlap(
    const MovementCollisionBody& body, bool* query_ok) {

  const bool refreshed = RefreshFacets(body, {0.0f, 0.0f, 0.0f});
  if (query_ok != nullptr) {
    *query_ok = refreshed;
  }
  if (!refreshed) {
    return {};
  }

  MovementCollisionTrace result;
  result.distance = 0.0f;
  const auto hull = BuildRetailHull(body);
  const CollisionAabb local_box{
      .min = {body.position.x - body.radius, body.position.y - body.radius,
              body.position.z},
      .max = {body.position.x + body.radius, body.position.y + body.radius,
              body.position.z + body.height},
  };
  for (const MovementCollisionFacet& facet : cache_.facets) {
    if (!AabbOverlaps(local_box, FacetBounds(facet))) {
      continue;
    }
    const FacetSweep overlap = StaticOverlapFacet(hull, facet);
    if (!overlap.hit) {
      continue;
    }
    if (result.contacts.size() < MovementCollisionContactSet::kCapacity) {

      result.contacts.push_back({.normal = overlap.normal,
                                 .surface_normal = facet.normal,
                                 .distance = overlap.depth,
                                 .vertices = facet.vertices,
                                 .owner_id = facet.owner_id,
                                 .facet_id = facet.facet_id,
                                 .owner_guid = facet.owner_guid,
                                 .secondary = facet.secondary});
    }
  }
  if (!result.contacts.empty()) {

    std::sort(result.contacts.begin(), result.contacts.end(),
              [](const MovementCollisionContact& lhs,
                 const MovementCollisionContact& rhs) {
                return lhs.distance > rhs.distance;
              });
    result.hit = true;
  }
  return result;
}

bool MovementCollisionSolver::TryTerrainStep(
    MovementCollisionBody& body, const C3Vector& displacement,
    const MovementCollisionContact& trigger,
    const MovementCollisionStep& step) {

  const float allowance = StepAllowance(body);
  const C3Vector horizontal{displacement.x, displacement.y, 0.0f};
  const float horizontal_length = Length(horizontal);
  if (body.step_height <= Constants::kTraceEpsilon ||
      allowance <= Constants::kTraceEpsilon ||
      horizontal_length < Constants::kTraceEpsilon) {
    return false;
  }
  const C3Vector direction = Scale(horizontal, 1.0f / horizontal_length);

  const float probe_length =
      std::max(body.radius + Constants::kPlaneEpsilon,
               body.step_height * Constants::kStepProbeLateralScale);

  MovementCollisionBody trial = body;
  bool query_ok = false;

  C3Vector probe_direction = direction;
  const C3Vector& trigger_normal = trigger.surface_normal;
  if (trigger_normal.z >= 0.0f &&
      trigger_normal.z <= Constants::kWalkableNormalZ) {
    const float horizontal_normal =
        std::sqrt(trigger_normal.x * trigger_normal.x +
                  trigger_normal.y * trigger_normal.y);
    if (horizontal_normal >= Constants::kTiny) {
      const C3Vector push{-trigger_normal.x / horizontal_normal,
                          -trigger_normal.y / horizontal_normal, 0.0f};
      const MovementCollisionTrace pre =
          SweepHull(trial, Scale(push, probe_length), &query_ok);
      if (!query_ok) {
        return false;
      }
      if (pre.hit && !pre.contacts.empty()) {
        const C3Vector& normal = pre.contacts.front().surface_normal;

        if (normal.x == trigger_normal.x && normal.y == trigger_normal.y &&
            normal.z == trigger_normal.z) {
          probe_direction = push;
        }
      }
    }
  }

  const C3Vector up{0.0f, 0.0f, allowance};
  const MovementCollisionTrace up_trace = SweepHull(trial, up, &query_ok);
  if (!query_ok) {
    return false;
  }
  const float rise =
      up_trace.hit ? std::max(0.0f, up_trace.distance) : allowance;

  const float banked_rise =
      body.stepping ? body.position.z - body.step_reference_z : 0.0f;
  if (std::fabs(rise + banked_rise) < Constants::kTiny) {
    body.stepping = false;
    return false;
  }
  trial.position.z += rise;

  float advance;
  bool lateral_clear;
  const C3Vector lateral = Scale(probe_direction, probe_length);
  const MovementCollisionTrace side = SweepHull(trial, lateral, &query_ok);
  if (!query_ok) {
    return false;
  }
  if (side.hit) {
    advance = std::max(0.0f, side.distance);
    lateral_clear = false;
    trial.position = Add(trial.position, Scale(probe_direction, advance));
    const float leftover = probe_length - advance;
    if (leftover > Constants::kTraceEpsilon && !side.contacts.empty()) {
      const MovementCollisionContact& hit = side.contacts.front();

      const bool overhang = hit.surface_normal.z < 0.0f &&
                            -hit.surface_normal.z > Constants::kWalkableNormalZ;
      if (Walkable(body, hit.surface_normal) || overhang) {

        const C3Vector& surface = hit.surface_normal;

        C3Vector forward = Scale(probe_direction, leftover);
        float lift;
        if (std::fabs(surface.z) < Constants::kTiny) {
          const float raw_ratio = -Dot(surface, forward);
          forward = {};
          lift = raw_ratio >= 0.0f ? allowance : -body.step_height;
        } else {
          lift = -Dot(surface, forward) / surface.z;
          if (lift < -body.step_height) {
            forward = Scale(forward, -body.step_height / lift);
            lift = -body.step_height;
          } else if (lift > allowance) {
            forward = Scale(forward, allowance / lift);
            lift = allowance;
          }
        }
        const C3Vector slide{forward.x, forward.y, lift};
        const float slide_length = Length(slide);
        if (slide_length >= Constants::kTraceEpsilon) {
          const MovementCollisionTrace retry =
              SweepHull(trial, slide, &query_ok);
          if (!query_ok) {
            return false;
          }
          const float fraction =
              retry.hit ? std::clamp(retry.distance / slide_length, 0.0f, 1.0f)
                        : 1.0f;
          lateral_clear = !retry.hit;
          const C3Vector moved = Scale(slide, fraction);
          trial.position = Add(trial.position, moved);
          advance += std::sqrt(moved.x * moved.x + moved.y * moved.y);
        }
      }
    }
  } else {
    advance = probe_length;
    lateral_clear = true;
    trial.position = Add(trial.position, lateral);
  }

  if (!lateral_clear && advance < body.radius) {
    body.stepping = false;
    return false;
  }

  const C3Vector down{0.0f, 0.0f, -rise};
  const MovementCollisionTrace down_trace = SweepHull(trial, down, &query_ok);
  if (!query_ok) {
    return false;
  }
  const float drop = down_trace.hit ? down_trace.distance : rise;
  trial.position.z -= drop;

  bool accepted = true;
  if (down_trace.hit && !down_trace.contacts.empty() &&
      !Walkable(body, down_trace.contacts.front().surface_normal)) {

    accepted = StepFallCarriesForward(
        body, trial.position, direction, std::max(0.0f, rise - drop),
        step.movement_speed, step.safe_fall);
  }
  if (!accepted) {
    body.stepping = false;
    return false;
  }
  if (!body.stepping) {
    body.step_reference_z = body.position.z;
  }
  body.stepping = true;
  return true;
}

bool MovementCollisionSolver::StepFallCarriesForward(
    const MovementCollisionBody& body, const C3Vector& trial_position,
    const C3Vector& direction, const float free_height,
    const float movement_speed, const bool safe_fall) {

  const std::uint32_t fall_ms = static_cast<std::uint32_t>(std::lround(
      std::sqrt((free_height + free_height) / Constants::kGravity) *
      1000.0f));
  if (fall_ms == 0u) {

    return false;
  }
  MovementCollisionBody simulated = body;
  simulated.position = trial_position;
  simulated.mode = MovementCollisionMode::kFalling;
  simulated.stepping = false;
  const float fall_seconds = static_cast<float>(fall_ms) * 0.001f;
  const float fall_distance = IntegrateFallDistance(
      fall_seconds, safe_fall, 0.0f);
  MovementCollisionStep fall_step;
  fall_step.displacement = {direction.x * fall_distance,
                            direction.y * fall_distance,
                            -fall_distance};
  fall_step.duration_ms = fall_ms;

  fall_step.movement_speed = movement_speed;
  fall_step.vertical_speed = 0.0f;
  fall_step.fall_start_z = simulated.position.z;
  fall_step.initial_direction = {direction.x, direction.y, 0.0f};
  fall_step.safe_fall = safe_fall;
  fall_step.falling = true;
  fall_step.directional_input = true;
  const MovementCollisionResult sim = SolveAirborne(simulated, fall_step);
  if (sim.status == MovementCollisionStatus::kQueryFailed ||
      sim.status == MovementCollisionStatus::kCancelled) {
    return false;
  }
  const float offset_x = simulated.position.x - body.position.x;
  const float offset_y = simulated.position.y - body.position.y;
  const float offset =
      std::sqrt(offset_x * offset_x + offset_y * offset_y);
  if (offset < body.radius) {
    return false;
  }
  return (offset_x / offset) * direction.x +
             (offset_y / offset) * direction.y >
         Constants::kStepFallAcceptCos;
}

MovementCollisionResult MovementCollisionSolver::SolveSimpleCollision(
    MovementCollisionBody& body, const MovementCollisionStep& step) {

  MovementCollisionResult result;
  result.consumed_ms = step.duration_ms;
  const C3Vector start_position = body.position;
  C3Vector remaining = step.displacement;
  float remaining_ms = static_cast<float>(step.duration_ms);
  std::uint32_t stalled = 0u;
  while (Length(remaining) >= Constants::kTraceEpsilon) {
    if (callbacks_.cancelled && callbacks_.cancelled()) {
      result.status = MovementCollisionStatus::kCancelled;
      return result;
    }

    bool query_ok = false;
    const float remaining_distance = Length(remaining);
    const MovementCollisionTrace trace = SweepHull(body, remaining, &query_ok);
    if (!query_ok) {
      result.status = callbacks_.cancelled && callbacks_.cancelled()
                          ? MovementCollisionStatus::kCancelled
                          : MovementCollisionStatus::kQueryFailed;
      return result;
    }

    if (!cache_.complete) {
      body.position = start_position;
      result.status = MovementCollisionStatus::kBlocked;
      result.reset_requested = true;
      return result;
    }
    if (!trace.hit || trace.contacts.empty()) {
      body.position = Add(body.position, remaining);
      remaining = {};
      break;
    }

    result.status = MovementCollisionStatus::kCollided;
    const float fraction = std::clamp(
        trace.distance / remaining_distance, 0.0f, 1.0f);
    body.position = Add(body.position, Scale(remaining, fraction));
    const float progressed_ms = remaining_ms * fraction;
    remaining_ms -= progressed_ms;
    remaining = Scale(remaining, 1.0f - fraction);
    result.last_contact = trace.contacts.front();
    if (!trace.contacts.front().secondary) {

      body.primary_contact_seen = true;
    }

    stalled = progressed_ms + Constants::kTraceEpsilon <= 1.0f
                  ? stalled + 1u
                  : 1u;
    if (stalled >= Constants::kStalledIterationLimit) {
      result.status = MovementCollisionStatus::kBlocked;
      result.reset_requested = true;
      remaining = {};
      break;
    }
    if (body.trigger_ascent_jump_on_liquid_contact &&
        trace.contacts.front().secondary && !body.primary_contact_seen) {

      result.ascent_jump_contact = true;
      result.state_snapshot_required = true;
      remaining = {};
      break;
    }

    if (remaining_ms < 1.0f) {
      result.state_snapshot_required = true;
      remaining = {};
      break;
    }

    remaining = Add(
        remaining,
        SimpleCollisionCorrection(remaining, trace.contacts.front()));
    result.state_snapshot_required = true;
    if (Length(remaining) < Constants::kTraceEpsilon) {
      remaining = {};
      break;
    }
  }

  if (Length(remaining) >= Constants::kTraceEpsilon) {
    result.status = MovementCollisionStatus::kBlocked;
    result.reset_requested = true;
  }
  return result;
}

MovementCollisionResult MovementCollisionSolver::SolveGround(
    MovementCollisionBody& body, const MovementCollisionStep& step) {
  MovementCollisionResult result;
  const C3Vector start_position = body.position;

  const auto hold_incomplete_data = [&]() -> bool {
    if (!cache_.complete) {
      body.position = start_position;
      result.status = MovementCollisionStatus::kBlocked;
      result.reset_requested = true;
      result.consumed_ms = step.duration_ms;
      return true;
    }
    return false;
  };

  const float walk_horizontal =
      std::sqrt(step.displacement.x * step.displacement.x +
                step.displacement.y * step.displacement.y);
  const Float2 walk_direction =
      walk_horizontal >= Constants::kTiny
          ? Float2{step.displacement.x / walk_horizontal,
                   step.displacement.y / walk_horizontal}
          : Float2{};
  C3Vector remaining = step.displacement;
  float remaining_ms = static_cast<float>(step.duration_ms);
  std::uint32_t stalled = 0;
  const std::uint64_t iteration_limit =
      static_cast<std::uint64_t>(step.duration_ms) +
      Constants::kStalledIterationLimit;
  for (std::uint64_t iteration = 0;
       Length(remaining) >= Constants::kTraceEpsilon &&
       iteration < iteration_limit;
       ++iteration) {
    if (callbacks_.cancelled && callbacks_.cancelled()) {
      result.status = MovementCollisionStatus::kCancelled;
      return result;
    }
    bool query_ok = false;
    const float remaining_distance = Length(remaining);
    const MovementCollisionTrace trace = SweepHull(body, remaining, &query_ok);
    if (!query_ok) {
      result.status = callbacks_.cancelled && callbacks_.cancelled()
                          ? MovementCollisionStatus::kCancelled
                          : MovementCollisionStatus::kQueryFailed;
      return result;
    }
    if (!trace.hit) {
      if (cache_.primary_facet_count == 0u && body.allow_fall_transition) {

        if (hold_incomplete_data()) {
          return result;
        }

        body.mode = MovementCollisionMode::kFalling;
        result.transitioned_to_falling = true;
        result.consumed_ms = 0u;
        return result;
      }
      body.position = Add(body.position, remaining);
      remaining = {};
      break;
    }

    result.status = MovementCollisionStatus::kCollided;
    const float fraction = std::clamp(trace.distance / remaining_distance,
                                      0.0f, 1.0f);
    body.position = Add(body.position, Scale(remaining, fraction));
    const float progressed_ms = remaining_ms * fraction;
    stalled = progressed_ms + Constants::kTraceEpsilon <= 1.0f
                  ? stalled + 1u
                  : 1u;
    remaining_ms -= progressed_ms;
    remaining = Scale(remaining, 1.0f - fraction);
    result.last_contact = trace.contacts.front();

    const MovementCollisionContact front = trace.contacts.front();
    const C3Vector& surface = front.surface_normal;
    const bool walkable = Walkable(body, surface);
    const bool touches_top = surface.z < 0.0f &&
                             ContactTouchesTop(body, front);
    const bool down_facing_overhang =
        touches_top && -surface.z > Constants::kWalkableNormalZ;
    const bool was_stepping = body.stepping;

    if (touches_top && was_stepping) {
      body.stepping = false;
    }

    const bool step_fall =
        touches_top && was_stepping && !down_facing_overhang;
    if (step_fall) {
      body.stepping = false;
      const MovementCollisionMode previous = body.mode;
      body.mode = MovementCollisionMode::kFalling;
      result.transitioned_to_falling = previous != body.mode;
      result.consumed_ms = step.duration_ms;
      return result;
    }

    if (walkable || down_facing_overhang) {

      if (walkable &&
          WithinFacetFootprintTolerance(body.position, front.vertices)) {
        body.stepping = false;
      }
      remaining = GroundWalkableResponse(body, remaining, front);
    } else {

      if (!body.stepping) {
        (void)TryTerrainStep(body, remaining, front, step);
      }
      if (body.stepping) {

        const float allowance = StepAllowance(body);
        C3Vector response_surface = surface;

        if (response_surface.z <= Constants::kWalkableNormalZ &&
            !std::isnan(response_surface.z) &&
            LengthSquared(response_surface) >= Constants::kTiny) {
          response_surface = Scale(response_surface, -1.0f);
        }
        C3Vector forward = remaining;
        float lift;
        if (std::fabs(response_surface.z) < Constants::kTiny) {
          lift = allowance;
        } else {
          lift = -(response_surface.x * remaining.x +
                   response_surface.y * remaining.y +
                   response_surface.z * remaining.z) /
                 response_surface.z;
          if (lift < 0.0f) {
            forward = {};
            lift = allowance;
          } else if (lift > allowance) {
            forward = Scale(forward, allowance / lift);
            lift = allowance;
          }
        }
        remaining = Add(forward, {0.0f, 0.0f, lift});
      } else {
        remaining = HorizontalWallDeflect(remaining, trace.contacts);
      }
    }

    if (body.stepping && remaining.z > Constants::kTraceEpsilon) {
      const float rise_ceiling = body.step_reference_z + body.step_height;
      if (body.position.z + remaining.z > rise_ceiling) {
        const float ratio = std::max(
            0.0f, (rise_ceiling - body.position.z) / remaining.z);
        remaining = Scale(remaining, ratio);
      }
    }

    if (stalled >= Constants::kStalledIterationLimit ||
        Length(remaining) < Constants::kMinDistance) {
      result.status = MovementCollisionStatus::kBlocked;
      result.reset_requested = true;
      remaining = {};
      break;
    }
  }

  if (Length(remaining) >= Constants::kTraceEpsilon) {
    result.status = MovementCollisionStatus::kBlocked;
    result.reset_requested = true;
  }

  if (result.status != MovementCollisionStatus::kBlocked) {
    const float movement_distance = Length(step.displacement);
    const float probe_distance =
        step.ground_probe_distance > 0.0f
            ? step.ground_probe_distance
            : movement_distance * Constants::kLowerRingScale;
    if (probe_distance > Constants::kTraceEpsilon) {
      const C3Vector down{0.0f, 0.0f, -probe_distance};
      bool query_ok = false;
      const MovementCollisionTrace ground = SweepHull(body, down, &query_ok);
      if (!query_ok) {
        result.status = callbacks_.cancelled && callbacks_.cancelled()
                            ? MovementCollisionStatus::kCancelled
                            : MovementCollisionStatus::kQueryFailed;
        return result;
      }
      if (ground.hit && !ground.contacts.empty()) {
        const MovementCollisionContact& support = ground.contacts.front();
        if (Walkable(body, support.surface_normal)) {

          body.position = Add(body.position,
                              Scale(down, ground.distance / probe_distance));
          body.stepping = false;
          result.last_contact = support;
        } else if (body.stepping &&
                   support.surface_normal.x * walk_direction.x +
                           support.surface_normal.y * walk_direction.y <=
                       -Constants::kSteepOpposeEpsilon) {

          body.position = Add(body.position,
                              Scale(down, ground.distance / probe_distance));
          result.last_contact = support;
        } else {
          if (hold_incomplete_data()) {
            return result;
          }

          if (body.allow_fall_transition) {
            body.stepping = false;
            const MovementCollisionMode previous = body.mode;
            body.mode = MovementCollisionMode::kFalling;
            result.transitioned_to_falling = previous != body.mode;
          }
        }
      } else if (body.stepping) {

        body.position = Add(body.position, down);
      } else {
        if (hold_incomplete_data()) {
          return result;
        }
        if (body.allow_fall_transition) {
          const MovementCollisionMode previous = body.mode;
          body.mode = MovementCollisionMode::kFalling;
          result.transitioned_to_falling = previous != body.mode;
        }
      }
    }
  }

  result.consumed_ms = step.duration_ms;
  return result;
}

MovementCollisionResult MovementCollisionSolver::SolveAirborne(
    MovementCollisionBody& body, const MovementCollisionStep& step) {
  MovementCollisionResult result;
  const bool falling_path = body.mode == MovementCollisionMode::kFalling;
  if (falling_path) {

    body.stepping = false;
  }
  const C3Vector start_position = body.position;
  const float initial_horizontal_limit =
      std::sqrt(step.displacement.x * step.displacement.x +
                step.displacement.y * step.displacement.y) +
      Constants::kTraceEpsilon;
  const float initial_horizontal =
      std::sqrt(step.displacement.x * step.displacement.x +
                step.displacement.y * step.displacement.y);
  const float vertical_recovery_limit =
      std::fabs(step.displacement.z) * Constants::kFallingLateralRecovery;
  C3Vector horizontal_direction = step.initial_direction;
  float horizontal_direction_length =
      std::sqrt(horizontal_direction.x * horizontal_direction.x +
                horizontal_direction.y * horizontal_direction.y);
  if (horizontal_direction_length < Constants::kTiny) {
    horizontal_direction = {step.displacement.x, step.displacement.y, 0.0f};
    horizontal_direction_length = initial_horizontal;
  }
  if (horizontal_direction_length >= Constants::kTiny) {
    horizontal_direction.x /= horizontal_direction_length;
    horizontal_direction.y /= horizontal_direction_length;
  } else {
    horizontal_direction = {};
  }
  float current_speed = step.movement_speed;
  C3Vector remaining = step.displacement;
  float remaining_ms = static_cast<float>(step.duration_ms);
  std::uint32_t stalled = 0;
  std::uint32_t deflection_iterations = 0;
  bool used_vertical_recovery = false;
  if (Length(remaining) < Constants::kTraceEpsilon) {
    remaining_ms = 0.0f;
  }

  while (Length(remaining) >= Constants::kTraceEpsilon) {
    if (callbacks_.cancelled && callbacks_.cancelled()) {
      result.status = MovementCollisionStatus::kCancelled;
      return result;
    }
    bool query_ok = false;
    const float remaining_distance = Length(remaining);
    const MovementCollisionTrace trace = SweepHull(body, remaining, &query_ok);
    if (!query_ok) {
      result.status = callbacks_.cancelled && callbacks_.cancelled()
                          ? MovementCollisionStatus::kCancelled
                          : MovementCollisionStatus::kQueryFailed;
      return result;
    }

    if (!cache_.complete) {
      body.position = start_position;
      result.status = MovementCollisionStatus::kBlocked;
      result.reset_requested = true;
      result.consumed_ms = step.duration_ms;
      return result;
    }
    if (!trace.hit) {
      body.position = Add(body.position, remaining);
      remaining = {};
      remaining_ms = 0.0f;
      break;
    }

    result.status = MovementCollisionStatus::kCollided;
    const C3Vector pre_contact_position = body.position;
    const C3Vector incoming_direction =
        Scale(remaining, 1.0f / remaining_distance);
    C3Vector contact_vector = Scale(incoming_direction, trace.distance);
    float contact_distance = trace.distance;
    const float contact_time_seconds =
        static_cast<float>(step.fall_time_ms) * 0.001f +
        (static_cast<float>(step.duration_ms) - remaining_ms) * 0.001f;
    const float remaining_duration_seconds = remaining_ms * 0.001f;
    const C3Vector pre_response_contact =
        Add(pre_contact_position, Scale(incoming_direction, contact_distance));
    const bool generated_falling_far =
        AirborneGeneratedContactTriggersFallingFar(
            body, step, pre_contact_position, incoming_direction,
            contact_distance, contact_time_seconds,
            trace.contacts.front());

    const float response_duration_seconds = AdjustAirborneContactTime(
        step, pre_contact_position, contact_time_seconds,
        remaining_duration_seconds, initial_horizontal,
        generated_falling_far, contact_vector,
        contact_distance);
    body.position = Add(pre_contact_position, contact_vector);
    const float progressed_ms = response_duration_seconds * 1000.0f;

    const float stall_threshold_ms = 0.5f;
    stalled = progressed_ms <= stall_threshold_ms
                  ? stalled + 1u
                  : 1u;
    remaining_ms = std::max(0.0f, remaining_ms - progressed_ms);
    remaining = Scale(
        incoming_direction,
        std::max(0.0f, remaining_distance - contact_distance));
    result.last_contact = trace.contacts.front();

    if (generated_falling_far) {
      result.falling_far_contact = true;
      result.state_snapshot_required = true;
      remaining = {};
      break;
    }

    if (falling_path &&
        AirborneLandingAdmission(body, pre_response_contact,
                                 trace.contacts.front())) {
      body.mode = MovementCollisionMode::kGround;
      result.landed = true;
      result.state_snapshot_required = true;
      remaining = {};
      break;
    }

    if (remaining_ms <= stall_threshold_ms) {
      result.state_snapshot_required = true;
      remaining = {};
      break;
    }

    if (stalled >= Constants::kStalledIterationLimit) {
      const float horizontal_remaining =
          std::sqrt(remaining.x * remaining.x + remaining.y * remaining.y);

      if (body.allow_vertical_stall_recovery && !used_vertical_recovery &&
          horizontal_remaining >= Constants::kTraceEpsilon) {

        remaining.x = 0.0f;
        remaining.y = 0.0f;
        current_speed = 0.0f;
        result.current_speed_update = current_speed;
        used_vertical_recovery = true;
        stalled = 0;
        continue;
      }

      if (falling_path) {
        body.mode = MovementCollisionMode::kGround;
        result.landed = true;
      }
      result.state_snapshot_required = true;
      result.status = MovementCollisionStatus::kBlocked;
      result.reset_requested = true;
      remaining = {};
      break;
    }

    const C3Vector deflection =
        Deflect(body, incoming_direction, trace.distance, remaining_distance,
                trace.contacts.front());
    C3Vector deflected = Add(remaining, deflection);
    if (deflection_iterations != 0u) {
      const float endpoint_x =
          body.position.x - start_position.x + deflected.x;
      const float endpoint_y =
          body.position.y - start_position.y + deflected.y;
      const float endpoint_horizontal_squared =
          endpoint_x * endpoint_x + endpoint_y * endpoint_y;
      if (initial_horizontal_limit * initial_horizontal_limit <
              endpoint_horizontal_squared &&
          vertical_recovery_limit * vertical_recovery_limit <
              endpoint_horizontal_squared) {

        if (falling_path) {
          body.mode = MovementCollisionMode::kGround;
          result.landed = true;
        }
        result.state_snapshot_required = true;
        result.status = MovementCollisionStatus::kBlocked;
        result.reset_requested = true;
        remaining = {};
        break;
      }
    }
    remaining = deflected;
    result.state_snapshot_required = true;
    ++deflection_iterations;
    const float new_horizontal_length =
        std::sqrt(remaining.x * remaining.x + remaining.y * remaining.y);
    if (new_horizontal_length >= Constants::kTiny) {
      const C3Vector new_horizontal_direction{
          remaining.x / new_horizontal_length,
          remaining.y / new_horizontal_length,
          0.0f};
      current_speed = std::max(
          0.0f,
          (horizontal_direction.x * new_horizontal_direction.x +
           horizontal_direction.y * new_horizontal_direction.y) *
              current_speed);
      horizontal_direction = new_horizontal_direction;
      result.current_speed_update = current_speed;
      result.horizontal_direction_update = horizontal_direction;
    } else {
      current_speed = 0.0f;
      result.current_speed_update = current_speed;
    }
    if (Length(remaining) < Constants::kTraceEpsilon) {
      result.status = MovementCollisionStatus::kBlocked;
      result.reset_requested = true;
      remaining = {};
      break;
    }
  }

  if (Length(remaining) >= Constants::kTraceEpsilon) {
    result.status = MovementCollisionStatus::kBlocked;
    result.reset_requested = true;
  }
  if (falling_path && step.vertical_speed != 0.0f &&
      step.directional_input) {
    const float horizontal_length =
        std::sqrt(step.initial_direction.x * step.initial_direction.x +
                  step.initial_direction.y * step.initial_direction.y);
    if (horizontal_length >= Constants::kTiny) {

      result.current_speed_update = step.movement_speed;
      result.horizontal_direction_update =
          C3Vector{step.initial_direction.x / horizontal_length,
                   step.initial_direction.y / horizontal_length, 0.0f};
    }
  }
  if (falling_path) {
    const float elapsed = std::clamp(
        static_cast<float>(step.duration_ms) - remaining_ms,
        0.0f, static_cast<float>(step.duration_ms));
    result.consumed_ms = static_cast<std::uint32_t>(std::round(elapsed));
  } else {
    result.consumed_ms = step.duration_ms;
  }
  return result;
}

MovementCollisionResult MovementCollisionSolver::SolveSpecial(
    MovementCollisionBody& body, const MovementCollisionStep& step) {
  MovementCollisionResult result;
  const C3Vector start_position = body.position;

  const float probe_distance = body.hover_height + body.hover_height;
  const C3Vector down{0.0f, 0.0f, -probe_distance};
  bool query_ok = false;
  const MovementCollisionTrace clearance = SweepHull(body, down, &query_ok);
  if (!query_ok) {
    result.status = callbacks_.cancelled && callbacks_.cancelled()
                        ? MovementCollisionStatus::kCancelled
                        : MovementCollisionStatus::kQueryFailed;
    return result;
  }

  if (!cache_.complete) {
    body.position = start_position;
    result.status = MovementCollisionStatus::kBlocked;
    result.reset_requested = true;
    result.consumed_ms = step.duration_ms;
    return result;
  }
  float measured_clearance = probe_distance;
  std::optional<MovementCollisionContact> clearance_contact;
  if (clearance.hit && !clearance.contacts.empty()) {
    measured_clearance = clearance.distance;
    clearance_contact = clearance.contacts.front();
    if (measured_clearance < body.hover_height &&
        !HoverWalkable(body, clearance_contact->surface_normal)) {
      measured_clearance = body.hover_height;
    }
  } else if (cache_.primary_facet_count == 0u) {

    body.position = start_position;
    result.consumed_ms = step.duration_ms;
    return result;
  } else {

    if (body.allow_special_fall_transition) {
      body.mode = MovementCollisionMode::kFalling;
      result = SolveAirborne(body, step);
      if (result.status == MovementCollisionStatus::kQueryFailed ||
          result.status == MovementCollisionStatus::kCancelled) {

        body.mode = MovementCollisionMode::kSpecial;
        return result;
      }
      result.transitioned_to_falling = true;
      return result;
    }
  }
  if (measured_clearance < Constants::kPlaneEpsilon) {
    measured_clearance = 0.0f;
  }

  const float maximum_correction =
      static_cast<float>(step.duration_ms) * 0.001f *
      Constants::kHoverVerticalSpeed;
  C3Vector remaining = step.displacement;
  remaining.z += std::clamp(body.hover_height - measured_clearance,
                            -maximum_correction, maximum_correction);

  float remaining_ms = static_cast<float>(step.duration_ms);
  std::uint32_t stalled = 0;
  const std::uint64_t iteration_limit =
      static_cast<std::uint64_t>(step.duration_ms) +
      Constants::kStalledIterationLimit;
  for (std::uint64_t iteration = 0;
       Length(remaining) >= Constants::kTraceEpsilon &&
       iteration < iteration_limit;
       ++iteration) {
    if (callbacks_.cancelled && callbacks_.cancelled()) {
      result.status = MovementCollisionStatus::kCancelled;
      return result;
    }

    bool movement_query_ok = false;
    const float remaining_distance = Length(remaining);
    const MovementCollisionTrace trace =
        SweepHull(body, remaining, &movement_query_ok);
    if (!movement_query_ok) {
      result.status = callbacks_.cancelled && callbacks_.cancelled()
                          ? MovementCollisionStatus::kCancelled
                          : MovementCollisionStatus::kQueryFailed;
      return result;
    }

    if (!cache_.complete) {
      body.position = start_position;
      result.status = MovementCollisionStatus::kBlocked;
      result.reset_requested = true;
      result.consumed_ms = step.duration_ms;
      return result;
    }
    if (!trace.hit || trace.contacts.empty()) {
      body.position = Add(body.position, remaining);
      remaining = {};
      break;
    }

    result.status = MovementCollisionStatus::kCollided;
    const float fraction = std::clamp(
        trace.distance / remaining_distance, 0.0f, 1.0f);
    body.position = Add(body.position, Scale(remaining, fraction));
    const float progressed_ms = remaining_ms * fraction;
    remaining_ms -= progressed_ms;
    remaining = Scale(remaining, 1.0f - fraction);
    result.last_contact = trace.contacts.front();

    stalled = progressed_ms + Constants::kTraceEpsilon <= 1.0f
                  ? stalled + 1u
                  : 1u;
    if (stalled >= Constants::kStalledIterationLimit) {
      result.status = MovementCollisionStatus::kBlocked;
      result.reset_requested = true;
      remaining = {};
      break;
    }
    if (remaining_ms < 1.0f) {
      if (std::fabs(remaining_ms) < Constants::kTraceEpsilon) {
        remaining = {};
        break;
      }
      result.status = MovementCollisionStatus::kBlocked;
      result.reset_requested = true;
      remaining = {};
      break;
    }

    const MovementCollisionContact& contact = trace.contacts.front();
    const C3Vector surface =
        NormalizeOr(contact.surface_normal, contact.normal);
    bool normal_response = HoverWalkable(body, surface);
    if (normal_response && body.stepping &&
        WithinFacetFootprintTolerance(body.position, contact.vertices)) {

      body.stepping = false;
    }
    if (!normal_response && surface.z < 0.0f &&
        ContactTouchesTop(body, contact)) {

      if (body.stepping) {
        body.stepping = false;
      }
      normal_response = -surface.z > Constants::kWalkableNormalZ;
    } else if (!normal_response) {

      normal_response = body.stepping;
    }

    if (normal_response) {
      remaining = HoverNormalResponse(remaining, contact);
    } else {
      remaining = Add(remaining, HoverWallResponse(remaining, contact));
    }
    if (Length(remaining) < Constants::kTraceEpsilon) {
      remaining = {};
      break;
    }
  }

  if (Length(remaining) >= Constants::kTraceEpsilon) {
    result.status = MovementCollisionStatus::kBlocked;
    result.reset_requested = true;
  }
  result.consumed_ms = step.duration_ms;
  if (!result.last_contact.has_value() && clearance_contact.has_value()) {
    result.last_contact = clearance_contact;
  }
  return result;
}

MovementCollisionResult MovementCollisionSolver::Solve(
    MovementCollisionBody& body, const MovementCollisionStep& step) {
  MovementCollisionResult invalid;
  if (step.duration_ms == 0) {
    return invalid;
  }
  if (!Finite(body.position) || !Finite(step.displacement) ||
      !Finite(body.radius) || !Finite(body.height) ||
      !Finite(body.step_height) || !Finite(body.hover_height) ||
      !Finite(step.ground_probe_distance) ||
      !Finite(step.movement_speed) ||
      !Finite(step.vertical_speed) || !Finite(step.fall_start_z) ||
      !Finite(step.initial_direction) ||
      body.radius < 0.0f || body.height < 0.0f || body.step_height < 0.0f ||
      body.hover_height < 0.0f ||
      step.ground_probe_distance < 0.0f ||
      (body.parent.has_value() &&
       (!Finite(body.parent->parent_to_world) ||
        !Finite(body.parent->world_to_parent) ||
        !Finite(body.parent->parent_origin_world)))) {
    invalid.status = MovementCollisionStatus::kInvalidInput;
    return invalid;
  }

  C3Vector world_target = Add(body.position, step.displacement);
  if (body.parent.has_value()) {
    world_target = body.parent->ToWorldPoint(world_target);
  }
  const float edge_margin = body.radius + Constants::kWorldEdgePadding;
  const float minimum_coordinate = -Constants::kWorldHalfSize + edge_margin;
  const float maximum_coordinate = Constants::kWorldHalfSize - edge_margin;
  if (world_target.x > maximum_coordinate ||
      world_target.x <= minimum_coordinate ||
      world_target.y > maximum_coordinate ||
      world_target.y <= minimum_coordinate) {

    invalid.status = MovementCollisionStatus::kBlocked;
    invalid.consumed_ms = step.duration_ms;
    return invalid;
  }
  if (!IsBound()) {
    invalid.status = MovementCollisionStatus::kQueryFailed;
    return invalid;
  }

  MovementCollisionBody working = body;
  const MovementCollisionMode previous_mode = body.mode;
  MovementCollisionResult result;
  switch (working.mode) {
    case MovementCollisionMode::kGround:
      result = SolveGround(working, step);
      break;
    case MovementCollisionMode::kSimpleCollision:
    case MovementCollisionMode::kFlying:
    case MovementCollisionMode::kSwimming:

      result = SolveSimpleCollision(working, step);
      break;
    case MovementCollisionMode::kSpecial:
      result = SolveSpecial(working, step);
      break;
    case MovementCollisionMode::kFalling:
      result = SolveAirborne(working, step);
      break;
  }

  if (result.status == MovementCollisionStatus::kQueryFailed ||
      result.status == MovementCollisionStatus::kCancelled ||
      result.status == MovementCollisionStatus::kInvalidInput) {
    return result;
  }

  body = std::move(working);
  if (result.last_contact.has_value() && callbacks_.contact) {
    callbacks_.contact(*result.last_contact);
  }
  if (previous_mode != body.mode && callbacks_.mode_changed) {
    callbacks_.mode_changed(previous_mode, body.mode);
  }
  if (result.reset_requested && callbacks_.reset_movement) {
    callbacks_.reset_movement();
  }
  return result;
}

}

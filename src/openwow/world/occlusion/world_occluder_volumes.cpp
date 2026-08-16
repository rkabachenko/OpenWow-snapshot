#include "openwow/world/occlusion/world_occluder_volumes.h"

#include "openwow/world/occlusion/retail_occluders.h"

#include <cmath>

namespace openwow::world {

namespace {

constexpr float kRetailOccluderClipBand = 1.0e-4f;

constexpr float kRetailOccluderMinimumNormalLengthSquared = 1.0e-4f;

constexpr float kRetailOccluderApertureDepthEpsilon = 1.0e-06f;

[[nodiscard]] float Dot3(const std::span<const float, 3> lhs,
                         const std::array<float, 3>& rhs) {
  return lhs[0] * rhs[0] + lhs[1] * rhs[1] + lhs[2] * rhs[2];
}

struct OccluderBounds {
  std::array<float, 6> aabb;
};

[[nodiscard]] const std::array<OccluderBounds,
                               kRetailWorldOccluders.size()>&
RetailOccluderBounds() {
  static const auto bounds = [] {
    std::array<OccluderBounds, kRetailWorldOccluders.size()> derived{};
    for (std::size_t i = 0; i < kRetailWorldOccluders.size(); ++i) {
      const RetailWorldOccluder& record = kRetailWorldOccluders[i];
      auto& aabb = derived[i].aabb;
      aabb = {3.4028235e38f, 3.4028235e38f, 3.4028235e38f,
              -3.4028235e38f, -3.4028235e38f, -3.4028235e38f};
      for (std::uint32_t v = 0; v < record.vertex_count; ++v) {
        const auto& vertex =
            kRetailWorldOccluderVertices[record.first_vertex + v];
        for (std::size_t axis = 0; axis < 3u; ++axis) {
          aabb[axis] = std::min(aabb[axis], vertex[axis]);
          aabb[axis + 3u] = std::max(aabb[axis + 3u], vertex[axis]);
        }
      }
    }
    return derived;
  }();
  return bounds;
}

}

void WorldOccluderVolumes::Clear() {
  planes_.clear();
  volumes_.clear();
}

void WorldOccluderVolumes::Rebuild(const std::uint32_t map_id,
                                   const std::span<const float, 3> eye,
                                   const std::span<const float, 3> view_forward,
                                   const float terrain_aperture_depth,
                                   const Frustum& frustum,
                                   const bool second_pass_only) {
  Clear();
  const auto& bounds = RetailOccluderBounds();
  for (std::size_t i = 0; i < kRetailWorldOccluders.size(); ++i) {
    const RetailWorldOccluder& record = kRetailWorldOccluders[i];

    if (record.map_id != map_id || record.vertex_count == 0u) {
      continue;
    }
    if (second_pass_only &&
        (record.flags & kRetailOccluderSecondPassFlag) == 0u) {
      continue;
    }
    const auto& aabb = bounds[i].aabb;
    if (!frustum.TestAABB(aabb[0], aabb[1], aabb[2], aabb[3], aabb[4],
                          aabb[5])) {
      continue;
    }
    AddVolume(eye,
              std::span<const std::array<float, 3>>(
                  kRetailWorldOccluderVertices.data() + record.first_vertex,
                  record.vertex_count),
              (record.flags & kRetailOccluderCameraApexFlag) != 0u,
              view_forward, terrain_aperture_depth);
  }
}

void WorldOccluderVolumes::AddVolume(
    const std::span<const float, 3> eye,
    const std::span<const std::array<float, 3>> polygon,
    const bool camera_apex_only, const std::span<const float, 3> view_forward,
    const float terrain_aperture_depth) {

  const bool trim_to_aperture =
      !camera_apex_only &&
      terrain_aperture_depth > kRetailOccluderApertureDepthEpsilon;

  if (polygon.size() < 3u) {
    return;
  }
  std::span<const std::array<float, 3>> source = polygon;
  if (trim_to_aperture) {

    const std::array<float, 3> anchor{
        eye[0] + terrain_aperture_depth * view_forward[0],
        eye[1] + terrain_aperture_depth * view_forward[1],
        eye[2] + terrain_aperture_depth * view_forward[2]};
    const float plane_d = -Dot3(view_forward, anchor);
    clipped_.clear();
    const std::size_t count = polygon.size();
    for (std::size_t i = 0; i < count; ++i) {
      const auto& current = polygon[i];
      const auto& next = polygon[(i + 1u) % count];
      const float current_distance = Dot3(view_forward, current) + plane_d;
      const float next_distance = Dot3(view_forward, next) + plane_d;
      if (current_distance >= -kRetailOccluderClipBand) {
        clipped_.push_back(current);
      }
      const bool crosses =
          (current_distance > kRetailOccluderClipBand &&
           next_distance < -kRetailOccluderClipBand) ||
          (current_distance < -kRetailOccluderClipBand &&
           next_distance > kRetailOccluderClipBand);
      if (crosses) {
        const float t = current_distance / (current_distance - next_distance);
        clipped_.push_back({current[0] + (next[0] - current[0]) * t,
                            current[1] + (next[1] - current[1]) * t,
                            current[2] + (next[2] - current[2]) * t});
      }
    }
    if (clipped_.size() < 3u) {
      return;
    }
    source = clipped_;
  }

  const auto first = static_cast<std::uint32_t>(planes_.size());
  const std::size_t count = source.size();
  for (std::size_t i = 0; i < count; ++i) {
    const auto& vertex = source[i];
    const auto& next = source[(i + 1u) % count];
    const float ax = eye[0] - vertex[0];
    const float ay = eye[1] - vertex[1];
    const float az = eye[2] - vertex[2];
    const float bx = next[0] - vertex[0];
    const float by = next[1] - vertex[1];
    const float bz = next[2] - vertex[2];
    const float nx = az * by - ay * bz;
    const float ny = ax * bz - az * bx;
    const float nz = ay * bx - ax * by;
    const float length_squared = nx * nx + ny * ny + nz * nz;
    if (trim_to_aperture &&
        length_squared <= kRetailOccluderMinimumNormalLengthSquared) {
      continue;
    }
    const float inverse_length = 1.0f / std::sqrt(length_squared);
    const float px = nx * inverse_length;
    const float py = ny * inverse_length;
    const float pz = nz * inverse_length;
    planes_.push_back(Vec4{px, py, pz,
                           -(px * vertex[0] + py * vertex[1] +
                             pz * vertex[2])});
  }

  if (planes_.size() == first) {
    return;
  }

  {
    const auto& v0 = polygon[0];
    const auto& v1 = polygon[1];
    const auto& v2 = polygon[2];
    const float ax = v1[0] - v0[0];
    const float ay = v1[1] - v0[1];
    const float az = v1[2] - v0[2];
    const float bx = v2[0] - v0[0];
    const float by = v2[1] - v0[1];
    const float bz = v2[2] - v0[2];
    const float nx = bz * ay - by * az;
    const float ny = bx * az - ax * bz;
    const float nz = by * ax - bx * ay;
    const float inverse_length =
        1.0f / std::sqrt(nx * nx + ny * ny + nz * nz);
    const float px = nx * inverse_length;
    const float py = ny * inverse_length;
    const float pz = nz * inverse_length;
    planes_.push_back(
        Vec4{px, py, pz,
             -(px * v0[0] + py * v0[1] + pz * v0[2])});
  }

  {
    const Vec4& cap = planes_.back();
    if (cap[0] * eye[0] + cap[1] * eye[1] + cap[2] * eye[2] + cap[3] < 0.0f) {
      for (std::size_t i = first; i < planes_.size(); ++i) {
        for (float& component : planes_[i]) {
          component = -component;
        }
      }
    }
  }

  volumes_.push_back(
      Volume{first, static_cast<std::uint32_t>(planes_.size()) - first});
}

bool WorldOccluderVolumes::IsPolygonOccluded(
    const std::span<const std::array<float, 3>> polygon) const {

  for (const Volume& volume : volumes_) {
    if (volume.plane_count == 0u) {
      return true;
    }
    bool inside_every_plane = true;
    for (std::uint32_t p = 0; p < volume.plane_count; ++p) {
      const Vec4& plane = planes_[volume.first_plane + p];
      for (const auto& vertex : polygon) {
        if (plane[0] * vertex[0] + plane[1] * vertex[1] +
                plane[2] * vertex[2] + plane[3] >
            0.0f) {
          inside_every_plane = false;
          break;
        }
      }
      if (!inside_every_plane) {
        break;
      }
    }
    if (inside_every_plane) {
      return true;
    }
  }
  return false;
}

bool WorldOccluderVolumes::IsSphereOccluded(
    const std::span<const float, 4> sphere) const {

  for (const Volume& volume : volumes_) {
    if (volume.plane_count == 0u) {
      return true;
    }
    bool inside_every_plane = true;
    for (std::uint32_t p = 0; p < volume.plane_count; ++p) {
      const Vec4& plane = planes_[volume.first_plane + p];
      if (plane[0] * sphere[0] + plane[1] * sphere[1] + plane[2] * sphere[2] +
              plane[3] + sphere[3] >
          0.0f) {
        inside_every_plane = false;
        break;
      }
    }
    if (inside_every_plane) {
      return true;
    }
  }
  return false;
}

}

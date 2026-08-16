
#include "openwow/world/terrain/terrain_occlusion.h"

#include <algorithm>
#include <cmath>

namespace openwow::world {

bool TerrainOcclusion::HasLineOfSight(float ax, float ay, float az,
                                       float bx, float by, float bz) const {
  if (!heightmap_) return true;

  const float dx = bx - ax;
  const float dy = by - ay;
  const float dz = bz - az;
  const float horizontal_dist = std::sqrt(dx * dx + dy * dy);

  if (horizontal_dist < step_size_) return true;

  const int steps = std::max(1, static_cast<int>(horizontal_dist / step_size_));
  const float inv_steps = 1.0f / static_cast<float>(steps);

  for (int i = 1; i < steps; ++i) {
    const float t = static_cast<float>(i) * inv_steps;
    const float sx = ax + dx * t;
    const float sy = ay + dy * t;
    const float sz = az + dz * t;

    auto terrain_h = heightmap_->GetHeight(sx, sy);
    if (terrain_h.has_value()) {

      if (*terrain_h > sz + height_margin_) {
        return false;
      }
    }
  }

  return true;
}

bool TerrainOcclusion::IsOccludedByTerrain(
    float view_x, float view_y, float view_z,
    float target_x, float target_y, float target_z) const {
  return !HasLineOfSight(view_x, view_y, view_z,
                         target_x, target_y, target_z);
}

std::optional<TerrainIntersection> TerrainOcclusion::RaycastTerrain(
    float ox, float oy, float oz,
    float dx, float dy, float dz,
    float max_dist) const {
  if (!heightmap_) return std::nullopt;

  const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
  if (len < 1e-6f) return std::nullopt;
  const float inv_len = 1.0f / len;
  const float ndx = dx * inv_len;
  const float ndy = dy * inv_len;
  const float ndz = dz * inv_len;

  const float horiz_len = std::sqrt(ndx * ndx + ndy * ndy);
  if (horiz_len < 1e-6f) {

    auto h = heightmap_->GetHeight(ox, oy);
    if (h.has_value()) {

      const float end_z = oz + ndz * max_dist;
      const float z_min = std::min(oz, end_z);
      const float z_max = std::max(oz, end_z);
      if (*h >= z_min && *h <= z_max) {
        TerrainIntersection hit;
        hit.x = ox;
        hit.y = oy;
        hit.z = *h;
        hit.distance = std::abs(*h - oz);
        return hit;
      }
    }
    return std::nullopt;
  }

  float dist = step_size_;

  while (dist <= max_dist) {
    const float cx = ox + ndx * dist;
    const float cy = oy + ndy * dist;
    const float cz = oz + ndz * dist;

    auto terrain_h = heightmap_->GetHeight(cx, cy);
    if (terrain_h.has_value()) {

      if (cz <= *terrain_h + height_margin_) {
        TerrainIntersection hit;
        hit.x = cx;
        hit.y = cy;
        hit.z = *terrain_h;
        hit.distance = dist;
        return hit;
      }
    }
    dist += step_size_;
  }

  return std::nullopt;
}

std::optional<float> TerrainOcclusion::GetTerrainHeight(float x, float y) const {
  if (!heightmap_) return std::nullopt;
  return heightmap_->GetHeight(x, y);
}

}

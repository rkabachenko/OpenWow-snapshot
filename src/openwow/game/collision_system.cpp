#include "openwow/game/collision_system.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace openwow::game {

CollisionSystem::CollisionSystem() = default;

void CollisionSystem::SetHeightmapProvider(data::map::HeightmapQuery* provider) {
  heightmap_ = provider;
}

void CollisionSystem::SetWaterHeightProvider(WaterHeightFn fn) {
  water_height_fn_ = std::move(fn);
}

void CollisionSystem::SetIndoorCheckProvider(IndoorCheckFn fn) {
  indoor_check_fn_ = std::move(fn);
}

RaycastResult CollisionSystem::Raycast(const Vec3& origin,
                                       const Vec3& direction,
                                       float maxDist) const {
  RaycastResult best;
  best.distance = maxDist;

  auto terrain = RaycastTerrain(origin, direction, maxDist);
  if (terrain.hit && terrain.distance < best.distance) {
    best = terrain;
  }

  const std::scoped_lock lock(geometry_mutex_);
  for (const auto& [handle, geom] : static_geometry_) {
    if (!RayAABBIntersect(origin, direction, geom.bounds, best.distance))
      continue;

    for (const auto& tri : geom.triangles) {
      float t = 0.0f;
      Vec3 normal;
      if (RayTriangleIntersect(origin, direction, tri, t, normal)) {
        if (t > 0.0f && t < best.distance) {
          best.hit = true;
          best.distance = t;
          best.hitPoint = {origin.x + direction.x * t,
                          origin.y + direction.y * t,
                          origin.z + direction.z * t};
          best.hitNormal = normal;
          best.hitType = HitType::WMO;
        }
      }
    }
  }

  return best;
}

RaycastResult CollisionSystem::RaycastTerrain(const Vec3& origin,
                                              const Vec3& direction,
                                              float maxDist) const {
  return StepTerrainRaycast(origin, direction, maxDist);
}

RaycastResult CollisionSystem::StepTerrainRaycast(const Vec3& origin,
                                                  const Vec3& direction,
                                                  float maxDist) const {
  RaycastResult result;

  if (!heightmap_) return result;

  constexpr float kStepSize = 1.0f;
  constexpr int kMaxSteps = 2000;
  constexpr int kBinarySteps = 10;

  float prevT = 0.0f;
  float prevDelta = 0.0f;
  bool hasPrev = false;

  int numSteps = std::min(kMaxSteps, static_cast<int>(maxDist / kStepSize) + 1);

  for (int i = 0; i <= numSteps; ++i) {
    float t = std::min(static_cast<float>(i) * kStepSize, maxDist);
    float px = origin.x + direction.x * t;
    float py = origin.y + direction.y * t;
    float pz = origin.z + direction.z * t;

    auto groundZ = heightmap_->GetHeight(px, py);
    if (!groundZ) {
      hasPrev = false;
      continue;
    }

    float delta = pz - *groundZ;

    if (hasPrev && prevDelta > 0.0f && delta <= 0.0f) {

      float lo = prevT;
      float hi = t;
      for (int b = 0; b < kBinarySteps; ++b) {
        float mid = (lo + hi) * 0.5f;
        float mx = origin.x + direction.x * mid;
        float my = origin.y + direction.y * mid;
        float mz = origin.z + direction.z * mid;
        auto gz = heightmap_->GetHeight(mx, my);
        if (!gz) { lo = mid; continue; }
        if (mz > *gz)
          lo = mid;
        else
          hi = mid;
      }

      float hitT = (lo + hi) * 0.5f;
      result.hit = true;
      result.distance = hitT;
      result.hitPoint = {origin.x + direction.x * hitT,
                        origin.y + direction.y * hitT,
                        origin.z + direction.z * hitT};
      result.hitType = HitType::Terrain;

      auto normal = heightmap_->GetNormal(result.hitPoint.x, result.hitPoint.y);
      if (normal) {
        result.hitNormal = {normal->x, normal->y, normal->z};
      }
      return result;
    }

    prevT = t;
    prevDelta = delta;
    hasPrev = true;
  }

  return result;
}

std::optional<float> CollisionSystem::GetGroundHeight(float x, float y) const {
  if (!heightmap_) return std::nullopt;
  return heightmap_->GetHeight(x, y);
}

bool CollisionSystem::IsOnGround(float x, float y, float z,
                                 float tolerance) const {
  auto ground = GetGroundHeight(x, y);
  if (!ground) return false;
  return std::abs(z - *ground) <= tolerance;
}

bool CollisionSystem::IsInWater(float x, float y, float z) const {
  auto waterH = GetWaterHeight(x, y);
  if (!waterH) return false;
  return z < *waterH;
}

std::optional<float> CollisionSystem::GetWaterHeight(float x, float y) const {
  if (water_height_fn_) return water_height_fn_(x, y);
  return std::nullopt;
}

bool CollisionSystem::IsIndoors(float x, float y, float z) const {
  if (indoor_check_fn_) return indoor_check_fn_(x, y, z);
  return false;
}

bool CollisionSystem::LineOfSight(const Vec3& from, const Vec3& to) const {
  Vec3 dir = to - from;
  float dist = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
  if (dist < 0.001f) return true;

  Vec3 ndir = {dir.x / dist, dir.y / dist, dir.z / dist};
  auto result = Raycast(from, ndir, dist);

  return !result.hit || result.distance >= dist - 0.1f;
}

RaycastResult CollisionSystem::SweepSphere(const Vec3& origin,
                                           const Vec3& direction,
                                           float radius,
                                           float maxDist) const {

  auto result = Raycast(origin, direction, maxDist);
  if (result.hit && result.distance > radius) {
    result.distance -= radius;
    result.hitPoint = {origin.x + direction.x * result.distance,
                      origin.y + direction.y * result.distance,
                      origin.z + direction.z * result.distance};
  }
  return result;
}

std::optional<float> CollisionSystem::GetCeilingHeight(float x, float y,
                                                       float z) const {

  Vec3 origin{x, y, z + 0.5f};
  Vec3 up{0.0f, 0.0f, 1.0f};
  auto result = Raycast(origin, up, 100.0f);
  if (result.hit) return result.hitPoint.z;
  return std::nullopt;
}

GeometryHandle CollisionSystem::RegisterStaticGeometry(
    const AABB& bounds, const std::vector<Triangle>& triangles) {
  const std::scoped_lock lock(geometry_mutex_);
  GeometryHandle h = next_handle_++;
  static_geometry_[h] = StaticGeometry{bounds, triangles};
  ++geometry_revision_;
  return h;
}

void CollisionSystem::UnregisterGeometry(GeometryHandle handle) {
  const std::scoped_lock lock(geometry_mutex_);
  if (static_geometry_.erase(handle) != 0u) {
    ++geometry_revision_;
  }
}

std::optional<MovementCollisionFacetBatch>
CollisionSystem::QueryMovementFacets(
    const CollisionAabb& bounds, const std::uint32_t ,
    const MovementCollisionLayer layer) const {
  MovementCollisionFacetBatch batch;
  const std::scoped_lock lock(geometry_mutex_);
  batch.revision = geometry_revision_;
  if (layer == MovementCollisionLayer::kSecondary) {
    return batch;
  }

  std::vector<GeometryHandle> handles;
  handles.reserve(static_geometry_.size());
  for (const auto& [handle, geometry] : static_geometry_) {
    const bool overlaps =
        geometry.bounds.min.x <= bounds.max.x &&
        geometry.bounds.max.x >= bounds.min.x &&
        geometry.bounds.min.y <= bounds.max.y &&
        geometry.bounds.max.y >= bounds.min.y &&
        geometry.bounds.min.z <= bounds.max.z &&
        geometry.bounds.max.z >= bounds.min.z;
    if (overlaps) {
      handles.push_back(handle);
    }
  }
  std::sort(handles.begin(), handles.end());

  for (const GeometryHandle handle : handles) {
    const auto found = static_geometry_.find(handle);
    if (found == static_geometry_.end()) {
      continue;
    }
    const auto& triangles = found->second.triangles;
    for (std::size_t index = 0; index < triangles.size(); ++index) {
      const Triangle& triangle = triangles[index];
      const float min_x = std::min({triangle.v0.x, triangle.v1.x, triangle.v2.x});
      const float min_y = std::min({triangle.v0.y, triangle.v1.y, triangle.v2.y});
      const float min_z = std::min({triangle.v0.z, triangle.v1.z, triangle.v2.z});
      const float max_x = std::max({triangle.v0.x, triangle.v1.x, triangle.v2.x});
      const float max_y = std::max({triangle.v0.y, triangle.v1.y, triangle.v2.y});
      const float max_z = std::max({triangle.v0.z, triangle.v1.z, triangle.v2.z});
      if (min_x > bounds.max.x || max_x < bounds.min.x ||
          min_y > bounds.max.y || max_y < bounds.min.y ||
          min_z > bounds.max.z || max_z < bounds.min.z) {
        continue;
      }

      const Vec3 edge_a = triangle.v1 - triangle.v0;
      const Vec3 edge_b = triangle.v2 - triangle.v0;
      Vec3 normal{edge_a.y * edge_b.z - edge_a.z * edge_b.y,
                  edge_a.z * edge_b.x - edge_a.x * edge_b.z,
                  edge_a.x * edge_b.y - edge_a.y * edge_b.x};
      const float normal_length =
          std::sqrt(normal.x * normal.x + normal.y * normal.y +
                    normal.z * normal.z);
      if (normal_length < MovementCollisionConstants::kTiny) {
        continue;
      }
      normal = normal * (1.0f / normal_length);
      MovementCollisionFacet facet;
      facet.vertices[0] = {triangle.v0.x, triangle.v0.y, triangle.v0.z};
      facet.vertices[1] = {triangle.v1.x, triangle.v1.y, triangle.v1.z};
      facet.vertices[2] = {triangle.v2.x, triangle.v2.y, triangle.v2.z};
      facet.normal = {normal.x, normal.y, normal.z};
      facet.owner_id = handle;
      facet.facet_id = static_cast<std::uint64_t>(index);
      facet.plane_offset =
          -(facet.normal.x * facet.vertices[0].x +
            facet.normal.y * facet.vertices[0].y +
            facet.normal.z * facet.vertices[0].z);
      batch.facets.push_back(facet);
    }
  }
  return batch;
}

std::uint64_t CollisionSystem::MovementFacetRevision() const {
  const std::scoped_lock lock(geometry_mutex_);
  return geometry_revision_;
}

float CollisionSystem::GetTerrainSlope(float x, float y) const {
  if (!heightmap_) return 0.0f;
  auto slope = heightmap_->GetSlope(x, y);
  return slope.value_or(0.0f);
}

bool CollisionSystem::RayTriangleIntersect(const Vec3& origin, const Vec3& dir,
                                           const Triangle& tri,
                                           float& t, Vec3& normal) {
  constexpr float kEpsilon = 1e-6f;

  Vec3 e1 = tri.v1 - tri.v0;
  Vec3 e2 = tri.v2 - tri.v0;

  Vec3 h{dir.y * e2.z - dir.z * e2.y,
         dir.z * e2.x - dir.x * e2.z,
         dir.x * e2.y - dir.y * e2.x};

  float a = e1.x * h.x + e1.y * h.y + e1.z * h.z;
  if (a > -kEpsilon && a < kEpsilon) return false;

  float f = 1.0f / a;
  Vec3 s = origin - tri.v0;
  float u = f * (s.x * h.x + s.y * h.y + s.z * h.z);
  if (u < 0.0f || u > 1.0f) return false;

  Vec3 q{s.y * e1.z - s.z * e1.y,
         s.z * e1.x - s.x * e1.z,
         s.x * e1.y - s.y * e1.x};

  float v = f * (dir.x * q.x + dir.y * q.y + dir.z * q.z);
  if (v < 0.0f || u + v > 1.0f) return false;

  t = f * (e2.x * q.x + e2.y * q.y + e2.z * q.z);
  if (t < kEpsilon) return false;

  normal = {e1.y * e2.z - e1.z * e2.y,
            e1.z * e2.x - e1.x * e2.z,
            e1.x * e2.y - e1.y * e2.x};
  float len = std::sqrt(normal.x * normal.x + normal.y * normal.y +
                        normal.z * normal.z);
  if (len > kEpsilon) {
    normal.x /= len;
    normal.y /= len;
    normal.z /= len;
  }

  return true;
}

bool CollisionSystem::RayAABBIntersect(const Vec3& origin, const Vec3& dir,
                                       const AABB& box, float maxDist) {
  float tmin = 0.0f;
  float tmax = maxDist;

  for (int i = 0; i < 3; ++i) {
    float o = (&origin.x)[i];
    float d = (&dir.x)[i];
    float bmin = (&box.min.x)[i];
    float bmax = (&box.max.x)[i];

    if (std::abs(d) < 1e-8f) {
      if (o < bmin || o > bmax) return false;
    } else {
      float t1 = (bmin - o) / d;
      float t2 = (bmax - o) / d;
      if (t1 > t2) std::swap(t1, t2);
      tmin = std::max(tmin, t1);
      tmax = std::min(tmax, t2);
      if (tmin > tmax) return false;
    }
  }
  return true;
}

}

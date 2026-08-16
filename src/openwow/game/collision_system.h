#pragma once

#include "openwow/data/map/heightmap_query.h"
#include "openwow/game/vec3.h"
#include "openwow/game/ground_walk.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace openwow::game {

enum class HitType : uint8_t {
  None    = 0,
  Terrain = 1,
  WMO     = 2,
  M2      = 3,
  Water   = 4,
};

struct RaycastResult {
  bool hit{false};
  Vec3 hitPoint{};
  Vec3 hitNormal{0.0f, 0.0f, 1.0f};
  float distance{0.0f};
  HitType hitType{HitType::None};
};

struct AABB {
  Vec3 min{};
  Vec3 max{};

  [[nodiscard]] bool Contains(const Vec3& p) const {
    return p.x >= min.x && p.x <= max.x &&
           p.y >= min.y && p.y <= max.y &&
           p.z >= min.z && p.z <= max.z;
  }

  [[nodiscard]] Vec3 Center() const {
    return {(min.x + max.x) * 0.5f,
            (min.y + max.y) * 0.5f,
            (min.z + max.z) * 0.5f};
  }
};

struct Triangle {
  Vec3 v0, v1, v2;
};

using GeometryHandle = uint32_t;

using WaterHeightFn = std::function<std::optional<float>(float x, float y)>;
using IndoorCheckFn = std::function<bool(float x, float y, float z)>;

class CollisionSystem {
 public:
  CollisionSystem();

  void SetHeightmapProvider(data::map::HeightmapQuery* provider);

  void SetWaterHeightProvider(WaterHeightFn fn);

  void SetIndoorCheckProvider(IndoorCheckFn fn);

  [[nodiscard]] RaycastResult Raycast(const Vec3& origin,
                                      const Vec3& direction,
                                      float maxDist = 1000.0f) const;

  [[nodiscard]] RaycastResult RaycastTerrain(const Vec3& origin,
                                             const Vec3& direction,
                                             float maxDist = 1000.0f) const;

  [[nodiscard]] std::optional<float> GetGroundHeight(float x, float y) const;

  [[nodiscard]] bool IsOnGround(float x, float y, float z,
                                float tolerance = 0.5f) const;

  [[nodiscard]] bool IsInWater(float x, float y, float z) const;

  [[nodiscard]] std::optional<float> GetWaterHeight(float x, float y) const;

  [[nodiscard]] bool IsIndoors(float x, float y, float z) const;

  [[nodiscard]] bool LineOfSight(const Vec3& from, const Vec3& to) const;

  [[nodiscard]] RaycastResult SweepSphere(const Vec3& origin,
                                          const Vec3& direction,
                                          float radius,
                                          float maxDist = 1000.0f) const;

  [[nodiscard]] std::optional<float> GetCeilingHeight(float x, float y,
                                                      float z) const;

  GeometryHandle RegisterStaticGeometry(const AABB& bounds,
                                        const std::vector<Triangle>& triangles);

  void UnregisterGeometry(GeometryHandle handle);

  [[nodiscard]] std::optional<MovementCollisionFacetBatch>
  QueryMovementFacets(const CollisionAabb& bounds,
                      std::uint32_t collision_mask,
                      MovementCollisionLayer layer) const;
  [[nodiscard]] std::uint64_t MovementFacetRevision() const;

  [[nodiscard]] float GetTerrainSlope(float x, float y) const;

 private:
  data::map::HeightmapQuery* heightmap_{nullptr};
  WaterHeightFn water_height_fn_;
  IndoorCheckFn indoor_check_fn_;

  struct StaticGeometry {
    AABB bounds;
    std::vector<Triangle> triangles;
  };

  uint32_t next_handle_{1};
  std::unordered_map<GeometryHandle, StaticGeometry> static_geometry_;
  std::uint64_t geometry_revision_{0};
  mutable std::mutex geometry_mutex_;

  static bool RayTriangleIntersect(const Vec3& origin, const Vec3& dir,
                                   const Triangle& tri,
                                   float& t, Vec3& normal);

  static bool RayAABBIntersect(const Vec3& origin, const Vec3& dir,
                               const AABB& box, float maxDist);

  RaycastResult StepTerrainRaycast(const Vec3& origin, const Vec3& direction,
                                   float maxDist) const;
};

}

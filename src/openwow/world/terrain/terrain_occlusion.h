#pragma once

#include "openwow/data/map/heightmap_query.h"

#include <cstdint>
#include <optional>

namespace openwow::world {

struct TerrainIntersection {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float distance = 0.0f;
};

class TerrainOcclusion {
 public:
  TerrainOcclusion() = default;

  void SetHeightmap(const data::map::HeightmapQuery* heightmap) {
    heightmap_ = heightmap;
  }

  [[nodiscard]] bool HasLineOfSight(float ax, float ay, float az,
                                     float bx, float by, float bz) const;

  [[nodiscard]] std::optional<TerrainIntersection> RaycastTerrain(
      float ox, float oy, float oz,
      float dx, float dy, float dz,
      float max_dist) const;

  [[nodiscard]] bool IsOccludedByTerrain(
      float view_x, float view_y, float view_z,
      float target_x, float target_y, float target_z) const;

  [[nodiscard]] std::optional<float> GetTerrainHeight(float x, float y) const;

  void SetStepSize(float yards) { step_size_ = yards; }
  [[nodiscard]] float GetStepSize() const { return step_size_; }

  void SetHeightMargin(float yards) { height_margin_ = yards; }
  [[nodiscard]] float GetHeightMargin() const { return height_margin_; }

 private:
  const data::map::HeightmapQuery* heightmap_ = nullptr;

  float step_size_ = 4.0f;

  float height_margin_ = 1.0f;
};

}


#pragma once

#include "openwow/game/ground_walk.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace openwow::game {

std::int32_t SimplifyMovePath(const C3Vector& start,
                              std::vector<C3Vector>& waypoints,
                              const float* transform_4x4 = nullptr);

inline bool ShouldSuppressMountedSpecialForLiquidDepth(
    float current_z, float surface_z, float collision_height,
    bool has_valid_surface) {
  if (!has_valid_surface) {
    return false;
  }

  float height_diff = surface_z - current_z;
  return collision_height * 0.5f < height_diff;
}

}

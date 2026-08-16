#pragma once

#include <cstdint>

namespace openwow::render {

struct M2AnimationSpatialBounds {
  float bounds_min[3] = {};
  float bounds_max[3] = {};
  float bounds_center[3] = {};
  float bounds_radius = 0.0f;

  void Clear() noexcept {
    bounds_min[0] = 0.0f;
    bounds_min[1] = 0.0f;
    bounds_min[2] = 0.0f;
    bounds_max[0] = 0.0f;
    bounds_max[1] = 0.0f;
    bounds_max[2] = 0.0f;
    bounds_center[0] = 0.0f;
    bounds_center[1] = 0.0f;
    bounds_center[2] = 0.0f;
    bounds_radius = 0.0f;
  }
};
static_assert(sizeof(M2AnimationSpatialBounds) == 40,
              "M2AnimationSpatialBounds must stay layout-compatible.");

}

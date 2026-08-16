#pragma once

#include "openwow/game/collision_polygon.h"
#include "openwow/game/collision_cell_view.h"

#include <cmath>
#include <cstdint>

namespace openwow::game {

inline constexpr float kMinUpwardNormalZ = 0.017452406f;

float* ComputeAverageGroundNormal(float* outNormal, const float* planes,
                                  uint32_t numPlanes);

}

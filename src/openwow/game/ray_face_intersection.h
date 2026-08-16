#pragma once

#include "openwow/game/collision_polygon.h"

#include <cmath>
#include <cstdint>

namespace openwow::game {

inline constexpr float kRayDotEpsilon = 0.00000023841858f;

inline constexpr float kBehindEpsilon = -0.00000095367432f;

inline constexpr float kLooseClipThreshold = -0.027777778f;

inline constexpr int kBoundingPlaneCount = 9;

int TestRayAgainstClippedFace(const float* rayDir,
                              ClippedPolygon& poly,
                              const float* planes,
                              int targetPlane,
                              float* inOutMinDist);

}

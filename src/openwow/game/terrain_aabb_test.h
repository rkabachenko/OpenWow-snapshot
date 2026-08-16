#pragma once

#include "openwow/game/collision_polygon.h"
#include "openwow/game/collision_cell_view.h"

#include <cstdint>
#include <cstring>

namespace openwow::game {

int AnyTerrainTriangleIntersectsAABB(const float* aabb);

}

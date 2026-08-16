
#include "openwow/game/terrain_aabb_test.h"

#include <cstring>

namespace openwow::game {

static constexpr uint32_t kCellStride      = 52;
static constexpr uint32_t kCellVerticesOff = 16;
static constexpr uint32_t kCellVerticesSize = 36;

int AnyTerrainTriangleIntersectsAABB(const float* aabb) {
  const uint32_t cellCount = CollisionGlobals::s_maxCellIndex;
  const uint8_t* cellData  = CollisionGlobals::s_cellNormals;

  if (cellCount == 0 || cellData == nullptr) {
    return 0;
  }

  for (uint32_t ci = 0; ci < cellCount; ++ci) {
    const uint8_t* entry = cellData + ci * kCellStride;

    ClippedPolygon poly;
    for (auto& v : poly.vertices) v = 0.0f;
    std::memcpy(poly.vertices, entry + kCellVerticesOff, kCellVerticesSize);
    poly.count   = 3;
    poly.tags[0] = -1;
    poly.tags[1] = -1;
    poly.tags[2] = -1;

    if (ClipPolygonToAABB(poly, aabb)) {
      return 1;
    }
  }

  return 0;
}

}

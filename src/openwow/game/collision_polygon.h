#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stddef.h>

namespace openwow::game {

inline constexpr float kCollisionMinDistSq = 0.00077160494f;

struct ClippedPolygon {

  float vertices[15 * 3];

  int32_t tags[15];

  uint32_t count;

  void Clear() {
    for (auto& v : vertices) v = 0.0f;
    for (auto& t : tags) t = -1;
    count = 0;
  }
};

static_assert(sizeof(ClippedPolygon) == 244,
              "ClippedPolygon must match binary stack layout (244 bytes)");
static_assert(offsetof(ClippedPolygon, tags) == 180,
              "tags array must be at byte offset 180");
static_assert(offsetof(ClippedPolygon, count) == 240,
              "count must be at byte offset 240");

inline void CopyClippedPolygon(ClippedPolygon& dst, const ClippedPolygon& src) {
  for (auto& v : dst.vertices) v = 0.0f;
  dst.count = src.count;
  std::memcpy(dst.vertices, src.vertices, 12 * src.count);
  std::memcpy(dst.tags, src.tags, 4 * src.count);
}

void ClipPolygonToPlane(ClippedPolygon& poly, const float* plane,
                        int planeIndex);

bool ClipPolygonToAABB(ClippedPolygon& poly, const float* aabb);

inline bool AllVerticesNearPoint(const ClippedPolygon& poly,
                                const float* point) {
  if (poly.count == 0) {
    return true;
  }

  const float ref_x = point[0];
  const float ref_y = point[1];
  const float ref_z = point[2];

  for (uint32_t i = 0; i < poly.count; ++i) {
    const float dx = ref_x - poly.vertices[i * 3 + 0];
    const float dy = ref_y - poly.vertices[i * 3 + 1];
    const float dz = ref_z - poly.vertices[i * 3 + 2];

    if (dx * dx + dy * dy + dz * dz > kCollisionMinDistSq) {
      return false;
    }
  }

  return true;
}

}

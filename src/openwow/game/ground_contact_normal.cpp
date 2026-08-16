
#include "openwow/game/ground_contact_normal.h"

#include <cmath>
#include <cstring>

namespace openwow::game {

static constexpr uint32_t kCellStride = 52;
static constexpr uint32_t kCellNormalXOff = 0;
static constexpr uint32_t kCellNormalYOff = 4;
static constexpr uint32_t kCellNormalZOff = 8;
static constexpr uint32_t kCellVerticesOff = 16;
static constexpr uint32_t kCellVerticesSize = 36;

float* ComputeAverageGroundNormal(float* outNormal, const float* planes,
                                  uint32_t numPlanes) {
  float accumX = 0.0f;
  float accumY = 0.0f;
  float accumZ = 0.0f;
  uint32_t qualifyingCount = 0;

  const uint32_t cellCount = CollisionGlobals::s_maxCellIndex;
  const uint8_t* cellData = CollisionGlobals::s_cellNormals;

  if (cellCount == 0 || cellData == nullptr) {
    outNormal[0] = 0.0f;
    outNormal[1] = 0.0f;
    outNormal[2] = 1.0f;
    return outNormal;
  }

  for (uint32_t ci = 0; ci < cellCount; ++ci) {
    const uint8_t* entry = cellData + ci * kCellStride;

    float nz;
    std::memcpy(&nz, entry + kCellNormalZOff, sizeof(float));
    if (nz <= kMinUpwardNormalZ) {
      continue;
    }

    ClippedPolygon poly;
    for (auto& v : poly.vertices) v = 0.0f;
    std::memcpy(poly.vertices, entry + kCellVerticesOff, kCellVerticesSize);
    poly.count = 3;
    poly.tags[0] = -1;
    poly.tags[1] = -1;
    poly.tags[2] = -1;

    bool clippedAway = false;
    if (numPlanes > 0) {
      const float* planePtr = planes;
      for (uint32_t pi = 0; pi < numPlanes; ++pi) {
        ClipPolygonToPlane(poly, planePtr, static_cast<int>(pi));
        if (poly.count == 0) {
          clippedAway = true;
          break;
        }
        planePtr += 4;
      }
    }

    if (clippedAway) {
      continue;
    }

    float nx, ny;
    std::memcpy(&nx, entry + kCellNormalXOff, sizeof(float));
    std::memcpy(&ny, entry + kCellNormalYOff, sizeof(float));
    accumX += nx;
    accumY += ny;
    accumZ += nz;
    qualifyingCount++;
  }

  if (qualifyingCount == 0) {
    outNormal[0] = 0.0f;
    outNormal[1] = 0.0f;
    outNormal[2] = 1.0f;
    return outNormal;
  }

  float inv = 1.0f / static_cast<float>(qualifyingCount);
  float avgX = accumX * inv;
  float avgY = accumY * inv;
  float avgZ = accumZ * inv;

  float lenSq = avgX * avgX + avgY * avgY + avgZ * avgZ;
  float invLen = 1.0f / std::sqrt(lenSq);

  outNormal[0] = avgX * invLen;
  outNormal[1] = avgY * invLen;
  outNormal[2] = avgZ * invLen;

  return outNormal;
}

}

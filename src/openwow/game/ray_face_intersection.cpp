
#include "openwow/game/ray_face_intersection.h"

#include <cmath>

namespace openwow::game {

int TestRayAgainstClippedFace(const float* rayDir,
                              ClippedPolygon& poly,
                              const float* planes,
                              int targetPlane,
                              float* inOutMinDist) {
  float minDist = *inOutMinDist;
  const float* plane = &planes[4 * targetPlane];

  uint32_t vertexCount = poly.count;
  bool allBehind = true;

  const float denom =
      rayDir[0] * plane[0] + rayDir[1] * plane[1] + rayDir[2] * plane[2];
  const float absDenom = std::fabs(denom);

  for (uint32_t i = 0; i < vertexCount; ++i) {
    const float* v = &poly.vertices[i * 3];
    float num = v[0] * plane[0] + v[1] * plane[1] + v[2] * plane[2] + plane[3];

    float t = num;
    if (absDenom >= kRayDotEpsilon) {
      t = num / denom;
    }

    if (t < minDist) {
      minDist = (t <= 0.0f) ? 0.0f : t;
    }

    if (t > kBehindEpsilon) {
      allBehind = false;
    }
  }

  if (allBehind) {

    for (int pi = 0; pi < kBoundingPlaneCount; ++pi) {
      if (pi != targetPlane) {
        ClipPolygonToPlane(poly, &planes[4 * pi], pi);
        if (poly.count == 0) {
          return 0;
        }
      }
    }

    vertexCount = poly.count;
    bool allBehindLoose = true;

    for (uint32_t i = 0; i < vertexCount; ++i) {
      const float* v = &poly.vertices[i * 3];
      float num =
          v[0] * plane[0] + v[1] * plane[1] + v[2] * plane[2] + plane[3];

      float t = num;
      if (absDenom >= kRayDotEpsilon) {
        t = num / denom;
      }

      if (t > kLooseClipThreshold) {
        allBehindLoose = false;
      }
    }

    if (allBehindLoose) {
      return 0;
    }
  }

  if (minDist >= *inOutMinDist) {
    return 0;
  }

  *inOutMinDist = minDist;
  return 1;
}

}


#include "openwow/game/collision_polygon.h"

#include <cfloat>
#include <cstring>

namespace openwow::game {

static constexpr float kClipEpsilon = 0.0013888889f;

void ClipPolygonToPlane(ClippedPolygon& poly, const float* plane,
                        int planeIndex) {
  const uint32_t count = poly.count;
  if (count == 0) return;

  float distances[15];
  float minDist = FLT_MAX;
  float maxDist = -FLT_MAX;

  for (uint32_t i = 0; i < count; ++i) {
    const float* v = &poly.vertices[i * 3];
    float d = -(v[0] * plane[0] + v[1] * plane[1] + v[2] * plane[2] + plane[3]);
    if (d < minDist) minDist = d;
    if (d > maxDist) maxDist = d;
    distances[i] = d;
  }

  if (minDist > -kClipEpsilon) {

    return;
  }

  if (maxDist < kClipEpsilon) {

    poly.count = 0;
    return;
  }

  ClippedPolygon temp;
  CopyClippedPolygon(temp, poly);
  poly.count = 0;

  uint32_t prevIdx = count - 1;
  for (uint32_t i = 0; i < count; ++i) {
    const float currDist = distances[i];
    const float prevDist = distances[prevIdx];

    const float* currVert = &temp.vertices[i * 3];
    const float* prevVert = &temp.vertices[prevIdx * 3];

    if (prevDist >= 0.0f) {

      if (currDist < 0.0f) {

        if (prevDist > kClipEpsilon) {
          float t = prevDist / (currDist - prevDist);
          uint32_t out = poly.count;
          float nx = prevVert[0] - (currVert[0] - prevVert[0]) * t;
          float ny = prevVert[1] - (currVert[1] - prevVert[1]) * t;
          float nz = prevVert[2] - (currVert[2] - prevVert[2]) * t;
          poly.tags[out] = planeIndex;
          poly.vertices[out * 3 + 0] = nx;
          poly.vertices[out * 3 + 1] = ny;
          poly.vertices[out * 3 + 2] = nz;
          poly.count++;
        }

      } else {

        uint32_t out = poly.count;
        poly.tags[out] = temp.tags[i];
        poly.vertices[out * 3 + 0] = currVert[0];
        poly.vertices[out * 3 + 1] = currVert[1];
        poly.vertices[out * 3 + 2] = currVert[2];
        poly.count++;
      }
    } else {

      if (currDist >= 0.0f) {

        if (currDist > kClipEpsilon) {
          float t = prevDist / (currDist - prevDist);
          uint32_t out = poly.count;
          float nx = prevVert[0] - (currVert[0] - prevVert[0]) * t;
          float ny = prevVert[1] - (currVert[1] - prevVert[1]) * t;
          float nz = prevVert[2] - (currVert[2] - prevVert[2]) * t;
          poly.tags[out] = planeIndex;
          poly.vertices[out * 3 + 0] = nx;
          poly.vertices[out * 3 + 1] = ny;
          poly.vertices[out * 3 + 2] = nz;
          poly.count++;
        }

        uint32_t out = poly.count;
        poly.tags[out] = temp.tags[i];
        poly.vertices[out * 3 + 0] = currVert[0];
        poly.vertices[out * 3 + 1] = currVert[1];
        poly.vertices[out * 3 + 2] = currVert[2];
        poly.count++;
      }

    }

    prevIdx = i;
  }

  if (poly.count < 3) {
    poly.count = 0;
  }
}

bool ClipPolygonToAABB(ClippedPolygon& poly, const float* aabb) {
  float plane[4];

  plane[0] = -1.0f;
  plane[1] =  0.0f;
  plane[2] =  0.0f;
  plane[3] =  aabb[0];
  ClipPolygonToPlane(poly, plane, -1);
  if (poly.count == 0) return false;

  plane[0] =  1.0f;
  plane[1] =  0.0f;
  plane[2] =  0.0f;
  plane[3] = -aabb[3];
  ClipPolygonToPlane(poly, plane, -1);
  if (poly.count == 0) return false;

  plane[0] =  0.0f;
  plane[1] = -1.0f;
  plane[2] =  0.0f;
  plane[3] =  aabb[1];
  ClipPolygonToPlane(poly, plane, -1);
  if (poly.count == 0) return false;

  plane[0] =  0.0f;
  plane[1] =  1.0f;
  plane[2] =  0.0f;
  plane[3] = -aabb[4];
  ClipPolygonToPlane(poly, plane, -1);
  if (poly.count == 0) return false;

  plane[0] =  0.0f;
  plane[1] =  0.0f;
  plane[2] = -1.0f;
  plane[3] =  aabb[2];
  ClipPolygonToPlane(poly, plane, -1);
  if (poly.count == 0) return false;

  plane[0] =  0.0f;
  plane[1] =  0.0f;
  plane[2] =  1.0f;
  plane[3] = -aabb[5];
  ClipPolygonToPlane(poly, plane, -1);

  return poly.count != 0;
}

}

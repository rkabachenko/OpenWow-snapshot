
#include "openwow/render/scene/frustum_culler.h"

#include <cmath>

namespace openwow::render {

void FrustumCuller::NormalizePlane(Plane& p) {
  float len = std::sqrt(p.nx * p.nx + p.ny * p.ny + p.nz * p.nz);
  if (len > 1e-8f) {
    float inv = 1.0f / len;
    p.nx *= inv;
    p.ny *= inv;
    p.nz *= inv;
    p.d *= inv;
  }
}

FrustumPlanes FrustumCuller::ExtractFromViewProj(const RenderMatrix4x4View vp) {

  FrustumPlanes f;

  auto extract = [&](int idx, int sign, int row) {
    Plane& p = f.planes[idx];
    p.nx = vp[3 * 4 + 0] + static_cast<float>(sign) * vp[row * 4 + 0];
    p.ny = vp[3 * 4 + 1] + static_cast<float>(sign) * vp[row * 4 + 1];
    p.nz = vp[3 * 4 + 2] + static_cast<float>(sign) * vp[row * 4 + 2];
    p.d = vp[3 * 4 + 3] + static_cast<float>(sign) * vp[row * 4 + 3];
    NormalizePlane(p);
  };

  extract(FrustumPlanes::kLeft, 1, 0);

  extract(FrustumPlanes::kRight, -1, 0);

  extract(FrustumPlanes::kBottom, 1, 1);

  extract(FrustumPlanes::kTop, -1, 1);

  extract(FrustumPlanes::kNear, 1, 2);

  extract(FrustumPlanes::kFar, -1, 2);

  return f;
}

CullResult FrustumCuller::TestAABB(const FrustumPlanes& frustum,
                                    const CullerAABB& aabb) {
  bool all_inside = true;

  for (int i = 0; i < 6; ++i) {
    const Plane& p = frustum.planes[i];

    float px = (p.nx >= 0.0f) ? aabb.max_x : aabb.min_x;
    float py = (p.ny >= 0.0f) ? aabb.max_y : aabb.min_y;
    float pz = (p.nz >= 0.0f) ? aabb.max_z : aabb.min_z;

    float nxx = (p.nx >= 0.0f) ? aabb.min_x : aabb.max_x;
    float nyy = (p.ny >= 0.0f) ? aabb.min_y : aabb.max_y;
    float nzz = (p.nz >= 0.0f) ? aabb.min_z : aabb.max_z;

    if (p.nx * px + p.ny * py + p.nz * pz + p.d < 0.0f) {
      return CullResult::Outside;
    }

    if (p.nx * nxx + p.ny * nyy + p.nz * nzz + p.d < 0.0f) {
      all_inside = false;
    }
  }

  return all_inside ? CullResult::Inside : CullResult::Intersect;
}

CullResult FrustumCuller::TestSphere(const FrustumPlanes& frustum, float cx,
                                      float cy, float cz, float radius) {
  bool all_inside = true;

  for (int i = 0; i < 6; ++i) {
    const Plane& p = frustum.planes[i];
    float dist = p.nx * cx + p.ny * cy + p.nz * cz + p.d;

    if (dist < -radius) {
      return CullResult::Outside;
    }
    if (dist < radius) {
      all_inside = false;
    }
  }

  return all_inside ? CullResult::Inside : CullResult::Intersect;
}

bool FrustumCuller::TestPoint(const FrustumPlanes& frustum, float px,
                               float py, float pz) {
  for (int i = 0; i < 6; ++i) {
    const Plane& p = frustum.planes[i];
    if (p.nx * px + p.ny * py + p.nz * pz + p.d < 0.0f) {
      return false;
    }
  }
  return true;
}

bool FrustumCuller::IntersectPlanes(const Plane& a, const Plane& b,
                                     const Plane& c, float& out_x,
                                     float& out_y, float& out_z) {

  float d1x = b.ny * c.nz - b.nz * c.ny;
  float d1y = b.nz * c.nx - b.nx * c.nz;
  float d1z = b.nx * c.ny - b.ny * c.nx;

  float denom = a.nx * d1x + a.ny * d1y + a.nz * d1z;
  if (std::fabs(denom) < 1e-10f) return false;

  float inv = 1.0f / denom;

  float d2x = c.ny * a.nz - c.nz * a.ny;
  float d2y = c.nz * a.nx - c.nx * a.nz;
  float d2z = c.nx * a.ny - c.ny * a.nx;

  float d3x = a.ny * b.nz - a.nz * b.ny;
  float d3y = a.nz * b.nx - a.nx * b.nz;
  float d3z = a.nx * b.ny - a.ny * b.nx;

  out_x = (-a.d * d1x - b.d * d2x - c.d * d3x) * inv;
  out_y = (-a.d * d1y - b.d * d2y - c.d * d3y) * inv;
  out_z = (-a.d * d1z - b.d * d2z - c.d * d3z) * inv;
  return true;
}

std::array<std::array<float, 3>, 8> FrustumCuller::GetFrustumCorners(
    const FrustumPlanes& frustum) {
  std::array<std::array<float, 3>, 8> corners{};

  const auto& N = frustum.planes[FrustumPlanes::kNear];
  const auto& F = frustum.planes[FrustumPlanes::kFar];
  const auto& L = frustum.planes[FrustumPlanes::kLeft];
  const auto& R = frustum.planes[FrustumPlanes::kRight];
  const auto& T = frustum.planes[FrustumPlanes::kTop];
  const auto& B = frustum.planes[FrustumPlanes::kBottom];

  IntersectPlanes(N, B, L, corners[0][0], corners[0][1], corners[0][2]);
  IntersectPlanes(N, B, R, corners[1][0], corners[1][1], corners[1][2]);
  IntersectPlanes(N, T, R, corners[2][0], corners[2][1], corners[2][2]);
  IntersectPlanes(N, T, L, corners[3][0], corners[3][1], corners[3][2]);
  IntersectPlanes(F, B, L, corners[4][0], corners[4][1], corners[4][2]);
  IntersectPlanes(F, B, R, corners[5][0], corners[5][1], corners[5][2]);
  IntersectPlanes(F, T, R, corners[6][0], corners[6][1], corners[6][2]);
  IntersectPlanes(F, T, L, corners[7][0], corners[7][1], corners[7][2]);

  return corners;
}

float FrustumCuller::DistanceToPlane(const Plane& plane, float px, float py,
                                      float pz) {
  return plane.nx * px + plane.ny * py + plane.nz * pz + plane.d;
}

}

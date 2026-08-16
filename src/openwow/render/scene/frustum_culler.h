#pragma once

#include <array>
#include <cmath>
#include <cstdint>

#include "openwow/render/api/math/render_math_types.h"

namespace openwow::render {

enum class CullResult : uint8_t {
  Outside,
  Inside,
  Intersect,
};

struct Plane {
  float nx{0.0f}, ny{0.0f}, nz{1.0f};
  float d{0.0f};

  [[nodiscard]] float DistanceTo(float px, float py, float pz) const {
    return nx * px + ny * py + nz * pz + d;
  }
};

struct FrustumPlanes {

  enum PlaneIndex { kLeft = 0, kRight, kTop, kBottom, kNear, kFar };

  std::array<Plane, 6> planes{};

  [[nodiscard]] const Plane& Near() const { return planes[kNear]; }
  [[nodiscard]] const Plane& Far() const { return planes[kFar]; }
  [[nodiscard]] const Plane& Left() const { return planes[kLeft]; }
  [[nodiscard]] const Plane& Right() const { return planes[kRight]; }
  [[nodiscard]] const Plane& Top() const { return planes[kTop]; }
  [[nodiscard]] const Plane& Bottom() const { return planes[kBottom]; }
};

struct CullerAABB {
  float min_x{0}, min_y{0}, min_z{0};
  float max_x{0}, max_y{0}, max_z{0};
};

class FrustumCuller {
 public:

  static FrustumPlanes ExtractFromViewProj(RenderMatrix4x4View vp);

  static CullResult TestAABB(const FrustumPlanes& frustum,
                              const CullerAABB& aabb);

  static CullResult TestSphere(const FrustumPlanes& frustum, float cx,
                                float cy, float cz, float radius);

  static bool TestPoint(const FrustumPlanes& frustum, float px, float py,
                         float pz);

  static std::array<std::array<float, 3>, 8> GetFrustumCorners(
      const FrustumPlanes& frustum);

  static float DistanceToPlane(const Plane& plane, float px, float py,
                                float pz);

 private:

  static void NormalizePlane(Plane& p);

  static bool IntersectPlanes(const Plane& a, const Plane& b, const Plane& c,
                               float& out_x, float& out_y, float& out_z);
};

}

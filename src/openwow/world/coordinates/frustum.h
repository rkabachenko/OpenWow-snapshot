#pragma once

#include <array>
#include <cmath>
#include <cstdint>

#include "openwow/foundation/compiler/inline_hint.h"
#include "openwow/world/coordinates/world_geometry.h"

namespace openwow::world {

struct Frustum {
  enum Plane { kLeft = 0, kRight, kTop, kBottom, kNear, kFar };

  std::array<std::array<float, 4>, 6> planes{};

  void ExtractFromViewProj(std::span<const float, 16> vp) {
    ExtractFromViewProjWindow(vp, -1.0f, -1.0f, 1.0f, 1.0f);
  }

  void ExtractFromViewProjWindow(std::span<const float, 16> vp,
                                 float min_x, float min_y,
                                 float max_x, float max_y) {
    if (min_x > max_x) {
      min_x = -1.0f;
      max_x = 1.0f;
    }
    if (min_y > max_y) {
      min_y = -1.0f;
      max_y = 1.0f;
    }

    auto extract = [&](int plane_idx, float sign, float bound, int column) {
      auto& p = planes[plane_idx];
      for (int row = 0; row < 4; ++row) {
        p[row] = bound * vp[row * 4 + 3] + sign * vp[row * 4 + column];
      }

      float len = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
      if (len > 1e-8f) {
        float inv = 1.0f / len;
        p[0] *= inv;
        p[1] *= inv;
        p[2] *= inv;
        p[3] *= inv;
      }
    };

    extract(kLeft, 1.0f, -min_x, 0);
    extract(kRight, -1.0f, max_x, 0);
    extract(kBottom, 1.0f, -min_y, 1);
    extract(kTop, -1.0f, max_y, 1);
    extract(kNear, 1.0f, 1.0f, 2);
    extract(kFar, -1.0f, 1.0f, 2);
  }

  [[nodiscard]] OPENWOW_FORCE_INLINE bool TestAABB(float min_x, float min_y, float min_z,
                               float max_x, float max_y, float max_z) const {
    for (int i = 0; i < 6; ++i) {
      const auto& p = planes[i];

      float px = (p[0] >= 0.0f) ? max_x : min_x;
      float py = (p[1] >= 0.0f) ? max_y : min_y;
      float pz = (p[2] >= 0.0f) ? max_z : min_z;

      if (p[0] * px + p[1] * py + p[2] * pz + p[3] < 0.0f) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] OPENWOW_FORCE_INLINE bool TestSphere(float cx, float cy, float cz,
                                 float radius) const {
    for (int i = 0; i < 6; ++i) {
      const auto& p = planes[i];
      float dist = p[0] * cx + p[1] * cy + p[2] * cz + p[3];
      if (dist < -radius) {
        return false;
      }
    }
    return true;
  }
};

}

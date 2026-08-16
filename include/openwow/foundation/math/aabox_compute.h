
#pragma once

#include <cstdint>

namespace openwow::math::aabox {

inline float* ComputeAABB(float* out, const float* points,
                           std::uint32_t count) noexcept {
  if (count == 0) {
    out[0] = 0.0f;
    out[1] = 0.0f;
    out[2] = 0.0f;
    out[3] = 0.0f;
    out[4] = 0.0f;
    out[5] = 0.0f;
    return out;
  }

  float min_x = points[0];
  float min_y = points[1];
  float min_z = points[2];
  float max_x = points[0];
  float max_y = points[1];
  float max_z = points[2];

  for (std::uint32_t i = 1; i < count; ++i) {
    const float* p = points + i * 3;
    if (!(min_x <= p[0])) min_x = p[0];
    if (!(p[0] <= max_x)) max_x = p[0];
    if (!(min_y <= p[1])) min_y = p[1];
    if (!(p[1] <= max_y)) max_y = p[1];
    if (!(min_z <= p[2])) min_z = p[2];
    if (!(p[2] <= max_z)) max_z = p[2];
  }

  out[0] = min_x;
  out[1] = min_y;
  out[2] = min_z;
  out[3] = max_x;
  out[4] = max_y;
  out[5] = max_z;
  return out;
}

}

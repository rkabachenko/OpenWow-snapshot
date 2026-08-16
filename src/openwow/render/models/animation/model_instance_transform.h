#pragma once

#include "openwow/render/api/math/render_math_types.h"

#include <cmath>

namespace openwow::render {

[[nodiscard]] inline RenderMatrix4x4 BuildM2ModelInstanceTransform(
    const float x,
    const float y,
    const float z,
    const float facing_rad,
    const float scale) {
  const float s = std::sin(facing_rad);
  const float c = std::cos(facing_rad);

  RenderMatrix4x4 out{};
  out[0] = c * scale;
  out[1] = s * scale;
  out[4] = -s * scale;
  out[5] = c * scale;
  out[10] = scale;
  out[12] = x;
  out[13] = y;
  out[14] = z;
  out[15] = 1.0f;
  return out;
}

}

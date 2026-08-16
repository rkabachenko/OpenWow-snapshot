
#pragma once

#include <cstddef>

namespace openwow::math::float_array {

inline float* Copy(float* dst, const float* src, std::size_t count) {
  for (std::size_t i = 0; i < count; ++i) {
    dst[i] = src[i];
  }
  return dst;
}

}

#pragma once

#include <cmath>

namespace openwow::math {

inline int GetDominantAxis(const float v[3]) {
  const float abs_y = std::fabs(v[1]);

  const float abs_x = std::fabs(v[0]);

  const float abs_z = std::fabs(v[2]);

  if (abs_x <= abs_y) {

    if (abs_y > abs_z)
      return 1;

  } else {
    if (abs_x > abs_z)
      return 0;

  }
  return 2;

}

}

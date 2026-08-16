
#pragma once

#include <cmath>

namespace openwow::math {

[[nodiscard]] inline int IsNaN(double value) noexcept {
  return std::isnan(value) ? 1 : 0;
}

}


#pragma once

#include "openwow/foundation/math/fast_trig_approx.h"

namespace openwow::math {

inline float CosineEaseInOut(float t) {
  const float cos_approx = detail::EvaluateFastTrigPolynomial(t);
  return static_cast<float>(0.5 - static_cast<double>(cos_approx) * 0.5);
}

}

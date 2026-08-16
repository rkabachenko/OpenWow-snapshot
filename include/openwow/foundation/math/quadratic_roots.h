
#pragma once

#include <cmath>

namespace openwow::math::quadratic_roots {

struct OrderedRoots {
  float low = 0.0f;
  float high = 0.0f;
};

inline bool SolveOrderedStable(float quadratic_term,
                               float linear_term,
                               float constant_term,
                               OrderedRoots* out_roots) {
  const double a = static_cast<double>(quadratic_term);
  const double b = static_cast<double>(linear_term);
  const double c = static_cast<double>(constant_term);
  const double discriminant = b * b - 4.0 * a * c;
  if (!(discriminant > 0.0)) {
    return false;
  }

  const double sqrt_discriminant = std::sqrt(discriminant);
  const double q =
      -0.5 * (b <= 0.0 ? b - sqrt_discriminant : b + sqrt_discriminant);
  const float root0 = static_cast<float>(c / q);
  const float root1 = static_cast<float>(q / a);

  if (out_roots != nullptr) {
    if (root0 <= root1) {
      out_roots->low = root0;
      out_roots->high = root1;
    } else {
      out_roots->low = root1;
      out_roots->high = root0;
    }
  }
  return true;
}

}

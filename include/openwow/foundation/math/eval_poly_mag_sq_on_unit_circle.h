
#pragma once

#include <cmath>

namespace openwow::math {

inline constexpr int kPolyCoeffCount = 10;

inline double EvalPolyMagSqOnUnitCircle(const float* coefficients, float theta) {
  const float c = std::cos(theta);
  const float s = std::sin(theta);

  float z_real = coefficients[8] + coefficients[9] * c;
  float z_imag = coefficients[9] * s;

  for (int k = 7; k >= 0; --k) {
    const float new_imag = z_imag * c + z_real * s;
    const float new_real = z_real * c - z_imag * s;
    z_real = new_real + coefficients[k];
    z_imag = new_imag;
  }

  const float final_imag = z_imag * c + z_real * s;
  const float final_real = z_real * c - z_imag * s + 1.0f;

  return static_cast<double>(final_imag * final_imag + final_real * final_real);
}

}

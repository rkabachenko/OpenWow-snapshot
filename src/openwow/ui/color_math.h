#pragma once

#include <algorithm>
#include <cmath>

namespace openwow::ui {

struct ColorRGB {
  double r{0.0};
  double g{0.0};
  double b{0.0};
};

struct ColorHSV {
  double h{0.0};
  double s{0.0};
  double v{0.0};
};

[[nodiscard]] inline ColorHSV RGBToHSV(double r, double g, double b) noexcept {
  r = std::clamp(r, 0.0, 1.0);
  g = std::clamp(g, 0.0, 1.0);
  b = std::clamp(b, 0.0, 1.0);

  const double cmax = std::max({r, g, b});
  const double cmin = std::min({r, g, b});
  const double delta = cmax - cmin;

  ColorHSV hsv;
  hsv.v = cmax;
  hsv.s = (cmax > 0.0) ? (delta / cmax) : 0.0;

  constexpr double kHueSectorDegrees = 60.0;
  constexpr double kFullHueDegrees = 360.0;
  constexpr double kColorEpsilon = 1.0e-10;
  if (delta < kColorEpsilon) {
    hsv.h = 0.0;
  } else if (cmax == r) {
    hsv.h = kHueSectorDegrees * std::fmod((g - b) / delta + 6.0, 6.0);
  } else if (cmax == g) {
    hsv.h = kHueSectorDegrees * ((b - r) / delta + 2.0);
  } else {
    hsv.h = kHueSectorDegrees * ((r - g) / delta + 4.0);
  }

  if (hsv.h >= kFullHueDegrees) {
    hsv.h -= kFullHueDegrees;
  }
  return hsv;
}

[[nodiscard]] inline ColorRGB HSVToRGB(double h, double s, double v) noexcept {
  constexpr double kHueSectorDegrees = 60.0;
  constexpr double kFullHueDegrees = 360.0;

  h = std::fmod(h, kFullHueDegrees);
  if (h < 0.0) {
    h += kFullHueDegrees;
  }
  s = std::clamp(s, 0.0, 1.0);
  v = std::clamp(v, 0.0, 1.0);

  const double c = v * s;
  const double x = c * (1.0 - std::fabs(std::fmod(h / kHueSectorDegrees, 2.0) - 1.0));
  const double m = v - c;

  double rp = 0.0;
  double gp = 0.0;
  double bp = 0.0;
  if (h < 60.0) {
    rp = c;
    gp = x;
  } else if (h < 120.0) {
    rp = x;
    gp = c;
  } else if (h < 180.0) {
    gp = c;
    bp = x;
  } else if (h < 240.0) {
    gp = x;
    bp = c;
  } else if (h < 300.0) {
    rp = x;
    bp = c;
  } else {
    rp = c;
    bp = x;
  }

  return {rp + m, gp + m, bp + m};
}

}

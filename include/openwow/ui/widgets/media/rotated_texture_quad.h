#pragma once

#include <cmath>

namespace openwow::ui::media {

struct RotatedTextureQuad {
  float upper_left_u;
  float upper_left_v;
  float lower_left_u;
  float lower_left_v;
  float upper_right_u;
  float upper_right_v;
  float lower_right_u;
  float lower_right_v;
};

[[nodiscard]] inline RotatedTextureQuad ComputeRotatedTextureQuad(
    const float angle_radians, const float center_u = 0.5f,
    const float center_v = 0.5f) noexcept {
  constexpr double kQuarterTurn = 0.7853981852531433;
  const double adjusted_angle =
      static_cast<double>(angle_radians) - kQuarterTurn;
  const float sine = static_cast<float>(std::sin(adjusted_angle));
  const float cosine = static_cast<float>(std::cos(adjusted_angle));

  return {
      sine + center_u,
      center_v - cosine,
      center_u - cosine,
      center_v - sine,
      cosine + center_u,
      sine + center_v,
      center_u - sine,
      center_v + cosine,
  };
}

[[nodiscard]] inline RotatedTextureQuad ComputeMinimapRotatingIconTextureQuad(
    const float angle_radians) noexcept {
  constexpr float kHalfTurn = 1.5707963705062866f;
  return ComputeRotatedTextureQuad(angle_radians - kHalfTurn);
}

}

#pragma once

#include <algorithm>
#include <cmath>

namespace openwow::render {

struct SelectionDecalUv {
  float u{0.0f};
  float v{0.0f};
};

[[nodiscard]] inline SelectionDecalUv SelectionDecalFootprintUv(
    const float world_x, const float world_y, const float center_x,
    const float center_y, const float xy_half_extent, const float camera_x,
    const float camera_y) {
  const float to_camera_yaw = std::atan2(camera_y - center_y, camera_x - center_x);
  const float cos_yaw = std::cos(to_camera_yaw);
  const float sin_yaw = std::sin(to_camera_yaw);
  const float inv_diameter = 1.0f / (2.0f * xy_half_extent);
  const float dx = world_x - center_x;
  const float dy = world_y - center_y;
  return SelectionDecalUv{
      0.5f + (dx * sin_yaw - dy * cos_yaw) * inv_diameter,
      0.5f + (dx * cos_yaw + dy * sin_yaw) * inv_diameter};
}

struct SelectionRingShape {

  float xy_half_extent{0.0f};

  float alpha_multiplier{1.0f};
};

[[nodiscard]] inline SelectionRingShape SelectionRingFlashShape(
    const float radius, const float t) {
  const float inv = 1.0f - t;
  const float ease = inv * inv * inv;
  float alpha = 1.0f - ease;
  if (t > 0.5f) {
    const float k = (t - 0.5f) + (t - 0.5f);
    alpha *= 1.0f - k * k;
  }
  return SelectionRingShape{
      radius + ((radius * 1.6f + 2.3f) - radius) * ease, alpha};
}

[[nodiscard]] inline SelectionRingShape PetTargetRingShape(const float radius,
                                                           const float t) {
  const float inv = 1.0f - t;
  const float ease = inv * inv * inv;
  float alpha = 1.0f;
  if (t < 0.25f) {
    alpha = (t * 4.0f) * (t * 4.0f);
  } else if (t > 0.75f) {
    const float k = (t - 0.75f) * 4.0f;
    alpha = 1.0f - k * k;
  }
  return SelectionRingShape{
      ease * ((radius * 1.5f + 2.3f) - radius) + radius, alpha};
}

}

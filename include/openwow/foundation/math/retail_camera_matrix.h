#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace openwow::math {

inline void BuildRetailCameraViewMatrix(const float eye[3], const float target[3],
                                        const float up[3], float out[16]) noexcept {
  std::fill_n(out, 16, 0.0f);
  out[0] = 1.0f;
  out[5] = 1.0f;
  out[10] = 1.0f;
  out[15] = 1.0f;

  float forward[3] = {
      target[0] - eye[0],
      target[1] - eye[1],
      target[2] - eye[2],
  };
  const float forward_length_squared =
      forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2];
  const float up_length_squared = up[0] * up[0] + up[1] * up[1] + up[2] * up[2];
  if (forward_length_squared < 0.01f || up_length_squared < 0.01f) {
    return;
  }

  const float inverse_forward_length = 1.0f / std::sqrt(forward_length_squared);
  for (float& component : forward) {
    component *= inverse_forward_length;
  }

  float right[3] = {
      forward[1] * up[2] - forward[2] * up[1],
      forward[2] * up[0] - forward[0] * up[2],
      forward[0] * up[1] - forward[1] * up[0],
  };
  const float right_length_squared =
      right[0] * right[0] + right[1] * right[1] + right[2] * right[2];
  const float inverse_right_length = 1.0f / std::sqrt(right_length_squared);
  for (float& component : right) {
    component *= inverse_right_length;
  }

  float view_up[3] = {
      right[1] * forward[2] - right[2] * forward[1],
      right[2] * forward[0] - right[0] * forward[2],
      right[0] * forward[1] - right[1] * forward[0],
  };

  const float view_up_length_squared = view_up[0] * view_up[0] +
                                       view_up[1] * view_up[1] +
                                       view_up[2] * view_up[2];
  const float inverse_view_up_length = 1.0f / std::sqrt(view_up_length_squared);
  for (float& component : view_up) {
    component *= inverse_view_up_length;
  }

  out[0] = right[0];
  out[4] = right[1];
  out[8] = right[2];
  out[1] = view_up[0];
  out[5] = view_up[1];
  out[9] = view_up[2];
  out[2] = forward[0];
  out[6] = forward[1];
  out[10] = forward[2];
  out[12] = -(eye[0] * out[0] + eye[1] * out[4] + eye[2] * out[8]);
  out[13] = -(eye[0] * out[1] + eye[1] * out[5] + eye[2] * out[9]);
  out[14] = -(eye[0] * out[2] + eye[1] * out[6] + eye[2] * out[10]);
}

inline void BuildRetailCameraProjectionMatrix(const float vertical_fov_radians,
                                              const float width_over_height,
                                              const float near_plane,
                                              const float far_plane,
                                              float out[16]) noexcept {
  std::fill_n(out, 16, 0.0f);
  if (!(vertical_fov_radians > 0.0f) ||
      !(vertical_fov_radians < 3.1415927f) ||
      !(width_over_height > 0.0f) || !(near_plane < far_plane)) {
    out[0] = out[5] = out[10] = out[15] = 1.0f;
    return;
  }

  const float inverse_tangent = 1.0f / std::tan(vertical_fov_radians * 0.5f);
  const float depth_range = far_plane - near_plane;
  out[0] = inverse_tangent / width_over_height;
  out[5] = inverse_tangent;
  out[10] = (near_plane + far_plane) / depth_range;
  out[11] = 1.0f;
  out[14] = (-2.0f * near_plane * far_plane) / depth_range;
}

}

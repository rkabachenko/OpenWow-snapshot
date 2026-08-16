#pragma once

namespace openwow::game {

struct Vec3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;

  [[nodiscard]] constexpr Vec3 operator+(const Vec3& other) const noexcept {
    return {x + other.x, y + other.y, z + other.z};
  }

  [[nodiscard]] constexpr Vec3 operator-(const Vec3& other) const noexcept {
    return {x - other.x, y - other.y, z - other.z};
  }

  [[nodiscard]] constexpr Vec3 operator*(float scale) const noexcept {
    return {x * scale, y * scale, z * scale};
  }
};

}

#pragma once

#include <cmath>

namespace openwow::render {

struct Vec3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

inline Vec3 operator+(const Vec3& lhs, const Vec3& rhs) {
  return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

inline Vec3 operator-(const Vec3& lhs, const Vec3& rhs) {
  return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

inline Vec3 operator*(const Vec3& value, float scale) {
  return {value.x * scale, value.y * scale, value.z * scale};
}

inline float LengthSquared(const Vec3& value) {
  return value.x * value.x + value.y * value.y + value.z * value.z;
}

inline float Length(const Vec3& value) {
  return std::sqrt(LengthSquared(value));
}

inline Vec3 Normalize(const Vec3& value) {
  const float length = Length(value);
  if (length <= 0.00000023841858f) {
    return {};
  }

  const float inverse_length = 1.0f / length;
  return value * inverse_length;
}

inline bool HasLength(const Vec3& value) {
  return LengthSquared(value) > 0.00000023841858f * 0.00000023841858f;
}

inline Vec3 ResolveTravelDirection(const Vec3& delta,
                                   const Vec3& previous_direction,
                                   bool has_previous_direction) {
  const float delta_length = Length(delta);
  if (delta_length <= 0.00000023841858f) {
    return has_previous_direction ? previous_direction : Vec3{};
  }

  if (delta_length < 0.0099999998f && has_previous_direction) {
    float blend = delta_length * 100.0f;
    if (blend < 0.050000001f) {
      blend = 0.050000001f;
    }

    const Vec3 blended =
        delta * blend + previous_direction * (1.0f - blend);
    const Vec3 normalized = Normalize(blended);
    return HasLength(normalized) ? normalized : previous_direction;
  }

  return Normalize(delta);
}

}

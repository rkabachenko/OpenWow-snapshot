#pragma once

#include <array>
#include <cmath>
#include <span>

namespace openwow::world {

using Matrix4 = std::array<float, 16>;
using Vec3 = std::array<float, 3>;
using Vec4 = std::array<float, 4>;
using Bounds = std::array<float, 6>;

struct FogState {
  Vec4 params{300.0f, 800.0f, 0.0f, 0.0f};
  Vec4 color{0.6f, 0.7f, 0.85f, 1.0f};
};

inline constexpr Matrix4 kIdentityMatrix{
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f,
};

[[nodiscard]] inline Matrix4 Multiply(const std::span<const float, 16> lhs,
                                      const std::span<const float, 16> rhs) {
  Matrix4 product{};
  for (std::size_t row = 0; row < 4; ++row) {
    for (std::size_t column = 0; column < 4; ++column) {
      for (std::size_t inner = 0; inner < 4; ++inner) {
        product[row * 4 + column] +=
            lhs[row * 4 + inner] * rhs[inner * 4 + column];
      }
    }
  }
  return product;
}

[[nodiscard]] inline Vec3 TransformPoint(const Vec3& point,
                                         const Matrix4& matrix) {
  return {
      point[0] * matrix[0] + point[1] * matrix[4] +
          point[2] * matrix[8] + matrix[12],
      point[0] * matrix[1] + point[1] * matrix[5] +
          point[2] * matrix[9] + matrix[13],
      point[0] * matrix[2] + point[1] * matrix[6] +
          point[2] * matrix[10] + matrix[14],
  };
}

[[nodiscard]] inline Matrix4 RotationX(const float radians) {
  Matrix4 matrix = kIdentityMatrix;
  const float sine = std::sin(radians);
  const float cosine = std::cos(radians);
  matrix[5] = cosine;
  matrix[6] = sine;
  matrix[9] = -sine;
  matrix[10] = cosine;
  return matrix;
}

[[nodiscard]] inline Matrix4 RotationY(const float radians) {
  Matrix4 matrix = kIdentityMatrix;
  const float sine = std::sin(radians);
  const float cosine = std::cos(radians);
  matrix[0] = cosine;
  matrix[2] = -sine;
  matrix[8] = sine;
  matrix[10] = cosine;
  return matrix;
}

[[nodiscard]] inline Matrix4 RotationZ(const float radians) {
  Matrix4 matrix = kIdentityMatrix;
  const float sine = std::sin(radians);
  const float cosine = std::cos(radians);
  matrix[0] = cosine;
  matrix[1] = sine;
  matrix[4] = -sine;
  matrix[5] = cosine;
  return matrix;
}

}

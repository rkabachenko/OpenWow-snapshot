#pragma once

#include <cstring>
#include <cmath>

namespace openwow::render {

struct Mat4 {
  float m[16] = {
      1, 0, 0, 0,
      0, 1, 0, 0,
      0, 0, 1, 0,
      0, 0, 0, 1
  };

  static Mat4 Identity() { return Mat4{}; }

  static Mat4 Lerp(const Mat4& a, const Mat4& b, float t) {
    Mat4 result;
    for (int i = 0; i < 16; ++i) {
      result.m[i] = a.m[i] * (1.0f - t) + b.m[i] * t;
    }
    return result;
  }

  static Mat4 Multiply(const Mat4& a, const Mat4& b) {
    Mat4 result;
    std::memset(result.m, 0, sizeof(result.m));
    for (int row = 0; row < 4; ++row) {
      for (int col = 0; col < 4; ++col) {
        for (int k = 0; k < 4; ++k) {
          result.m[row * 4 + col] += a.m[row * 4 + k] * b.m[k * 4 + col];
        }
      }
    }
    return result;
  }

  static Mat4 Translation(float x, float y, float z) {
    Mat4 r;
    r.m[12] = x;
    r.m[13] = y;
    r.m[14] = z;
    return r;
  }

  static Mat4 Scale(float s) {
    Mat4 r;
    r.m[0] = s;
    r.m[5] = s;
    r.m[10] = s;
    return r;
  }

  static Mat4 InverseRigidTransform(const Mat4& source) {
    Mat4 result{};
    result.m[0] = source.m[0];
    result.m[1] = source.m[4];
    result.m[2] = source.m[8];
    result.m[3] = 0.0f;

    result.m[4] = source.m[1];
    result.m[5] = source.m[5];
    result.m[6] = source.m[9];
    result.m[7] = 0.0f;

    result.m[8] = source.m[2];
    result.m[9] = source.m[6];
    result.m[10] = source.m[10];
    result.m[11] = 0.0f;
    result.m[15] = 1.0f;
    ApplyInverseTranslation(source, result);
    return result;
  }

  static Mat4 InverseUniformScaleTransform(const Mat4& source, float scale) {
    if (std::fabs(scale - 1.0f) < 0.00000095367432f) {
      return InverseRigidTransform(source);
    }

    Mat4 result = InverseRigidTransform(source);
    const float inverse_scale_squared = 1.0f / (scale * scale);
    result.m[0] *= inverse_scale_squared;
    result.m[1] *= inverse_scale_squared;
    result.m[2] *= inverse_scale_squared;
    result.m[4] *= inverse_scale_squared;
    result.m[5] *= inverse_scale_squared;
    result.m[6] *= inverse_scale_squared;
    result.m[8] *= inverse_scale_squared;
    result.m[9] *= inverse_scale_squared;
    result.m[10] *= inverse_scale_squared;
    ApplyInverseTranslation(source, result);
    return result;
  }

 private:
  static void ApplyInverseTranslation(const Mat4& source, Mat4& result) {
    const float translated_x = -source.m[12];
    const float translated_y = -source.m[13];
    const float translated_z = -source.m[14];
    result.m[12] = result.m[0] * translated_x + result.m[4] * translated_y
                 + result.m[8] * translated_z;
    result.m[13] = result.m[1] * translated_x + result.m[5] * translated_y
                 + result.m[9] * translated_z;
    result.m[14] = result.m[2] * translated_x + result.m[6] * translated_y
                 + result.m[10] * translated_z;
  }
};

}

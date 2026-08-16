
#pragma once

#include <cmath>

namespace openwow::math::quaternion_xyzw {

struct Quaternion {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float w = 1.0f;
};

inline Quaternion Multiply(const Quaternion& lhs, const Quaternion& rhs) {
  return {
      lhs.w * rhs.x + rhs.z * lhs.y + rhs.w * lhs.x - lhs.z * rhs.y,
      rhs.w * lhs.y + lhs.w * rhs.y + lhs.z * rhs.x - rhs.z * lhs.x,
      rhs.z * lhs.w + rhs.w * lhs.z + lhs.x * rhs.y - rhs.x * lhs.y,
      rhs.w * lhs.w - lhs.x * rhs.x - lhs.y * rhs.y - rhs.z * lhs.z,
  };
}

inline Quaternion FromAxisAngle(const float* axis_xyz, float radians) {
  const float half = radians * 0.5f;
  const float s = std::sin(half);
  const float c = std::cos(half);
  return {axis_xyz[0] * s, axis_xyz[1] * s, axis_xyz[2] * s, c};
}

inline void FromAxisAngle(float* out_xyzw, float radians,
                          const float* axis_xyz) {
  const float half = radians * 0.5f;
  const float s = std::sin(half);
  out_xyzw[0] = axis_xyz[0] * s;
  out_xyzw[1] = axis_xyz[1] * s;
  out_xyzw[2] = axis_xyz[2] * s;
  out_xyzw[3] = std::cos(half);
}

inline void FastNormalizeInPlace(Quaternion& q) {
  constexpr float kA = 1.021435f;
  constexpr float kB = 0.532516f;
  constexpr float kC = 0.959066f;
  constexpr float kThreshHigh = 0.915212f;
  constexpr float kThreshLow  = 0.652120f;

  const float lengthSq = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;

  float invSqrt = kA - (lengthSq - kC) * kB;

  if (lengthSq <= kThreshHigh) {

    invSqrt = invSqrt * (kA - (invSqrt * invSqrt * lengthSq - kC) * kB);

    if (lengthSq <= kThreshLow) {

      invSqrt = (kA - (lengthSq * invSqrt * invSqrt - kC) * kB) * invSqrt;
    }
  }

  q.x *= invSqrt;
  q.y *= invSqrt;
  q.z *= invSqrt;
  q.w *= invSqrt;
}

inline void FastNormalizeInPlace(float* q) {
  FastNormalizeInPlace(*reinterpret_cast<Quaternion*>(q));
}

inline Quaternion LerpQuaternion(const Quaternion& a, const Quaternion& b,
                                 float t) {
  Quaternion result;
  result.x = a.x + (b.x - a.x) * t;
  result.y = a.y + (b.y - a.y) * t;
  result.z = a.z + (b.z - a.z) * t;
  result.w = a.w + (b.w - a.w) * t;
  FastNormalizeInPlace(result);
  return result;
}

inline void LerpQuaternion(float* out, float t, const float* a,
                           const float* b) {
  out[0] = a[0] + (b[0] - a[0]) * t;
  out[1] = a[1] + (b[1] - a[1]) * t;
  out[2] = a[2] + (b[2] - a[2]) * t;
  out[3] = a[3] + (b[3] - a[3]) * t;
  FastNormalizeInPlace(out);
}

inline void QuatSlerp(float* out, float t, const float* a, const float* b) {
  constexpr float kSlerpSinEpsilon = 4.76837158203125e-7f;

  float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];

  float sign = 1.0f;
  if (dot < 0.0f) {
    sign = -1.0f;
    dot = -dot;
  }

  float sinOmega = std::sqrt(std::fabs(1.0f - dot * dot));

  if (std::fabs(sinOmega) < kSlerpSinEpsilon) {
    out[0] = a[0];
    out[1] = a[1];
    out[2] = a[2];
    out[3] = a[3];
    return;
  }

  float omega = std::atan2(sinOmega, dot);
  float invSinOmega = 1.0f / sinOmega;
  float scaleA = std::sin((1.0f - t) * omega) * invSinOmega;
  float scaleB = std::sin(t * omega) * invSinOmega * sign;

  out[0] = a[0] * scaleA + b[0] * scaleB;
  out[1] = a[1] * scaleA + b[1] * scaleB;
  out[2] = a[2] * scaleA + b[2] * scaleB;
  out[3] = a[3] * scaleA + b[3] * scaleB;
}

inline Quaternion QuatSlerp(const Quaternion& a, const Quaternion& b,
                            float t) {
  Quaternion result;
  QuatSlerp(&result.x, t, &a.x, &b.x);
  return result;
}

}

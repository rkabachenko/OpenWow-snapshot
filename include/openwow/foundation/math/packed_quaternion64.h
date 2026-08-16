
#pragma once

#include <cmath>
#include <cstdint>

namespace openwow::math::packed_quaternion64 {

inline constexpr float kScaleX = 2097152.0f;
inline constexpr float kScaleYZ = 1048576.0f;
inline constexpr float kInvScaleX = 1.0f / kScaleX;
inline constexpr float kInvScaleYZ = 1.0f / kScaleYZ;
inline constexpr std::uint32_t kSigned21BitMask = 0x1FFFFFu;

inline std::int64_t Compress(const float x, const float y, const float z,
                             const float w) {
  const int sign = w >= 0.0f ? 1 : -1;

  const std::int32_t packed_y =
      sign * static_cast<std::int32_t>(std::trunc(y * kScaleYZ));
  const std::int32_t packed_x =
      sign * static_cast<std::int32_t>(std::trunc(x * kScaleX));
  const std::int32_t packed_z =
      sign * static_cast<std::int32_t>(std::trunc(z * kScaleYZ));

  const std::uint64_t x_bits =
      static_cast<std::uint64_t>(static_cast<std::uint32_t>(packed_x)) << 42;
  const std::uint64_t y_bits =
      (static_cast<std::uint64_t>(static_cast<std::uint32_t>(packed_y)) &
       kSigned21BitMask)
      << 21;
  const std::uint64_t z_bits =
      static_cast<std::uint64_t>(static_cast<std::uint32_t>(packed_z)) &
      kSigned21BitMask;

  return static_cast<std::int64_t>(x_bits | y_bits | z_bits);
}

inline std::int64_t Compress(const float* quaternion_xyzw) {
  return Compress(quaternion_xyzw[0], quaternion_xyzw[1], quaternion_xyzw[2],
                  quaternion_xyzw[3]);
}

inline std::int32_t SignExtend21Bit(const std::uint32_t value) {
  return static_cast<std::int32_t>(value << 11) >> 11;
}

inline void Decompress(const std::int64_t packed,
                       float& x,
                       float& y,
                       float& z,
                       float& w,
                       const float epsilon = kInvScaleYZ) {
  x = static_cast<float>(packed >> 42) * kInvScaleX;
  y = static_cast<float>(
          SignExtend21Bit(static_cast<std::uint32_t>(
              (static_cast<std::uint64_t>(packed) >> 21) & kSigned21BitMask)))
      * kInvScaleYZ;
  z = static_cast<float>(
          SignExtend21Bit(static_cast<std::uint32_t>(packed) & kSigned21BitMask))
      * kInvScaleYZ;

  const float squared_length = x * x + y * y + z * z;
  w = std::fabs(squared_length - 1.0f) >= epsilon
          ? std::sqrt(1.0f - squared_length)
          : 0.0f;
}

}

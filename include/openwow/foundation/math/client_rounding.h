#pragma once

#include <cmath>
#include <cstdint>

namespace openwow::math {

inline constexpr float kLegacyPixelSnapPositiveBias = 0.99994999f;
inline constexpr float kHalfUpRoundBias = 0.5f;

inline int TruncateFloatToIntTowardZero(const double value) {
  return static_cast<int>(std::trunc(value));
}

inline int LegacyLineSpacingSnapToInt(const float value) {
  return TruncateFloatToIntTowardZero(static_cast<double>(value) +
                                      static_cast<double>(kLegacyPixelSnapPositiveBias));
}

inline float LegacyLineSpacingSnapToFloat(const float value) {
  return static_cast<float>(LegacyLineSpacingSnapToInt(value));
}

inline int LegacyPixelSnapToInt(const float value) {
  double adjusted = static_cast<double>(value);
  if (value > 0.0f) {
    adjusted += static_cast<double>(kLegacyPixelSnapPositiveBias);
  }
  return TruncateFloatToIntTowardZero(adjusted);
}

inline float LegacyPixelSnapToFloat(const float value) {
  return static_cast<float>(LegacyPixelSnapToInt(value));
}

inline int RoundFloatHalfUpToInt(const float value) {
  return static_cast<int>(
      std::trunc(static_cast<double>(value) + static_cast<double>(kHalfUpRoundBias)));
}

inline int RoundFloatHalfAwayFromZero(const float value) {
  if (value <= 0.0f) {
    return static_cast<int>(static_cast<std::int64_t>(
        static_cast<double>(value) - 0.5));
  }
  return static_cast<int>(static_cast<std::int64_t>(
      static_cast<double>(value) + 0.5));
}

inline double FloorFloat(float value) {
  return std::floor(value);
}

}

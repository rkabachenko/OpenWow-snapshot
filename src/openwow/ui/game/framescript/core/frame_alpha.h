#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace openwow::ui::game {

inline std::uint8_t QuantizeFrameAlphaByteTruncated(double value) {
  const double clamped = std::clamp(value, 0.0, 1.0);
  return static_cast<std::uint8_t>(std::clamp(static_cast<int>(clamped * 255.0), 0, 255));
}

inline std::uint8_t QuantizeScriptAlphaByteWrapped(double value) {
  const double scaled = value * 255.0;
  if (!std::isfinite(scaled) ||
      scaled < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
      scaled > static_cast<double>(std::numeric_limits<std::int32_t>::max())) {
    return 0;
  }

  return static_cast<std::uint8_t>(static_cast<std::int32_t>(scaled));
}

inline double NormalizeFrameAlphaByte(std::uint8_t value) {
  return static_cast<double>(value) / 255.0;
}

inline std::uint8_t MultiplyFrameAlphaBytes(std::uint8_t lhs, std::uint8_t rhs) {
  return static_cast<std::uint8_t>(
      (static_cast<unsigned int>(lhs) * static_cast<unsigned int>(rhs)) / 255u);
}

}

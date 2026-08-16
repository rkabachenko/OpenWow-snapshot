
#pragma once

#include <cmath>
#include <cstdint>

namespace openwow::math {

inline constexpr float kDegToRad       = 0.017453292f;
inline constexpr float kWaveFreq1      = 0.154f;
inline constexpr float kWaveFreq2      = 0.195f;
inline constexpr float kWaveFreq3      = 0.093f;
inline constexpr float kWaveAvgFactor  = 0.333333f;
inline constexpr float kWaveAmpScale   = 0.013090f;

[[nodiscard]] inline float CalcSwimWaveBob(std::uint32_t time_ms,
                                           float amplitude) noexcept {
  if (amplitude == 0.0f) {
    return 0.0f;
  }

  const float base = static_cast<float>(time_ms) * kDegToRad * amplitude;

  const float wave = std::cos(kWaveFreq1 * base) +
                     std::cos(kWaveFreq2 * base) +
                     std::cos(kWaveFreq3 * base);

  return wave * kWaveAvgFactor * (amplitude * kWaveAmpScale);
}

}

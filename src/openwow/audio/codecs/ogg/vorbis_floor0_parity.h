#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>

namespace openwow::audio {

namespace detail {

constexpr float kVorbisFloor0Pi = 3.14159265358979323846f;
constexpr float kVorbisFloor0CosScale = 128.0f / kVorbisFloor0Pi;

inline float ComputeVorbisFloor0Bark(const float frequency_hz) noexcept {
  const double x = static_cast<double>(frequency_hz);
  return static_cast<float>(
      std::atan(1.85e-8 * x * x) * 2.24 + std::atan(7.4e-4 * x) * 13.1 + x * 1.0e-4);
}

inline const std::array<float, 129> &VorbisFloor0CosTable() {
  static const std::array<float, 129> table = [] {
    std::array<float, 129> values{};
    for (std::size_t i = 0; i < values.size(); ++i) {
      values[i] = std::cos(static_cast<float>(i) * (kVorbisFloor0Pi / 128.0f));
    }
    return values;
  }();
  return table;
}

inline const std::array<float, 33> &VorbisFloor0InverseSqrtTable() {
  static const std::array<float, 33> table = [] {
    std::array<float, 33> values{};
    for (std::size_t i = 0; i < values.size(); ++i) {
      const float mantissa = 0.5f + static_cast<float>(i) / 64.0f;
      values[i] = 1.0f / std::sqrt(mantissa);
    }
    return values;
  }();
  return table;
}

}

inline int BuildVorbisFloor0BarkMap(const int order, const int sample_rate,
                                    const int bark_map_size, const int spectrum_bins,
                                    int *const map_out) noexcept {
  if (!map_out || order <= 0 || sample_rate <= 0 || bark_map_size <= 0 || spectrum_bins <= 0) {
    return 0;
  }
  (void)order;

  const float bark_limit = detail::ComputeVorbisFloor0Bark(static_cast<float>(sample_rate) * 0.5f);
  if (!(bark_limit > 0.0f)) {
    return 0;
  }

  const float bark_scale = static_cast<float>(bark_map_size) / bark_limit;
  const float hz_per_bin = (static_cast<float>(sample_rate) * 0.5f) /
                           static_cast<float>(spectrum_bins);
  const int max_entry = bark_map_size - 1;
  for (int i = 0; i < spectrum_bins; ++i) {
    const float linear_frequency = static_cast<float>(i) * hz_per_bin;
    int entry = static_cast<int>(std::floor(detail::ComputeVorbisFloor0Bark(linear_frequency) *
                                            bark_scale));
    if (entry > max_entry) {
      entry = max_entry;
    }
    map_out[i] = entry;
  }
  return spectrum_bins;
}

inline float InterpolateVorbisFloor0Cos(const float radians) noexcept {
  const auto &table = detail::VorbisFloor0CosTable();
  if (radians <= 0.0f) {
    return table.front();
  }
  if (radians >= detail::kVorbisFloor0Pi) {
    return table.back();
  }

  const float lookup = radians * detail::kVorbisFloor0CosScale;
  int index = static_cast<int>(lookup - 0.5f);
  index = std::clamp(index, 0, 127);
  return (table[index + 1] - table[index]) * (lookup - static_cast<float>(index)) + table[index];
}

inline float ComputeVorbisFloor0InverseSqrt(const float value, const int order) noexcept {
  if (!(value > 0.0f)) {
    return 0.0f;
  }

  int exponent = 0;
  const float mantissa = std::frexp(value, &exponent);
  const float lookup = mantissa * 64.0f - 32.0f;
  const auto &table = detail::VorbisFloor0InverseSqrtTable();
  int index = static_cast<int>(lookup - 0.5f);
  index = std::clamp(index, 0, 31);
  const float interpolated =
      (table[index + 1] - table[index]) * (lookup - static_cast<float>(index)) + table[index];
  return interpolated * std::exp2(-0.5f * static_cast<float>(order + exponent));
}

inline float ComputeVorbisFloor0LinearGain(const float curve_value, const int order,
                                           const float amplitude,
                                           const float amplitude_offset) noexcept {
  if (!(curve_value > 0.0f)) {
    return 0.0f;
  }

  const float db = amplitude * ComputeVorbisFloor0InverseSqrt(curve_value, order) -
                   amplitude_offset;
  if (db >= 0.0f) {
    return 1.0f;
  }
  return std::pow(10.0f, db * 0.05f);
}

}

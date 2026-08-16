
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace openwow::render {

inline constexpr std::size_t kGammaRampEntryCount = 256;

struct GammaRamp {
  std::array<std::uint16_t, kGammaRampEntryCount> red{};
  std::array<std::uint16_t, kGammaRampEntryCount> green{};
  std::array<std::uint16_t, kGammaRampEntryCount> blue{};
};

static_assert(sizeof(GammaRamp) == 0x600, "GammaRamp must match the retail layout");

struct GammaTransferChannel {
  float minimum{0.0f};
  float maximum{1.0f};
  float exponent{1.0f};
};

struct GammaTransferParameters {
  GammaTransferChannel red{};
  GammaTransferChannel green{};
  GammaTransferChannel blue{};

  [[nodiscard]] static constexpr GammaTransferParameters UniformPower(float exponent) {
    const GammaTransferChannel channel{0.0f, 1.0f, exponent};
    return {channel, channel, channel};
  }
};

static_assert(sizeof(GammaTransferParameters) == sizeof(float) * 9u,
              "Gamma transfer parameters must remain three contiguous RGB triples");

inline GammaRamp BuildGammaRamp(float gamma) {
  GammaRamp ramp;
  for (std::size_t index = 0; index < kGammaRampEntryCount; ++index) {
    const double normalized =
        static_cast<double>(index) / static_cast<double>(kGammaRampEntryCount - 1u);
    const auto value = static_cast<std::uint16_t>(
        std::pow(normalized, static_cast<double>(gamma)) * 65535.0);
    ramp.red[index] = value;
    ramp.green[index] = value;
    ramp.blue[index] = value;
  }
  return ramp;
}

namespace detail {

inline std::uint16_t ApplyGammaTransferChannel(std::uint16_t calibrated_value,
                                               float normalized,
                                               const GammaTransferChannel& channel) {
  const double shaped =
      static_cast<double>(channel.minimum) +
      static_cast<double>(channel.maximum - channel.minimum) *
          std::pow(static_cast<double>(normalized),
                   static_cast<double>(channel.exponent));
  const float corrected =
      static_cast<float>(calibrated_value) +
      static_cast<float>((shaped - static_cast<double>(normalized)) * 65535.0);

  if (!(corrected > 0.0f)) {
    return 0u;
  }
  if (corrected >= 65535.0f) {
    return 65535u;
  }
  return static_cast<std::uint16_t>(corrected);
}

}

inline GammaRamp BuildCalibratedGammaRamp(
    const GammaRamp& desktop_calibration,
    const GammaTransferParameters& parameters) {
  GammaRamp ramp;
  float normalized = 0.0f;
  constexpr float step = 1.0f / static_cast<float>(kGammaRampEntryCount);

  for (std::size_t index = 0; index < kGammaRampEntryCount; ++index) {
    ramp.red[index] = detail::ApplyGammaTransferChannel(
        desktop_calibration.red[index], normalized, parameters.red);
    ramp.green[index] = detail::ApplyGammaTransferChannel(
        desktop_calibration.green[index], normalized, parameters.green);
    ramp.blue[index] = detail::ApplyGammaTransferChannel(
        desktop_calibration.blue[index], normalized, parameters.blue);
    normalized += step;
  }
  return ramp;
}

inline GammaRamp BuildCalibratedGammaRamp(const GammaRamp& desktop_calibration,
                                          float gamma) {
  return BuildCalibratedGammaRamp(
      desktop_calibration, GammaTransferParameters::UniformPower(gamma));
}

}

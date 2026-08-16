#include "openwow/render/effects/ffx_glow.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>

namespace openwow::render {
namespace {

constexpr std::uint32_t kMinimumFfxDimension = 8u;
constexpr std::uint32_t kGlowWavePeriodXMs = 3174u;
constexpr std::uint32_t kGlowWavePeriodYMs = 2805u;
constexpr float kGlowWavePixelScale = 1.0f / 128.0f;
constexpr float kGlowWaveYScale = 0.88f;
constexpr float kPixelToWaveInput =
    (1.0f / 128.0f) * 6.2831855f * 0.31830987f;

std::uint32_t RoundUpPowerOfTwo(const std::uint32_t value) noexcept {
  if (value <= 1u) {
    return 1u;
  }
  if (value > 0x80000000u) {
    return std::numeric_limits<std::uint32_t>::max();
  }
  return std::bit_ceil(value);
}

float RetailCosineApproximation(const float value) noexcept {

  std::int32_t integral = 0;
  if (value <= 0.0f) {
    integral = static_cast<std::int32_t>(value) - 1;
  } else {
    integral = static_cast<std::int32_t>(value);
  }
  const float fraction = value - static_cast<float>(integral);
  float result = 1.0f - fraction * (6.0f - 4.0f * fraction) * fraction;
  if ((integral & 1) != 0) {
    result = -result;
  }
  return result;
}

std::int8_t EncodeSignedWave(const float value) noexcept {
  const float scaled = std::clamp(value * 128.0f, -128.0f, 127.0f);
  return static_cast<std::int8_t>(static_cast<std::int32_t>(scaled));
}

std::uint8_t EncodeUnsignedWave(const float value) noexcept {
  const float scaled = (value * 0.5f + 0.5f) * 255.0f;
  if (!(scaled >= 0.0f)) {
    return 0u;
  }

  if (!(scaled < 255.0f)) {
    return 0x7fu;
  }
  return static_cast<std::uint8_t>(static_cast<std::uint32_t>(scaled));
}

}

FfxTextureAllocation ResolveFfxTextureAllocation(
    const FfxExtent requested,
    const FfxTextureMode mode,
    const FfxExtent maximum) noexcept {
  FfxExtent backing = requested;
  if (mode == FfxTextureMode::kPowerOfTwo) {
    backing.width = RoundUpPowerOfTwo(backing.width);
    backing.height = RoundUpPowerOfTwo(backing.height);
  }

  backing.width = std::min(backing.width, maximum.width);
  backing.height = std::min(backing.height, maximum.height);
  backing.width = std::max(backing.width, kMinimumFfxDimension);
  backing.height = std::max(backing.height, kMinimumFfxDimension);

  return {
      .logical = requested,
      .backing = backing,
      .reciprocal_width = 1.0f / static_cast<float>(backing.width),
      .reciprocal_height = 1.0f / static_cast<float>(backing.height),
  };
}

FfxTargetPyramid ResolveFfxTargetPyramid(const FfxExtent viewport,
                                         const FfxTextureMode mode,
                                         const FfxExtent maximum) noexcept {

  const FfxExtent basis{
      .width = std::max(kMinimumFfxDimension,
                        std::min(viewport.width, maximum.width)),
      .height = std::max(kMinimumFfxDimension,
                         std::min(viewport.height, maximum.height)),
  };

  FfxTargetPyramid pyramid;
  pyramid.full = ResolveFfxTextureAllocation(basis, mode, maximum);
  pyramid.half = ResolveFfxTextureAllocation(
      {basis.width / 2u, basis.height / 2u},
      mode, maximum);
  pyramid.quarter = ResolveFfxTextureAllocation(
      {basis.width / 4u, basis.height / 4u},
      mode, maximum);
  return pyramid;
}

FfxViewportScale ResolveFfxViewportScale(const FfxExtent viewport,
                                         const FfxExtent target) noexcept {
  const auto resolve = [](const std::uint32_t viewport_dimension,
                          const std::uint32_t target_dimension) {
    if (viewport_dimension == 0u) {
      return 1.0f;
    }
    return std::min(1.0f,
                    static_cast<float>(target_dimension) /
                        static_cast<float>(viewport_dimension));
  };
  return {
      .x = resolve(viewport.width, target.width),
      .y = resolve(viewport.height, target.height),
  };
}

std::vector<std::uint16_t> BuildFfxGridIndices(const std::uint32_t columns,
                                               const std::uint32_t rows) {
  if (columns < 2u || rows < 2u) {
    return {};
  }

  const std::uint64_t vertex_count =
      static_cast<std::uint64_t>(columns) * rows;
  const std::uint64_t cell_count =
      static_cast<std::uint64_t>(columns - 1u) * (rows - 1u);
  if (vertex_count > 65536u ||
      cell_count > std::numeric_limits<std::size_t>::max() / 6u) {
    throw std::length_error("FFX grid exceeds 16-bit index range");
  }

  std::vector<std::uint16_t> indices;
  indices.reserve(static_cast<std::size_t>(cell_count * 6u));
  for (std::uint32_t y = 0; y < rows - 1u; ++y) {
    for (std::uint32_t x = 0; x < columns - 1u; ++x) {
      const auto top_left = static_cast<std::uint16_t>(y * columns + x);
      const auto top_right = static_cast<std::uint16_t>(top_left + 1u);
      const auto bottom_left = static_cast<std::uint16_t>(top_left + columns);
      const auto bottom_right = static_cast<std::uint16_t>(bottom_left + 1u);
      indices.insert(indices.end(), {
          top_left, bottom_right, bottom_left,
          top_left, top_right, bottom_right,
      });
    }
  }
  return indices;
}

FfxGlowWaveLut BuildFfxGlowWaveLut(const FfxGlowWaveLutFormat format) {
  const std::uint32_t bytes_per_pixel =
      format == FfxGlowWaveLutFormat::kRgba8 ? 4u : 2u;
  FfxGlowWaveLut lut{
      .format = format,
      .row_stride = FfxGlowWaveLut::kWidth * bytes_per_pixel,
  };
  lut.pixels.resize(static_cast<std::size_t>(lut.row_stride) *
                    FfxGlowWaveLut::kHeight);

  for (std::uint32_t y = 0; y < FfxGlowWaveLut::kHeight; ++y) {
    const float y_input = static_cast<float>(y) * kPixelToWaveInput - 0.5f;
    const float y_wave = RetailCosineApproximation(y_input);
    for (std::uint32_t x = 0; x < FfxGlowWaveLut::kWidth; ++x) {
      const float x_input = static_cast<float>(x) * kPixelToWaveInput - 0.5f;
      const float x_wave = RetailCosineApproximation(x_input);
      const std::size_t offset =
          static_cast<std::size_t>(y) * lut.row_stride + x * bytes_per_pixel;

      if (format == FfxGlowWaveLutFormat::kSignedRg8) {
        lut.pixels[offset] = static_cast<std::uint8_t>(EncodeSignedWave(x_wave));
        lut.pixels[offset + 1u] =
            static_cast<std::uint8_t>(EncodeSignedWave(y_wave));
      } else {
        lut.pixels[offset] = 0u;
        lut.pixels[offset + 1u] = EncodeUnsignedWave(y_wave);
        lut.pixels[offset + 2u] = EncodeUnsignedWave(x_wave);
        lut.pixels[offset + 3u] = 0u;
      }
    }
  }
  return lut;
}

FfxGlowWaveTransform ResolveFfxGlowWaveTransform(
    const std::uint32_t tick_ms,
    const FfxExtent render_extent) noexcept {
  return {
      .translation_x = static_cast<float>(tick_ms % kGlowWavePeriodXMs) /
                       static_cast<float>(kGlowWavePeriodXMs),
      .translation_y = static_cast<float>(tick_ms % kGlowWavePeriodYMs) /
                       static_cast<float>(kGlowWavePeriodYMs),
      .scale_x = static_cast<float>(render_extent.width) * kGlowWavePixelScale,
      .scale_y = static_cast<float>(render_extent.height) * kGlowWavePixelScale *
                 kGlowWaveYScale,
  };
}

}

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace openwow::render {

struct FfxExtent {
  std::uint32_t width = 0;
  std::uint32_t height = 0;

  friend constexpr bool operator==(const FfxExtent&, const FfxExtent&) = default;
};

enum class FfxTextureMode : std::uint8_t {
  kPowerOfTwo = 0,
  kNonPowerOfTwo = 2,
  kRectangle = 3,
};

[[nodiscard]] constexpr FfxTextureMode SelectFfxTextureMode(
    const bool rectangle_cvar_enabled,
    const bool extended_modes_allowed,
    const bool rectangle_supported,
    const bool non_power_of_two_supported) noexcept {
  if (rectangle_cvar_enabled && extended_modes_allowed) {
    if (rectangle_supported) {
      return FfxTextureMode::kRectangle;
    }
    if (non_power_of_two_supported) {
      return FfxTextureMode::kNonPowerOfTwo;
    }
  }
  return FfxTextureMode::kPowerOfTwo;
}

struct FfxTextureAllocation {
  FfxExtent logical{};
  FfxExtent backing{};
  float reciprocal_width = 0.0f;
  float reciprocal_height = 0.0f;

  friend constexpr bool operator==(const FfxTextureAllocation&,
                                   const FfxTextureAllocation&) = default;
};

[[nodiscard]] FfxTextureAllocation ResolveFfxTextureAllocation(
    FfxExtent requested,
    FfxTextureMode mode,
    FfxExtent maximum) noexcept;

struct FfxTargetPyramid {
  FfxTextureAllocation full{};
  FfxTextureAllocation half{};
  FfxTextureAllocation quarter{};
};

[[nodiscard]] FfxTargetPyramid ResolveFfxTargetPyramid(
    FfxExtent viewport,
    FfxTextureMode mode,
    FfxExtent maximum) noexcept;

struct FfxViewportScale {
  float x = 1.0f;
  float y = 1.0f;
};

[[nodiscard]] FfxViewportScale ResolveFfxViewportScale(
    FfxExtent viewport,
    FfxExtent target) noexcept;

[[nodiscard]] constexpr FfxViewportScale ResolveFfxTextureUvScale(
    const FfxTextureAllocation& allocation) noexcept {
  return {
      .x = allocation.backing.width == 0u
               ? 1.0f
               : static_cast<float>(allocation.logical.width) /
                     static_cast<float>(allocation.backing.width),
      .y = allocation.backing.height == 0u
               ? 1.0f
               : static_cast<float>(allocation.logical.height) /
                     static_cast<float>(allocation.backing.height),
  };
}

[[nodiscard]] constexpr std::uint8_t EncodeFfxVertexIntensity(
    const float intensity) noexcept {
  return static_cast<std::uint8_t>(
      static_cast<std::int32_t>(intensity * 127.5f + 127.5f));
}

struct FfxSampleOffset {
  float x = 0.0f;
  float y = 0.0f;

  friend constexpr bool operator==(const FfxSampleOffset&,
                                   const FfxSampleOffset&) = default;
};

inline constexpr std::array<FfxSampleOffset, 4> kFfxBox4SampleOffsets{{
    {-1.5f, -1.5f},
    {0.5f, -1.5f},
    {0.5f, 0.5f},
    {-1.5f, 0.5f},
}};
inline constexpr std::array<float, 4> kFfxGauss4SampleOffsets{
    -2.5f, -0.5f, 0.5f, 2.5f};

inline constexpr std::array<float, 4> kFfxGauss4SampleWeights{
    0.125f, 0.375f, 0.375f, 0.125f};
static_assert(kFfxGauss4SampleWeights[0] + kFfxGauss4SampleWeights[1] +
                      kFfxGauss4SampleWeights[2] + kFfxGauss4SampleWeights[3] ==
                  1.0f,
              "FFXGauss4 is energy-preserving");
inline constexpr std::array<std::uint16_t, 4> kFfxQuadStripIndices{
    0u, 3u, 1u, 2u};

[[nodiscard]] std::vector<std::uint16_t> BuildFfxGridIndices(
    std::uint32_t columns,
    std::uint32_t rows);

enum class FfxGlowPassKind : std::uint8_t {
  kBox4,
  kGauss4,
  kGlow,
  kGlowWave,
};

struct FfxGlowModePayload {

  std::uint32_t alternate_chain = 0;
  std::uint32_t glow_coefficient = 0;
  std::uint32_t effect_intensity = 0;
};

static_assert(sizeof(FfxGlowModePayload) == 12);
static_assert(offsetof(FfxGlowModePayload, glow_coefficient) == 4);
static_assert(offsetof(FfxGlowModePayload, effect_intensity) == 8);

[[nodiscard]] constexpr std::uint32_t PackFfxGlowVertexColor(
    const FfxGlowModePayload payload) noexcept {
  const auto glow = static_cast<std::uint8_t>(payload.glow_coefficient);
  const auto effect = static_cast<std::uint8_t>(payload.effect_intensity);
  return (static_cast<std::uint32_t>(glow) << 24u) |
         static_cast<std::uint32_t>(effect) * 0x00010101u;
}

struct FfxGlowChainPlan {
  std::array<FfxGlowPassKind, 3> primary{
      FfxGlowPassKind::kBox4,
      FfxGlowPassKind::kGauss4,
      FfxGlowPassKind::kGlow,
  };
  std::array<FfxGlowPassKind, 3> alternate{
      FfxGlowPassKind::kBox4,
      FfxGlowPassKind::kGauss4,
      FfxGlowPassKind::kGlow,
  };
  bool use_alternate = false;
  std::uint32_t vertex_color = 0;

  [[nodiscard]] constexpr std::span<const FfxGlowPassKind> ActivePasses()
      const noexcept {
    return use_alternate ? std::span<const FfxGlowPassKind>(alternate)
                         : std::span<const FfxGlowPassKind>(primary);
  }
};

[[nodiscard]] constexpr FfxGlowChainPlan MakeFfxGlowChainPlan(
    const bool use_glow_wave_fallback) noexcept {
  FfxGlowChainPlan plan;
  if (use_glow_wave_fallback) {
    plan.alternate.back() = FfxGlowPassKind::kGlowWave;
  }
  return plan;
}

constexpr void SetFfxGlowMode(FfxGlowChainPlan& plan,
                              const FfxGlowModePayload payload) noexcept {
  plan.use_alternate = payload.alternate_chain != 0u;
  plan.vertex_color = PackFfxGlowVertexColor(payload);
}

enum class FfxGlowWaveLutFormat : std::uint8_t {
  kRgba8 = 2,
  kSignedRg8 = 9,
};

[[nodiscard]] constexpr FfxGlowWaveLutFormat SelectFfxGlowWaveLutFormat(
    const std::uint32_t gpu_tier) noexcept {
  switch (gpu_tier) {
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 11:
    case 12:
      return FfxGlowWaveLutFormat::kRgba8;
    default:
      return FfxGlowWaveLutFormat::kSignedRg8;
  }
}

struct FfxGlowWaveLut {
  static constexpr std::uint32_t kWidth = 128;
  static constexpr std::uint32_t kHeight = 128;

  FfxGlowWaveLutFormat format = FfxGlowWaveLutFormat::kSignedRg8;
  std::uint32_t row_stride = 0;
  std::vector<std::uint8_t> pixels;
};

[[nodiscard]] FfxGlowWaveLut BuildFfxGlowWaveLut(
    FfxGlowWaveLutFormat format);

struct FfxGlowWaveTransform {
  float translation_x = 0.0f;
  float translation_y = 0.0f;
  float scale_x = 0.0f;
  float scale_y = 0.0f;
  float rotation_radians = 0.17453294f;
};

[[nodiscard]] FfxGlowWaveTransform ResolveFfxGlowWaveTransform(
    std::uint32_t tick_ms,
    FfxExtent render_extent) noexcept;

}

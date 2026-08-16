#pragma once

#include "openwow/data/formats/dbc/dbc_entries_world.h"
#include "openwow/render/api/math/render_math_types.h"

#include <array>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <limits>

namespace openwow::render {

inline constexpr float kRetailLiquidRotationMultiplier = 57.29578f;

struct LiquidPixelShaderMaterialConstants {
  RenderVec4 reciprocal_scale_and_params{};
  RenderVec4 trailing_params{};
};

struct ProceduralLiquidWaveRecord {
  float falloff_origin_u{0.0f};
  float falloff_origin_v{0.0f};
  float phase_vector_u{0.0f};
  float phase_vector_v{0.0f};
  float spatial_period{0.0f};
  float falloff_radius{0.0f};
  float amplitude{0.0f};
  float tick_scale{0.0f};
};

static_assert(sizeof(ProceduralLiquidWaveRecord) == 32u);

struct ProceduralLiquidWaveState {
  std::array<ProceduralLiquidWaveRecord, 3> waves{};
};

struct ProceduralLiquidVertexConstants {
  RenderVec4 phases{};
  RenderVec4 inverse_falloff_radii{};
  std::array<RenderVec4, 3> waves{};
  RenderVec4 inverse_spatial_periods{};
  RenderVec4 amplitudes{};
};

struct ProceduralLiquidTextureConstants {

  std::array<RenderVec4, 6> transforms{};
};

[[nodiscard]] inline float LiquidSpatialReciprocal(
    const float value) noexcept {

  return value >= 0.001f || std::isnan(value) ? 1.0f / value : 999.99994f;
}

[[nodiscard]] inline float LiquidFalloffReciprocal(
    const float value) noexcept {

  return value < 0.001f ? 999.99994f : 1.0f / value;
}

[[nodiscard]] inline float BuildProceduralLiquidPhase(
    const std::uint32_t tick_count_ms, const float tick_scale) noexcept {
  constexpr float kPhaseIndexScale = 0.0009765625f;
  constexpr float kPi = 3.1415927f;

  const float scaled_tick = static_cast<float>(tick_count_ms) * tick_scale;
  std::uint32_t truncated_tick = 0u;
  if (!std::isnan(scaled_tick) && scaled_tick > 0.0f) {
    constexpr float kTwoToThe32 = 4294967296.0f;
    truncated_tick = scaled_tick >= kTwoToThe32
                         ? std::numeric_limits<std::uint32_t>::max()
                         : static_cast<std::uint32_t>(scaled_tick);
  }
  const float half_phase =
      static_cast<float>(truncated_tick & 0x1fffu) * kPhaseIndexScale * kPi;
  return half_phase + half_phase;
}

[[nodiscard]] inline ProceduralLiquidVertexConstants
BuildProceduralLiquidVertexConstants(
    const ProceduralLiquidWaveState& state,
    const std::uint32_t tick_count_ms) noexcept {
  ProceduralLiquidVertexConstants constants{};
  for (std::size_t index = 0u; index < state.waves.size(); ++index) {
    const ProceduralLiquidWaveRecord& wave = state.waves[index];
    constants.phases[index] =
        BuildProceduralLiquidPhase(tick_count_ms, wave.tick_scale);
    constants.inverse_falloff_radii[index] =
        LiquidFalloffReciprocal(wave.falloff_radius);
    constants.waves[index] = {wave.falloff_origin_u,
                              wave.falloff_origin_v,
                              wave.phase_vector_u,
                              wave.phase_vector_v};
    constants.inverse_spatial_periods[index] =
        LiquidSpatialReciprocal(wave.spatial_period);
    constants.amplitudes[index] = wave.amplitude;
  }
  return constants;
}

[[nodiscard]] inline RenderVec4 BuildProceduralLiquidRotationScale(
    const float scale, const float liquid_type_angle) noexcept {
  const float angle =
      liquid_type_angle * kRetailLiquidRotationMultiplier;
  const float cosine = std::cos(angle) * scale;
  const float sine = std::sin(angle) * scale;
  return {cosine, sine, -sine, cosine};
}

[[nodiscard]] inline ProceduralLiquidTextureConstants
BuildProceduralLiquidTextureConstants(
    const data::dbc::LiquidTypeEntry& liquid_type) noexcept {
  ProceduralLiquidTextureConstants constants{};
  for (std::size_t index = 0u; index < 4u; ++index) {
    constants.transforms[index] = BuildProceduralLiquidRotationScale(
        liquid_type.float_params[index], liquid_type.float_params[index + 4u]);
  }
  constants.transforms[4] =
      {1.0f, 0.0f, 0.0f, liquid_type.float_params[8]};
  constants.transforms[5] = BuildProceduralLiquidRotationScale(
      liquid_type.float_params[9], liquid_type.float_params[10]);
  return constants;
}

[[nodiscard]] inline LiquidPixelShaderMaterialConstants
BuildLiquidPixelShaderMaterialConstants(
    const data::dbc::LiquidTypeEntry& liquid_type) noexcept {
  LiquidPixelShaderMaterialConstants constants{};
  constants.reciprocal_scale_and_params = {
      LiquidSpatialReciprocal(liquid_type.float_params[11]),
      liquid_type.float_params[12], liquid_type.float_params[13],
      liquid_type.float_params[14]};
  constants.trailing_params = {
      liquid_type.float_params[17], liquid_type.float_params[15],
      liquid_type.float_params[16], 0.0f};
  return constants;
}

}

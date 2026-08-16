#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "openwow/data/wmo/wmo_file.h"
#include "openwow/render/api/math/render_math_types.h"

namespace openwow::render {

struct WmoLightingPalette {
  RenderVec3 outdoor_ambient{};
  RenderVec3 outdoor_diffuse{};
  RenderVec3 window_ambient{};
  RenderVec3 window_diffuse{};
  std::uint32_t material_ambient_argb{};
};

struct WmoVertexLightingInput {
  RenderVec3 normal{0.0f, 0.0f, 1.0f};
  RenderVec3 surface_to_light{0.0f, 0.0f, 1.0f};
  RenderVec3 ambient{1.0f, 1.0f, 1.0f};
  RenderVec3 diffuse{};
  RenderVec3 vertex_color{1.0f, 1.0f, 1.0f};
  RenderVec3 material_color{127.0f / 255.0f, 127.0f / 255.0f, 127.0f / 255.0f};
  RenderVec3 emissive{};
  bool lighting_enabled{true};
  bool unified_render_path{false};
};

[[nodiscard]] inline float SaturateWmo(const float value) noexcept {
  return std::clamp(value, 0.0f, 1.0f);
}

[[nodiscard]] inline RenderVec3 NormalizeWmoDirection(const RenderVec3 &value) noexcept {
  const float length_squared = value[0] * value[0] + value[1] * value[1] + value[2] * value[2];
  if (length_squared <= 1.0e-12f) {
    return {};
  }
  const float inverse_length = 1.0f / std::sqrt(length_squared);
  return {value[0] * inverse_length, value[1] * inverse_length, value[2] * inverse_length};
}

[[nodiscard]] inline RenderVec3
EvaluateRetailWmoVertexColor(const WmoVertexLightingInput &input) noexcept {
  if (!input.lighting_enabled) {
    return input.vertex_color;
  }

  const RenderVec3 normal = NormalizeWmoDirection(input.normal);
  const RenderVec3 surface_to_light = NormalizeWmoDirection(input.surface_to_light);
  const float ndl = SaturateWmo(normal[0] * surface_to_light[0] + normal[1] * surface_to_light[1] +
                                normal[2] * surface_to_light[2]);

  RenderVec3 output{};
  for (std::size_t channel = 0; channel < output.size(); ++channel) {
    const float light = SaturateWmo(input.ambient[channel] + input.diffuse[channel] * ndl);
    const float base = input.unified_render_path
                           ? light * input.material_color[channel] + input.vertex_color[channel]
                           : input.vertex_color[channel] * light;
    output[channel] = SaturateWmo(base + input.emissive[channel]);
  }
  return output;
}

[[nodiscard]] inline std::uint8_t ScaleRetailWmoNightGlowChannel(const std::uint8_t channel,
                                                                 const float intensity) noexcept {
  const auto scale = static_cast<std::uint32_t>(SaturateWmo(intensity) * 255.0f);
  return static_cast<std::uint8_t>((static_cast<std::uint32_t>(channel) * scale) >> 8u);
}

[[nodiscard]] inline RenderVec4 EvaluateRetailWmoMaterialEmissive(
    const std::uint32_t material_ambient_argb,
    const std::uint32_t sidn_argb, const float intensity,
    const bool sidn_enabled) noexcept {
  const auto half_sum = [=](const std::uint32_t shift) {
    const std::uint32_t ambient =
        (material_ambient_argb >> shift) & 0xffu;
    const std::uint32_t sidn = sidn_enabled
                                   ? ScaleRetailWmoNightGlowChannel(
                                         static_cast<std::uint8_t>(
                                             (sidn_argb >> shift) & 0xffu),
                                         intensity)
                                   : 0u;
    return static_cast<float>((std::min(ambient + sidn, 255u)) >> 1u) /
           255.0f;
  };
  return {
      half_sum(16u),
      half_sum(8u),
      half_sum(0u),
      0.0f,
  };
}

struct WmoPixelMaterialInput {
  RenderVec3 lit_vertex_color{1.0f, 1.0f, 1.0f};
  RenderVec4 diffuse_texture{1.0f, 1.0f, 1.0f, 1.0f};
  RenderVec3 environment_texture{};
  RenderVec3 secondary_texture{};
  RenderVec3 separate_vertex_color{};
  RenderVec3 separate_specular_color{};
  float vertex_alpha{1.0f};
  data::wmo::WmoShaderType shader{data::wmo::kShaderDiffuse};
};

[[nodiscard]] inline RenderVec4
EvaluateRetailWmoPixelMaterial(const WmoPixelMaterialInput &input) noexcept {
  RenderVec4 output{};
  for (std::size_t channel = 0; channel < 3u; ++channel) {
    const float texture_color = input.shader == data::wmo::kShaderTwoLayer
                                    ? input.diffuse_texture[channel] +
                                          (input.secondary_texture[channel] -
                                           input.diffuse_texture[channel]) *
                                              input.vertex_alpha
                                    : input.diffuse_texture[channel];
    const float diffuse = 2.0f * input.lit_vertex_color[channel] * texture_color;
    float environment = 0.0f;
    if (input.shader == data::wmo::kShaderEnv) {
      environment = input.diffuse_texture[3] * input.environment_texture[channel];
    } else if (input.shader == data::wmo::kShaderEnvMetal) {
      environment = input.diffuse_texture[channel] * input.diffuse_texture[3] *
                    input.environment_texture[channel];
    }
    const float separate =
        (input.shader == data::wmo::kShaderSpecular ||
         input.shader == data::wmo::kShaderMetal)
            ? input.separate_specular_color[channel]
            : (input.shader == data::wmo::kShaderTwoLayer
                   ? input.separate_vertex_color[channel]
                   : 0.0f);
    output[channel] = diffuse + environment + separate;
  }

  output[3] = input.shader == data::wmo::kShaderDiffuse
                  ? input.vertex_alpha * input.diffuse_texture[3]
                  : input.vertex_alpha;
  return output;
}

}

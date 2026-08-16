#include "openwow/render/m2/m2_shaders.h"

#include "openwow/render/m2/m2_skin_geometry.h"
#include "openwow/render/models/animation/model_light_record.h"
#include "openwow/render/backend/bgfx/embedded_shaders.h"
#include "openwow/render/resources/shaders/shader_registry.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace openwow::render::m2 {

namespace {

void SafeDestroy(bgfx::UniformHandle &h) {
  if (bgfx::isValid(h)) {
    bgfx::destroy(h);
    h = BGFX_INVALID_HANDLE;
  }
}
void SafeDestroy(bgfx::ProgramHandle &h) {
  if (bgfx::isValid(h)) {
    bgfx::destroy(h);
    h = BGFX_INVALID_HANDLE;
  }
}

constexpr std::array<M2MaterialFogMode, static_cast<std::size_t>(M2BlendMode::Count)>
    kM2BlendModeToMaterialFogMode = {
        M2MaterialFogMode::SceneColor, M2MaterialFogMode::SceneColor,
        M2MaterialFogMode::SceneColor, M2MaterialFogMode::Black,
        M2MaterialFogMode::Black,      M2MaterialFogMode::White,
        M2MaterialFogMode::Gray};

[[nodiscard]] bgfx::ProgramHandle CreateM2PermutationProgram(
    const bgfx::RendererType::Enum type, const std::string& vertex_shader,
    const char* const fragment_shader) {
  const bgfx::ShaderHandle vertex = bgfx::createEmbeddedShader(
      openwow::render::s_m2Shaders, type, vertex_shader.c_str());
  const bgfx::ShaderHandle fragment = bgfx::createEmbeddedShader(
      openwow::render::s_m2Shaders, type, fragment_shader);
  if (!bgfx::isValid(vertex) || !bgfx::isValid(fragment)) {
    if (bgfx::isValid(vertex)) {
      bgfx::destroy(vertex);
    }
    if (bgfx::isValid(fragment)) {
      bgfx::destroy(fragment);
    }
    return BGFX_INVALID_HANDLE;
  }
  return bgfx::createProgram(vertex, fragment, true);
}

void BuildM2PermutationTable(M2BonePaletteShader& palette_shader,
                             const bgfx::RendererType::Enum type,
                             const std::string& vertex_stem) {
  for (std::size_t vertex_index = 0; vertex_index < kM2VertexVariantCount;
       ++vertex_index) {
    const std::string vertex_shader =
        vertex_stem + kM2VertexVariantSuffixes[vertex_index];
    for (std::size_t fragment_index = 0;
         fragment_index < kM2FragmentVariantCount; ++fragment_index) {
      const char* const fragment_shader =
          kM2FragmentVariantKeys[fragment_index].shader_name;
      const bgfx::ProgramHandle program =
          CreateM2PermutationProgram(type, vertex_shader, fragment_shader);
      if (!bgfx::isValid(program)) {

        openwow::diagnostics::Log(
            openwow::diagnostics::LogLevel::kWarn,
            "M2 shaders: permutation " + vertex_shader + " + " +
                fragment_shader + " did not link; falling back to the "
                "uniform-branched program");
      }
      palette_shader.permutations[vertex_index][fragment_index] = program;
    }
  }
}

void DestroyM2PermutationTable(M2BonePaletteShader& palette_shader) {
  for (auto& row : palette_shader.permutations) {
    for (auto& program : row) {
      SafeDestroy(program);
    }
  }
}

}

M2ShaderHandles LoadM2Program() {
  M2ShaderHandles out;

  const auto type = bgfx::getRendererType();
  constexpr std::array<openwow::render::ShaderProgramId,
                       kM2BonePaletteCapacities.size()>
      kProgramIds = {
          openwow::render::ShaderProgramId::M2Bones8,
          openwow::render::ShaderProgramId::M2Bones16,
          openwow::render::ShaderProgramId::M2Bones32,
          openwow::render::ShaderProgramId::M2Bones64,
          openwow::render::ShaderProgramId::M2Bones128,
          openwow::render::ShaderProgramId::M2,
      };
  for (std::size_t index = 0; index < out.bone_palette_shaders.size(); ++index) {
    auto& variant = out.bone_palette_shaders[index];
    variant.capacity = kM2BonePaletteCapacities[index];
    variant.program =
        openwow::render::CreateEmbeddedProgram(kProgramIds[index], type);
    const std::string uniform_name =
        "u_boneColumns" + std::to_string(variant.capacity);
    variant.bone_columns = bgfx::createUniform(
        uniform_name.c_str(), bgfx::UniformType::Vec4,
        static_cast<std::uint16_t>(variant.capacity * 3u));
    BuildM2PermutationTable(
        variant, type, "vs_m2_bones_" + std::to_string(variant.capacity));
  }

  out.instanced_shader.capacity =
      static_cast<std::uint16_t>(kM2StaticInstancingMaxBones);
  out.instanced_shader.program = openwow::render::CreateEmbeddedProgram(
      openwow::render::ShaderProgramId::M2Instanced, type);
  out.instanced_shader.bone_columns = bgfx::createUniform(
      "u_boneColumns8", bgfx::UniformType::Vec4,
      static_cast<std::uint16_t>(kM2StaticInstancingMaxBones * 3u));
  BuildM2PermutationTable(out.instanced_shader, type, "vs_m2_instanced");

  out.s_tex0 = bgfx::createUniform("s_m2Tex0", bgfx::UniformType::Sampler);
  out.s_tex1 = bgfx::createUniform("s_m2Tex1", bgfx::UniformType::Sampler);

  out.u_world_matrix =
      bgfx::createUniform("u_m2WorldMtx", bgfx::UniformType::Mat4);

  out.u_vertex_params = bgfx::createUniform(
      "u_m2VertexParams", bgfx::UniformType::Vec4, kM2VertexParamCount);
  out.u_lights = bgfx::createUniform("u_m2Lights", bgfx::UniformType::Vec4,
                                     kM2LightUniformCount);
  out.u_fragment_params = bgfx::createUniform(
      "u_m2FragmentParams", bgfx::UniformType::Vec4, kM2FragmentParamCount);

  const bool variants_valid =
      std::all_of(out.bone_palette_shaders.begin(),
                  out.bone_palette_shaders.end(),
                  [](const M2BonePaletteShader& variant) {
                    return bgfx::isValid(variant.program) &&
                           bgfx::isValid(variant.bone_columns);
                  }) &&
      bgfx::isValid(out.instanced_shader.program) &&
      bgfx::isValid(out.instanced_shader.bone_columns);
  const std::array common_uniforms{
      out.s_tex0,
      out.s_tex1,
      out.u_world_matrix,
      out.u_vertex_params,
      out.u_lights,
      out.u_fragment_params,
  };
  const bool common_uniforms_valid =
      std::all_of(common_uniforms.begin(), common_uniforms.end(),
                  [](const bgfx::UniformHandle handle) {
                    return bgfx::isValid(handle);
                  });
  if (!variants_valid || !common_uniforms_valid) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "M2 shaders: shared program/uniform creation failed");
    DestroyM2Program(out);
  }

  return out;
}

void DestroyM2Program(M2ShaderHandles &h) {
  for (auto& variant : h.bone_palette_shaders) {
    DestroyM2PermutationTable(variant);
    SafeDestroy(variant.program);
    SafeDestroy(variant.bone_columns);
    variant.capacity = 0;
  }
  DestroyM2PermutationTable(h.instanced_shader);
  SafeDestroy(h.instanced_shader.program);
  SafeDestroy(h.instanced_shader.bone_columns);
  h.instanced_shader.capacity = 0;
  SafeDestroy(h.s_tex0);
  SafeDestroy(h.s_tex1);
  SafeDestroy(h.u_world_matrix);
  SafeDestroy(h.u_vertex_params);
  SafeDestroy(h.u_lights);
  SafeDestroy(h.u_fragment_params);
}

const M2BonePaletteShader* FindM2BonePaletteShader(
    const M2ShaderHandles& handles,
    const std::uint32_t bone_count) noexcept {
  const std::uint16_t capacity = ResolveM2BonePaletteCapacity(bone_count);
  if (capacity == 0u) {
    return nullptr;
  }
  const auto it = std::find_if(
      handles.bone_palette_shaders.begin(),
      handles.bone_palette_shaders.end(),
      [capacity](const M2BonePaletteShader& variant) {
        return variant.capacity == capacity;
      });
  return it == handles.bone_palette_shaders.end() ? nullptr : &*it;
}

M2VertexVariant ResolveM2VertexVariant(const M2BatchUniforms& uniforms) noexcept {

  const bool lit = uniforms.material_flags[0] < 0.5f;
  const bool uses_env_texgen = !(uniforms.tex_gen_flags[0] < 1.5f) ||
                               !(uniforms.tex_gen_flags[1] < 1.5f);
  if (lit) {
    return uses_env_texgen ? M2VertexVariant::LitEnv : M2VertexVariant::LitNoEnv;
  }
  return uses_env_texgen ? M2VertexVariant::UnlitEnv
                         : M2VertexVariant::UnlitNoEnv;
}

M2FragmentVariant ResolveM2FragmentVariant(
    const M2BatchUniforms& uniforms) noexcept {

  const auto selector = [&uniforms](const std::size_t channel) {
    return static_cast<int>(uniforms.combiner_mode[channel] + 0.5f);
  };
  const int op1 = selector(0);
  const int op2 = selector(1);
  const int texture_count = selector(2);
  const int special = selector(3);

  for (std::size_t index = 1; index < kM2FragmentVariantCount; ++index) {
    const auto& key = kM2FragmentVariantKeys[index];
    if (key.texture_count == texture_count && key.op1 == op1 &&
        key.op2 == op2 && key.special == special) {
      return static_cast<M2FragmentVariant>(index);
    }
  }
  return M2FragmentVariant::Generic;
}

M2ProgramSelection SelectM2Program(const M2BonePaletteShader& palette_shader,
                                   const M2BatchUniforms& uniforms) noexcept {
  const M2VertexVariant vertex_variant = ResolveM2VertexVariant(uniforms);
  const M2FragmentVariant fragment_variant = ResolveM2FragmentVariant(uniforms);
  const bgfx::ProgramHandle program =
      palette_shader.permutations[static_cast<std::size_t>(vertex_variant)]
                                 [static_cast<std::size_t>(fragment_variant)];
  if (!bgfx::isValid(program)) {

    return {.program = palette_shader.program, .reads_lighting_uniforms = true};
  }
  return {.program = program,
          .reads_lighting_uniforms =
              M2VertexVariantReadsLightingUniforms(vertex_variant)};
}

std::optional<uint64_t> ResolveM2BlendState(const M2BlendMode mode) noexcept {
  switch (mode) {
  case M2BlendMode::Opaque:
    return BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS |
           BGFX_STATE_WRITE_Z;

  case M2BlendMode::AlphaKey:

    return BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS |
           BGFX_STATE_WRITE_Z;

  case M2BlendMode::Alpha:
    return BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS |
           BGFX_STATE_WRITE_Z |
           BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_SRC_ALPHA,
                                          BGFX_STATE_BLEND_INV_SRC_ALPHA,
                                          BGFX_STATE_BLEND_ONE,
                                          BGFX_STATE_BLEND_INV_SRC_ALPHA);

  case M2BlendMode::NoAlphaAdd:
    return BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS |
           BGFX_STATE_WRITE_Z |
           BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE,
                                          BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_ONE);

  case M2BlendMode::Add:
    return BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS |
           BGFX_STATE_WRITE_Z |
           BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE,
                                          BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_ONE);

  case M2BlendMode::Mod:
    return BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS |
           BGFX_STATE_WRITE_Z |
           BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_DST_COLOR, BGFX_STATE_BLEND_ZERO);

  case M2BlendMode::Mod2x:
    return BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS |
           BGFX_STATE_WRITE_Z |
           BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_DST_COLOR, BGFX_STATE_BLEND_SRC_COLOR);

  case M2BlendMode::Count:
    break;
  }
  return std::nullopt;
}

uint64_t ApplyMaterialRenderFlags(uint64_t state, uint16_t flags) {

  if ((flags & kM2MaterialFlagTwoSided) == 0) {
    state |= BGFX_STATE_CULL_CW;
  }

  if ((flags & kM2MaterialFlagDisableDepthTest) != 0) {
    state &= ~BGFX_STATE_DEPTH_TEST_MASK;
  }
  if ((flags & kM2MaterialFlagDisableDepthWrite) != 0) {
    state &= ~BGFX_STATE_WRITE_Z;
  }

  return state;
}

std::optional<uint64_t> ResolveM2RenderState(const M2BlendMode mode,
                                             const uint16_t flags) noexcept {
  const auto blend_state = ResolveM2BlendState(mode);
  if (!blend_state.has_value()) {
    return std::nullopt;
  }
  return ApplyMaterialRenderFlags(*blend_state, flags);
}

bool BatchBelongsToOpaqueList(const std::uint32_t blend_mode, const float opacity) {
  return blend_mode <= static_cast<std::uint32_t>(M2BlendMode::AlphaKey) &&
         opacity >= kM2OpaqueListOpacityThreshold;
}

bool BatchBelongsToOpaqueList(const M2BlendMode mode, const float opacity) {
  return BatchBelongsToOpaqueList(static_cast<std::uint32_t>(mode), opacity);
}

std::optional<float> ResolveM2AlphaRefThreshold(const M2BlendMode mode,
                                                const float batch_alpha) noexcept {
  switch (mode) {
  case M2BlendMode::Opaque:
    return 0.0f;

  case M2BlendMode::AlphaKey:
    return batch_alpha * (224.0f / 255.0f);

  case M2BlendMode::Alpha:
  case M2BlendMode::NoAlphaAdd:
  case M2BlendMode::Add:
  case M2BlendMode::Mod:
  case M2BlendMode::Mod2x:
    return 1.0f / 255.0f;

  case M2BlendMode::Count:
    break;
  }
  return std::nullopt;
}

bool BlendModeUsesMaterialLighting(const M2BlendMode mode) {
  switch (mode) {
  case M2BlendMode::Opaque:
  case M2BlendMode::AlphaKey:
  case M2BlendMode::Alpha:
  case M2BlendMode::NoAlphaAdd:
  case M2BlendMode::Add:
    return true;

  case M2BlendMode::Mod:
  case M2BlendMode::Mod2x:
  case M2BlendMode::Count:
    return false;
  }
  return false;
}

M2MaterialFogMode ResolveM2MaterialFogMode(const M2BlendMode mode,
                                           const bool render_flag_unfogged,
                                           const float effective_alpha) noexcept {
  if (render_flag_unfogged || effective_alpha <= 0.0f) {
    return M2MaterialFogMode::Disabled;
  }

  const auto index = static_cast<std::size_t>(mode);
  if (index >= kM2BlendModeToMaterialFogMode.size()) {
    return M2MaterialFogMode::Disabled;
  }
  return kM2BlendModeToMaterialFogMode[index];
}

RenderVec4 ResolveM2MaterialFogColor(const M2MaterialFogMode mode,
                                     const RenderVec4 &scene_fog_color) noexcept {
  switch (mode) {
  case M2MaterialFogMode::SceneColor:
    return scene_fog_color;
  case M2MaterialFogMode::Black:
    return RenderVec4{0.0f, 0.0f, 0.0f, scene_fog_color[3]};
  case M2MaterialFogMode::White:
    return RenderVec4{1.0f, 1.0f, 1.0f, scene_fog_color[3]};
  case M2MaterialFogMode::Gray:
    return RenderVec4{0.5f, 0.5f, 0.5f, scene_fog_color[3]};
  case M2MaterialFogMode::Disabled:
    return scene_fog_color;
  }
  return scene_fog_color;
}

M2MaterialColorSlots BuildMaterialColorSlots(const M2BlendMode mode, const bool render_flag_unlit,
                                             const RenderVec4 &color) {
  M2MaterialColorSlots out{};
  out.diffuse = color;

  if (mode == M2BlendMode::Mod || mode == M2BlendMode::Mod2x) {
    out.diffuse[0] = 0.0f;
    out.diffuse[1] = 0.0f;
    out.diffuse[2] = 0.0f;
    out.emissive[0] = (mode == M2BlendMode::Mod2x) ? 0.5f : 1.0f;
    out.emissive[1] = out.emissive[0];
    out.emissive[2] = out.emissive[0];
    out.uses_lighting = false;
    return out;
  }

  out.uses_lighting = BlendModeUsesMaterialLighting(mode) && !render_flag_unlit;
  if (!out.uses_lighting) {

    out.emissive[0] = out.diffuse[0];
    out.emissive[1] = out.diffuse[1];
    out.emissive[2] = out.diffuse[2];
    out.diffuse[0] = 0.0f;
    out.diffuse[1] = 0.0f;
    out.diffuse[2] = 0.0f;
  }

  return out;
}

namespace {

RenderVec3 NormalizeM2LightingVector(const RenderVec3 &value) noexcept {
  const float length_squared = value[0] * value[0] + value[1] * value[1] +
                               value[2] * value[2];
  if (!(length_squared > ModelLightRecord::kRetailDirectionNormalizeLengthSqEpsilon)) {
    return {};
  }
  const float inverse_length = 1.0f / std::sqrt(length_squared);
  return {value[0] * inverse_length, value[1] * inverse_length,
          value[2] * inverse_length};
}

float DotM2Lighting(const RenderVec3 &lhs, const RenderVec3 &rhs) noexcept {
  return lhs[0] * rhs[0] + lhs[1] * rhs[1] + lhs[2] * rhs[2];
}

}

RenderVec3 EvaluateM2SurfaceLighting(const M2BatchUniforms &uniforms,
                                     const RenderVec3 &world_normal,
                                     const RenderVec3 &world_position) noexcept {
  const RenderVec3 normal = NormalizeM2LightingVector(world_normal);
  const int light_count = std::clamp(
      static_cast<int>(uniforms.light_count[0] + 0.5f), 0,
      M2BatchUniforms::kMaxM2Lights);
  RenderVec3 lighting{uniforms.light_ambient[0], uniforms.light_ambient[1],
                      uniforms.light_ambient[2]};

  for (int index = 0; index < light_count; ++index) {
    const auto &position_range = uniforms.light_pos_range[static_cast<std::size_t>(index)];
    const auto &attenuation = uniforms.light_attenuation[static_cast<std::size_t>(index)];
    const auto &color = uniforms.light_color[static_cast<std::size_t>(index)];
    float strength = 0.0f;
    if (color[3] > 0.5f) {
      const RenderVec3 to_light{position_range[0] - world_position[0],
                                position_range[1] - world_position[1],
                                position_range[2] - world_position[2]};
      const float distance_squared = DotM2Lighting(to_light, to_light);
      const float distance = distance_squared > 0.0f ? std::sqrt(distance_squared) : 0.0f;
      const RenderVec3 direction = NormalizeM2LightingVector(to_light);
      const float denominator = attenuation[0] + attenuation[1] * distance +
                                attenuation[2] * distance_squared;
      const float falloff = 1.0f / std::max(denominator, 0.0001f);
      strength = std::max(DotM2Lighting(normal, direction), 0.0f) * falloff;
    } else {
      const RenderVec3 direction = NormalizeM2LightingVector(
          {position_range[0], position_range[1], position_range[2]});
      strength = std::max(DotM2Lighting(normal, direction), 0.0f);
    }
    lighting[0] += color[0] * strength;
    lighting[1] += color[1] * strength;
    lighting[2] += color[2] * strength;
  }

  for (float &channel : lighting) {
    channel = std::clamp(channel, 0.0f, 1.0f);
  }
  return lighting;
}

RenderVec4 EvaluateM2MaterialVertexColor(const M2BatchUniforms &uniforms,
                                         const RenderVec4 &vertex_color,
                                         const RenderVec3 &world_normal,
                                         const RenderVec3 &world_position) noexcept {
  RenderVec4 result{
      vertex_color[0] * uniforms.material_color[0],
      vertex_color[1] * uniforms.material_color[1],
      vertex_color[2] * uniforms.material_color[2],
      vertex_color[3] * uniforms.material_color[3],
  };
  if (uniforms.material_flags[0] < 0.5f) {
    const RenderVec3 lighting = EvaluateM2SurfaceLighting(
        uniforms, world_normal, world_position);
    result[0] *= lighting[0];
    result[1] *= lighting[1];
    result[2] *= lighting[2];
  }
  result[0] = std::clamp(result[0] + uniforms.emissive_color[0], 0.0f, 1.0f);
  result[1] = std::clamp(result[1] + uniforms.emissive_color[1], 0.0f, 1.0f);
  result[2] = std::clamp(result[2] + uniforms.emissive_color[2], 0.0f, 1.0f);
  result[3] = std::clamp(result[3], 0.0f, 1.0f);
  return result;
}

}

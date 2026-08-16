#pragma once

#include "openwow/render/m2/m2_public_types.h"
#include "openwow/render/api/math/render_math_types.h"

#include <array>
#include <bgfx/bgfx.h>
#include <cstdint>
#include <optional>

namespace openwow::render::m2 {

enum class M2CombinerMode : uint8_t {
  Opaque = 0,
  Mod = 1,
  Decal = 2,
  Add = 3,
  Mod2x = 4,
  Fade = 5,
  Mod2xNA = 6,
  AddNA = 7,
  Count = 8
};

inline constexpr std::array<std::uint32_t, 8u> kM2CombinerColorOpToFixedFunc = {0u, 0u, 4u, 2u,
                                                                                1u, 5u, 1u, 2u};

inline constexpr std::array<std::uint32_t, 8u> kM2CombinerAlphaOpToFixedFunc = {3u, 0u, 3u, 2u,
                                                                                1u, 3u, 3u, 3u};

enum class M2BlendMode : uint8_t {
  Opaque = 0,
  AlphaKey = 1,
  Alpha = 2,
  NoAlphaAdd = 3,
  Add = 4,
  Mod = 5,
  Mod2x = 6,
  Count = 7
};

inline constexpr std::uint16_t kM2MaterialFlagUnlit = 0x01u;
inline constexpr std::uint16_t kM2MaterialFlagUnfogged = 0x02u;
inline constexpr std::uint16_t kM2MaterialFlagTwoSided = 0x04u;
inline constexpr std::uint16_t kM2MaterialFlagDisableDepthTest = 0x08u;
inline constexpr std::uint16_t kM2MaterialFlagDisableDepthWrite = 0x10u;

enum class M2TexGen : uint8_t {
  TexCoord0 = 0,
  TexCoord1 = 1,
  EnvSphere = 2,
};

inline constexpr std::array<std::uint16_t, 6> kM2BonePaletteCapacities = {
    8u, 16u, 32u, 64u, 128u, 256u};

[[nodiscard]] constexpr std::uint16_t ResolveM2BonePaletteCapacity(
    const std::uint32_t bone_count) noexcept {
  for (const auto capacity : kM2BonePaletteCapacities) {
    if (bone_count <= capacity) {
      return capacity;
    }
  }
  return 0u;
}

enum class M2VertexVariant : std::uint8_t {
  LitEnv = 0,
  LitNoEnv,
  UnlitEnv,
  UnlitNoEnv,
  Count
};
inline constexpr std::size_t kM2VertexVariantCount =
    static_cast<std::size_t>(M2VertexVariant::Count);

enum class M2FragmentVariant : std::uint8_t {
  Generic = 0,
  Tex1Op1,
  Tex1Op0,
  Special1,
  Tex2Op1Op4,
  Tex2Op0Op6,
  Tex2Op1Op1,
  Tex2Op1Op0,
  Special2,
  Special3,
  Count
};
inline constexpr std::size_t kM2FragmentVariantCount =
    static_cast<std::size_t>(M2FragmentVariant::Count);

struct M2FragmentVariantKey {
  int texture_count{0};
  int op1{0};
  int op2{0};
  int special{0};
  const char* shader_name{nullptr};
};

inline constexpr std::array<M2FragmentVariantKey, kM2FragmentVariantCount>
    kM2FragmentVariantKeys = {{
        {0, 0, 0, 0, "fs_m2"},
        {1, 1, 0, 0, "fs_m2_t1_op1"},
        {1, 0, 0, 0, "fs_m2_t1_op0"},
        {2, 0, 6, 1, "fs_m2_special1"},
        {2, 1, 4, 0, "fs_m2_t2_op1_op4"},
        {2, 0, 6, 0, "fs_m2_t2_op0_op6"},
        {2, 1, 1, 0, "fs_m2_t2_op1_op1"},
        {2, 1, 0, 0, "fs_m2_t2_op1_op0"},
        {2, 0, 3, 2, "fs_m2_special2"},
        {2, 0, 3, 3, "fs_m2_special3"},
    }};

inline constexpr std::array<const char*, kM2VertexVariantCount>
    kM2VertexVariantSuffixes = {"_lit_env", "_lit_noenv", "_unlit_env",
                                "_unlit_noenv"};

[[nodiscard]] constexpr bool M2VertexVariantReadsLightingUniforms(
    const M2VertexVariant variant) noexcept {
  return variant == M2VertexVariant::LitEnv ||
         variant == M2VertexVariant::LitNoEnv;
}

using M2PermutationTable =
    std::array<std::array<bgfx::ProgramHandle, kM2FragmentVariantCount>,
               kM2VertexVariantCount>;

[[nodiscard]] constexpr M2PermutationTable MakeEmptyM2PermutationTable() noexcept {
  M2PermutationTable table{};
  for (auto& row : table) {
    for (auto& handle : row) {
      handle = BGFX_INVALID_HANDLE;
    }
  }
  return table;
}

struct M2BonePaletteShader {
  std::uint16_t capacity{0};

  bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle bone_columns = BGFX_INVALID_HANDLE;

  M2PermutationTable permutations = MakeEmptyM2PermutationTable();
};

inline constexpr std::uint32_t kM2StaticInstancingMaxBones = 8u;

inline constexpr std::uint16_t kM2InstanceDataStride = 20u * sizeof(float);
static_assert(sizeof(M2InstancedDrawRecord) == kM2InstanceDataStride);

enum M2VertexParamSlot : std::uint16_t {
  kM2VertexParamUvTransform0Row0 = 0,
  kM2VertexParamUvTransform0Row1 = 1,
  kM2VertexParamUvTransform1Row0 = 2,
  kM2VertexParamUvTransform1Row1 = 3,
  kM2VertexParamTexGenFlags = 4,
  kM2VertexParamMaterialColor = 5,
  kM2VertexParamMaterialFlags = 6,
  kM2VertexParamEmissiveColor = 7,
  kM2VertexParamLightCount = 8,
  kM2VertexParamLightAmbient = 9,
};
inline constexpr std::uint16_t kM2VertexParamCount = 10u;

inline constexpr std::uint16_t kM2LightSlotStride = 3u;
enum M2LightSlotOffset : std::uint16_t {
  kM2LightOffsetPosRange = 0,
  kM2LightOffsetAttenuation = 1,
  kM2LightOffsetColor = 2,
};
inline constexpr std::uint16_t kM2LightUniformCount =
    kM2LightSlotStride *
    static_cast<std::uint16_t>(M2BatchUniforms::kMaxM2Lights);

enum M2FragmentParamSlot : std::uint16_t {
  kM2FragmentParamCombinerMode = 0,
  kM2FragmentParamAlphaRef = 1,
  kM2FragmentParamMaterialFlags = 2,
  kM2FragmentParamFogParams = 3,
  kM2FragmentParamFogColor = 4,
};
inline constexpr std::uint16_t kM2FragmentParamCount = 5u;

struct M2ShaderHandles {
  std::array<M2BonePaletteShader, kM2BonePaletteCapacities.size()>
      bone_palette_shaders{};

  M2BonePaletteShader instanced_shader{};

  bgfx::UniformHandle s_tex0 = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle s_tex1 = BGFX_INVALID_HANDLE;

  bgfx::UniformHandle u_world_matrix = BGFX_INVALID_HANDLE;

  bgfx::UniformHandle u_vertex_params = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_lights = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_fragment_params = BGFX_INVALID_HANDLE;
};

[[nodiscard]] const M2BonePaletteShader* FindM2BonePaletteShader(
    const M2ShaderHandles& handles, std::uint32_t bone_count) noexcept;

[[nodiscard]] M2VertexVariant ResolveM2VertexVariant(
    const M2BatchUniforms& uniforms) noexcept;

[[nodiscard]] M2FragmentVariant ResolveM2FragmentVariant(
    const M2BatchUniforms& uniforms) noexcept;

struct M2ProgramSelection {
  bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;

  bool reads_lighting_uniforms{true};
};

[[nodiscard]] M2ProgramSelection SelectM2Program(
    const M2BonePaletteShader& palette_shader,
    const M2BatchUniforms& uniforms) noexcept;

M2ShaderHandles LoadM2Program();

void DestroyM2Program(M2ShaderHandles &handles);

std::optional<uint64_t> ResolveM2BlendState(M2BlendMode mode) noexcept;

uint64_t ApplyMaterialRenderFlags(uint64_t state, uint16_t flags);

std::optional<uint64_t> ResolveM2RenderState(M2BlendMode mode, uint16_t flags) noexcept;

constexpr float kM2OpaqueListOpacityThreshold = 0.99999f;
bool BatchBelongsToOpaqueList(std::uint32_t blend_mode, float opacity);
bool BatchBelongsToOpaqueList(M2BlendMode mode, float opacity);

inline constexpr std::array<std::uint32_t, 14u> kM2BlendModeToPassGroup = {
    2u, 2u, 2u, 10u, 3u, 4u, 5u, 2u, 2u, 2u, 10u, 3u, 4u, 5u};
inline constexpr std::size_t kM2BlendModeToPassGroupCount = kM2BlendModeToPassGroup.size();
inline constexpr std::uint32_t kM2TransparentPassGroupA = 3u;
inline constexpr std::uint32_t kM2TransparentPassGroupB = 10u;

std::optional<float> ResolveM2AlphaRefThreshold(M2BlendMode mode, float batch_alpha) noexcept;

bool BlendModeUsesMaterialLighting(M2BlendMode mode);

enum class M2MaterialFogMode : uint8_t {
  Disabled = 0,
  SceneColor = 1,
  Black = 2,
  White = 3,
  Gray = 4,
};

M2MaterialFogMode ResolveM2MaterialFogMode(M2BlendMode mode, bool render_flag_unfogged,
                                           float effective_alpha) noexcept;

RenderVec4 ResolveM2MaterialFogColor(M2MaterialFogMode mode,
                                     const RenderVec4 &scene_fog_color) noexcept;

struct M2MaterialColorSlots {
  RenderVec4 diffuse{1.0f, 1.0f, 1.0f, 1.0f};
  RenderVec4 emissive{0.0f, 0.0f, 0.0f, 0.0f};
  bool uses_lighting{true};
};

M2MaterialColorSlots BuildMaterialColorSlots(M2BlendMode mode, bool render_flag_unlit,
                                             const RenderVec4 &color);

[[nodiscard]] RenderVec3 EvaluateM2SurfaceLighting(
    const M2BatchUniforms &uniforms, const RenderVec3 &world_normal,
    const RenderVec3 &world_position) noexcept;

[[nodiscard]] RenderVec4 EvaluateM2MaterialVertexColor(
    const M2BatchUniforms &uniforms, const RenderVec4 &vertex_color,
    const RenderVec3 &world_normal, const RenderVec3 &world_position) noexcept;

}

#pragma once

#include <bgfx/bgfx.h>
#include <bgfx/embedded_shader.h>

#include <cstdint>
#include <span>

namespace openwow::render {

enum class ShaderProgramId : std::uint8_t {
  Ui,
  UiMaterial,
  WorldText,
  M2,
  M2Bones8,
  M2Bones16,
  M2Bones32,
  M2Bones64,
  M2Bones128,
  Particle,
  Sky,
  DistantTerrain,
  Terrain,
  TerrainSplat,

  TerrainSplatArray,
  LiquidWater,
  LiquidWater1,
  LiquidWater2,
  LiquidWater3,
  LiquidWaterNoSpec,
  LiquidWaterNoSpec1,
  LiquidWaterNoSpec2,
  LiquidWaterNoSpec3,
  LiquidMagma,
  LiquidProceduralWater,
  Weather,
  Minimap,
  Wmo,
  WmoSpecular,
  WmoEnv,
  WmoTwoLayer,
  Ribbon,
  Nameplate,
  Debug,

  WmoPortalFill,
  Celestial,
  PostProcessDeath,
  PostProcessBlit,
  PostProcessBox4,
  PostProcessBlurH,
  PostProcessBlurV,
  PostProcessComposite,

  M2Instanced,

  Decal,
};

enum class ShaderProgramCreateError : std::uint8_t {
  None,
  UnknownProgram,
  VertexShaderUnavailable,
  FragmentShaderUnavailable,
  ProgramLinkFailed,
};

struct ShaderProgramDescriptor {
  ShaderProgramId id;
  const char* name;
  const bgfx::EmbeddedShader* table;
  const char* vertex_shader;
  const char* fragment_shader;
};

struct ShaderProgramCreateResult {
  bgfx::ProgramHandle handle = BGFX_INVALID_HANDLE;
  ShaderProgramCreateError error = ShaderProgramCreateError::None;

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == ShaderProgramCreateError::None && bgfx::isValid(handle);
  }
};

[[nodiscard]] std::span<const ShaderProgramDescriptor> DeclaredShaderPrograms() noexcept;
[[nodiscard]] const ShaderProgramDescriptor* FindShaderProgram(ShaderProgramId id) noexcept;

[[nodiscard]] const char* ShaderProgramCreateErrorName(
    ShaderProgramCreateError error) noexcept;

[[nodiscard]] ShaderProgramCreateResult TryCreateEmbeddedProgram(
    ShaderProgramId id,
    bgfx::RendererType::Enum renderer_type,
    bool destroy_shaders = true);

[[nodiscard]] bgfx::ProgramHandle CreateEmbeddedProgram(
    ShaderProgramId id,
    bgfx::RendererType::Enum renderer_type,
    bool destroy_shaders = true);

}

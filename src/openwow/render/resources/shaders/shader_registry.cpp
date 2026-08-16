#include "openwow/render/resources/shaders/shader_registry.h"

#include "openwow/render/backend/bgfx/embedded_shaders.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <array>
#include <string>

namespace openwow::render {
namespace {

constexpr std::array<ShaderProgramDescriptor, 44> kShaderPrograms{{
    {ShaderProgramId::Ui, "ui", s_uiShaders, "vs_ui", "fs_ui"},
    {ShaderProgramId::UiMaterial, "ui_material", s_uiMaterialShaders, "vs_ui",
     "fs_ui_material"},
    {ShaderProgramId::WorldText, "world_text", s_worldTextShaders, "vs_ui",
     "fs_world_text"},
    {ShaderProgramId::M2, "m2_bones_256", s_m2Shaders, "vs_m2_bones_256", "fs_m2"},
    {ShaderProgramId::M2Bones8, "m2_bones_8", s_m2Shaders, "vs_m2_bones_8", "fs_m2"},
    {ShaderProgramId::M2Bones16, "m2_bones_16", s_m2Shaders, "vs_m2_bones_16", "fs_m2"},
    {ShaderProgramId::M2Bones32, "m2_bones_32", s_m2Shaders, "vs_m2_bones_32", "fs_m2"},
    {ShaderProgramId::M2Bones64, "m2_bones_64", s_m2Shaders, "vs_m2_bones_64", "fs_m2"},
    {ShaderProgramId::M2Bones128, "m2_bones_128", s_m2Shaders, "vs_m2_bones_128", "fs_m2"},
    {ShaderProgramId::Particle, "particle", s_particleShaders, "vs_particle", "fs_particle"},
    {ShaderProgramId::Sky, "sky", s_skyShaders, "vs_sky", "fs_sky"},
    {ShaderProgramId::DistantTerrain, "distant_terrain", s_distantTerrainShaders,
     "vs_distant_terrain", "fs_distant_terrain"},
    {ShaderProgramId::Terrain, "terrain", s_terrainShaders, "vs_terrain", "fs_terrain"},
    {ShaderProgramId::TerrainSplat, "terrain_splat", s_terrainSplatShaders,
     "vs_terrain_splat", "fs_terrain_splat"},
    {ShaderProgramId::TerrainSplatArray, "terrain_splat_array",
     s_terrainSplatArrayShaders, "vs_terrain_splat_array", "fs_terrain_splat_array"},
    {ShaderProgramId::LiquidWater, "liquid_water_0", s_liquidWaterShaders,
     "vs_liquid_water_0", "fs_water"},
    {ShaderProgramId::LiquidWater1, "liquid_water_1", s_liquidWaterShaders,
     "vs_liquid_water_1", "fs_water"},
    {ShaderProgramId::LiquidWater2, "liquid_water_2", s_liquidWaterShaders,
     "vs_liquid_water_2", "fs_water"},
    {ShaderProgramId::LiquidWater3, "liquid_water_3", s_liquidWaterShaders,
     "vs_liquid_water_3", "fs_water"},
    {ShaderProgramId::LiquidWaterNoSpec, "liquid_water_no_spec_0",
     s_liquidWaterNoSpecShaders, "vs_liquid_water_no_spec_0",
     "fs_liquid_water_no_spec"},
    {ShaderProgramId::LiquidWaterNoSpec1, "liquid_water_no_spec_1",
     s_liquidWaterNoSpecShaders, "vs_liquid_water_no_spec_1",
     "fs_liquid_water_no_spec"},
    {ShaderProgramId::LiquidWaterNoSpec2, "liquid_water_no_spec_2",
     s_liquidWaterNoSpecShaders, "vs_liquid_water_no_spec_2",
     "fs_liquid_water_no_spec"},
    {ShaderProgramId::LiquidWaterNoSpec3, "liquid_water_no_spec_3",
     s_liquidWaterNoSpecShaders, "vs_liquid_water_no_spec_3",
     "fs_liquid_water_no_spec"},
    {ShaderProgramId::LiquidMagma, "liquid_magma", s_liquidMagmaShaders,
     "vs_liquid_magma", "fs_liquid_magma"},
    {ShaderProgramId::LiquidProceduralWater, "liquid_procedural_water",
     s_liquidProceduralWaterShaders, "vs_liquid_procedural_water",
     "fs_liquid_procedural_water"},
    {ShaderProgramId::Weather, "weather", s_weatherShaders, "vs_weather", "fs_weather"},
    {ShaderProgramId::Minimap, "minimap", s_minimapShaders, "vs_minimap", "fs_minimap"},
    {ShaderProgramId::Wmo, "wmo", s_wmoShaders, "vs_wmo", "fs_wmo"},
    {ShaderProgramId::WmoSpecular, "wmo_specular", s_wmoSpecularShaders, "vs_wmo",
     "fs_wmo_specular"},
    {ShaderProgramId::WmoEnv, "wmo_env", s_wmoEnvShaders, "vs_wmo", "fs_wmo_env"},
    {ShaderProgramId::WmoTwoLayer, "wmo_two_layer", s_wmoTwoLayerShaders, "vs_wmo",
     "fs_wmo_two_layer"},
    {ShaderProgramId::Ribbon, "ribbon", s_ribbonShaders, "vs_ribbon", "fs_ribbon"},
    {ShaderProgramId::Nameplate, "nameplate", s_nameplateShaders, "vs_nameplate",
     "fs_nameplate"},
    {ShaderProgramId::Debug, "debug", s_debugShaders, "vs_debug", "fs_debug"},
    {ShaderProgramId::WmoPortalFill, "wmo_portal_fill", s_portalFillShaders,
     "vs_portal_fill", "fs_debug"},
    {ShaderProgramId::Celestial, "celestial", s_celestialShaders, "vs_celestial",
     "fs_celestial"},
    {ShaderProgramId::PostProcessDeath, "postprocess_death", s_ppDeathShaders,
     "vs_postprocess", "fs_postprocess_death"},
    {ShaderProgramId::PostProcessBlit, "postprocess_blit", s_ppBlitShaders,
     "vs_postprocess", "fs_postprocess_blit"},
    {ShaderProgramId::PostProcessBox4, "postprocess_box4", s_ppBox4Shaders,
     "vs_postprocess", "fs_postprocess_box4"},
    {ShaderProgramId::PostProcessBlurH, "postprocess_blur_h", s_ppBlurHShaders,
     "vs_postprocess", "fs_postprocess_blur_h"},
    {ShaderProgramId::PostProcessBlurV, "postprocess_blur_v", s_ppBlurVShaders,
     "vs_postprocess", "fs_postprocess_blur_v"},
    {ShaderProgramId::PostProcessComposite, "postprocess_composite", s_ppCompositeShaders,
     "vs_postprocess", "fs_postprocess_composite"},
    {ShaderProgramId::M2Instanced, "m2_instanced", s_m2Shaders, "vs_m2_instanced",
     "fs_m2"},
    {ShaderProgramId::Decal, "decal", s_decalShaders, "vs_decal", "fs_decal"},
}};

static_assert(kShaderPrograms.size() ==
              static_cast<std::size_t>(ShaderProgramId::Decal) + 1u,
              "kShaderPrograms must have one row per ShaderProgramId; Decal is "
              "the last enumerator");
static_assert([] {
  for (std::size_t i = 0; i < kShaderPrograms.size(); ++i) {
    if (kShaderPrograms[i].id != static_cast<ShaderProgramId>(i)) return false;
  }
  return true;
}(), "kShaderPrograms must be ordered by ShaderProgramId: row i must be "
     "enumerator i, because FindShaderProgram indexes the table by the id");

void LogCreateFailure(const ShaderProgramDescriptor* descriptor,
                      const ShaderProgramCreateError error) {
  const std::string program_name = descriptor != nullptr ? descriptor->name : "<unknown>";
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                     "ShaderRegistry: " + program_name + ": " +
                         ShaderProgramCreateErrorName(error));
}

}

std::span<const ShaderProgramDescriptor> DeclaredShaderPrograms() noexcept {
  return kShaderPrograms;
}

const ShaderProgramDescriptor* FindShaderProgram(const ShaderProgramId id) noexcept {
  const auto index = static_cast<std::size_t>(id);
  if (index >= kShaderPrograms.size() || kShaderPrograms[index].id != id) {
    return nullptr;
  }
  return &kShaderPrograms[index];
}

const char* ShaderProgramCreateErrorName(const ShaderProgramCreateError error) noexcept {
  switch (error) {
    case ShaderProgramCreateError::None:
      return "no error";
    case ShaderProgramCreateError::UnknownProgram:
      return "unknown shader program";
    case ShaderProgramCreateError::VertexShaderUnavailable:
      return "embedded vertex shader unavailable for renderer";
    case ShaderProgramCreateError::FragmentShaderUnavailable:
      return "embedded fragment shader unavailable for renderer";
    case ShaderProgramCreateError::ProgramLinkFailed:
      return "shader program link failed";
  }
  return "unknown shader creation error";
}

ShaderProgramCreateResult TryCreateEmbeddedProgram(
    const ShaderProgramId id, const bgfx::RendererType::Enum renderer_type,
    const bool destroy_shaders) {
  const auto* descriptor = FindShaderProgram(id);
  if (descriptor == nullptr) {
    const auto error = ShaderProgramCreateError::UnknownProgram;
    LogCreateFailure(nullptr, error);
    return {.error = error};
  }

  const bgfx::ShaderHandle vertex =
      bgfx::createEmbeddedShader(descriptor->table, renderer_type, descriptor->vertex_shader);
  const bgfx::ShaderHandle fragment =
      bgfx::createEmbeddedShader(descriptor->table, renderer_type, descriptor->fragment_shader);

  if (!bgfx::isValid(vertex) || !bgfx::isValid(fragment)) {
    if (bgfx::isValid(vertex)) {
      bgfx::destroy(vertex);
    }
    if (bgfx::isValid(fragment)) {
      bgfx::destroy(fragment);
    }
    const auto error = !bgfx::isValid(vertex)
                           ? ShaderProgramCreateError::VertexShaderUnavailable
                           : ShaderProgramCreateError::FragmentShaderUnavailable;
    LogCreateFailure(descriptor, error);
    return {.error = error};
  }

  const bgfx::ProgramHandle program = bgfx::createProgram(vertex, fragment, destroy_shaders);
  if (!bgfx::isValid(program)) {
    if (!destroy_shaders) {
      bgfx::destroy(vertex);
      bgfx::destroy(fragment);
    }
    const auto error = ShaderProgramCreateError::ProgramLinkFailed;
    LogCreateFailure(descriptor, error);
    return {.error = error};
  }
  return {.handle = program};
}

bgfx::ProgramHandle CreateEmbeddedProgram(const ShaderProgramId id,
                                          const bgfx::RendererType::Enum renderer_type,
                                          const bool destroy_shaders) {
  return TryCreateEmbeddedProgram(id, renderer_type, destroy_shaders).handle;
}

}

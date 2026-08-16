#include "openwow/render/world/water/water_renderer.h"

#include "openwow/data/formats/dbc/dbc_entries_world.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/render/api/draw_encoder.h"
#include "openwow/render/backend/bgfx/bgfx_texture_lease.h"
#include "openwow/render/backend/bgfx/retail_render_profile.h"
#include "openwow/render/resources/shaders/shader_registry.h"
#include "openwow/render/resources/textures/texture_manager.h"
#include "openwow/render/world/water/liquid_shader_constants.h"
#include "openwow/world/presentation/world_presentation_commands.h"
#include "openwow/world/world_render_pipeline.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <numeric>
#include <string>
#include <tuple>
#include <utility>

namespace openwow::render {

namespace liquid_vs_param {

inline constexpr std::size_t kAmbient = 0u;
inline constexpr std::size_t kDiffuse = 1u;
inline constexpr std::size_t kSpecular = 2u;
inline constexpr std::size_t kLightDir = 3u;
inline constexpr std::size_t kCamera = 4u;

inline constexpr std::size_t kFrameConstantCount = 5u;

inline constexpr std::size_t kUvTransform0 = 5u;
inline constexpr std::size_t kUvTransform1 = 6u;
inline constexpr std::size_t kPointLightPosition = 7u;
inline constexpr std::size_t kPointLightDiffuse = 10u;
inline constexpr std::size_t kPointLightAttenuation = 13u;
inline constexpr std::size_t kCount = 16u;

}

namespace liquid_fs_param {

inline constexpr std::size_t kFogParams = 0u;
inline constexpr std::size_t kFogColor = 1u;
inline constexpr std::size_t kFrameConstantCount = 2u;
inline constexpr std::size_t kCount = 2u;

}

namespace liquid_proc_vs_param {

inline constexpr std::size_t kWavePhases = 0u;
inline constexpr std::size_t kWaveInverseFalloff = 1u;
inline constexpr std::size_t kWaveRecords = 2u;
inline constexpr std::size_t kWaveInverseSpatialPeriod = 5u;
inline constexpr std::size_t kWaveAmplitudes = 6u;
inline constexpr std::size_t kFrameConstantCount = 7u;

inline constexpr std::size_t kUvTransforms = 7u;
inline constexpr std::size_t kCount = 13u;

}

namespace liquid_proc_fs_param {

inline constexpr std::size_t kFogParams = 0u;
inline constexpr std::size_t kFogColor = 1u;
inline constexpr std::size_t kSpecular = 2u;
inline constexpr std::size_t kLightRay = 3u;
inline constexpr std::size_t kCamera = 4u;
inline constexpr std::size_t kFrameConstantCount = 5u;

inline constexpr std::size_t kMaterialParams = 5u;
inline constexpr std::size_t kMaterialParamCount = 2u;
inline constexpr std::size_t kCount = 7u;

}

static_assert(liquid_vs_param::kPointLightPosition +
                  WaterRenderer::kMaxLiquidDrawPointLights ==
              liquid_vs_param::kPointLightDiffuse);
static_assert(liquid_vs_param::kPointLightDiffuse +
                  WaterRenderer::kMaxLiquidDrawPointLights ==
              liquid_vs_param::kPointLightAttenuation);
static_assert(liquid_vs_param::kPointLightAttenuation +
                  WaterRenderer::kMaxLiquidDrawPointLights ==
              liquid_vs_param::kCount);
static_assert(liquid_vs_param::kCount == WaterRenderer::kLiquidVsParamCount);
static_assert(liquid_fs_param::kFrameConstantCount == liquid_fs_param::kCount);
static_assert(liquid_fs_param::kCount == WaterRenderer::kLiquidFsParamCount);
static_assert(liquid_proc_vs_param::kWaveRecords +
                  std::tuple_size_v<decltype(ProceduralLiquidVertexConstants::waves)> ==
              liquid_proc_vs_param::kWaveInverseSpatialPeriod);
static_assert(liquid_proc_vs_param::kUvTransforms +
                  std::tuple_size_v<decltype(ProceduralLiquidTextureConstants::transforms)> ==
              liquid_proc_vs_param::kCount);
static_assert(liquid_proc_vs_param::kCount ==
              WaterRenderer::kLiquidProcVsParamCount);
static_assert(liquid_proc_fs_param::kMaterialParams +
                  liquid_proc_fs_param::kMaterialParamCount ==
              liquid_proc_fs_param::kCount);
static_assert(liquid_proc_fs_param::kMaterialParamCount ==
              sizeof(LiquidPixelShaderMaterialConstants) / sizeof(RenderVec4));
static_assert(liquid_proc_fs_param::kCount ==
              WaterRenderer::kLiquidProcFsParamCount);

namespace {

constexpr std::uint32_t kLiquidAnimationFrameCount = 30u;
constexpr std::uint32_t kDefaultTexturePeriodMs = 1250u;
constexpr float kLiquidWorldTextureScale = 1.0f / 16.666666f;
constexpr std::array<ShaderProgramId, 4> kLiquidWaterProgramIds{
    ShaderProgramId::LiquidWater, ShaderProgramId::LiquidWater1,
    ShaderProgramId::LiquidWater2, ShaderProgramId::LiquidWater3};
constexpr std::array<ShaderProgramId, 4> kLiquidWaterNoSpecProgramIds{
    ShaderProgramId::LiquidWaterNoSpec, ShaderProgramId::LiquidWaterNoSpec1,
    ShaderProgramId::LiquidWaterNoSpec2,
    ShaderProgramId::LiquidWaterNoSpec3};

std::size_t VertexIndex(const world::WaterHeightfield& surface,
                        const std::uint32_t row,
                        const std::uint32_t column) noexcept {
  return world::WaterHeightfieldVertexIndex(surface, row, column);
}

bool IsCellRendered(const world::WaterHeightfield& surface,
                    const std::uint32_t row,
                    const std::uint32_t column) noexcept {
  return surface.vertex_rows >= 2u && surface.vertex_cols >= 2u &&
         world::IsWaterHeightfieldCellRendered(surface, row, column);
}

void NormalizeHeightfield(world::WaterHeightfield& heightfield) {
  const std::size_t vertex_count =
      static_cast<std::size_t>(heightfield.vertex_rows) *
      heightfield.vertex_cols;
  if (vertex_count == 0u) {
    return;
  }
  if (heightfield.heights.size() != vertex_count) {
    heightfield.heights.resize(vertex_count, 0.0f);
  }
  if (!heightfield.positions.empty() &&
      heightfield.positions.size() != vertex_count) {
    heightfield.positions.clear();
  }
  if (!heightfield.vertex_depths.empty() &&
      heightfield.vertex_depths.size() != vertex_count) {
    heightfield.vertex_depths.clear();
  }
  if (!heightfield.vertex_texcoords.empty() &&
      heightfield.vertex_texcoords.size() != vertex_count) {
    heightfield.vertex_texcoords.clear();
  }

  const std::size_t cell_count =
      heightfield.vertex_rows > 0u && heightfield.vertex_cols > 0u
          ? static_cast<std::size_t>(heightfield.vertex_rows - 1u) *
                (heightfield.vertex_cols - 1u)
          : 0u;
  if (cell_count != 0u && heightfield.render_mask.empty()) {
    heightfield.render_mask.assign(cell_count, std::uint8_t{1});
  }
}

RenderVec3 SurfacePosition(const world::WaterHeightfield& surface,
                           const std::uint32_t row,
                           const std::uint32_t column) {
  const std::size_t index = VertexIndex(surface, row, column);
  if (surface.positions.size() == surface.heights.size()) {
    return surface.positions[index];
  }
  return {
      surface.origin_x + static_cast<float>(row) * surface.step_x,
      surface.origin_z + static_cast<float>(column) * surface.step_z,
      surface.heights[index],
  };
}

RenderVec3 Subtract(const RenderVec3& lhs, const RenderVec3& rhs) noexcept {
  return {lhs[0] - rhs[0], lhs[1] - rhs[1], lhs[2] - rhs[2]};
}

RenderVec3 Cross(const RenderVec3& lhs, const RenderVec3& rhs) noexcept {
  return {lhs[1] * rhs[2] - lhs[2] * rhs[1],
          lhs[2] * rhs[0] - lhs[0] * rhs[2],
          lhs[0] * rhs[1] - lhs[1] * rhs[0]};
}

RenderVec3 Normalize(RenderVec3 value) noexcept {
  const float length_squared = value[0] * value[0] + value[1] * value[1] +
                               value[2] * value[2];
  if (length_squared <= std::numeric_limits<float>::epsilon()) {
    return {0.0f, 0.0f, 1.0f};
  }
  const float inverse_length = 1.0f / std::sqrt(length_squared);
  value[0] *= inverse_length;
  value[1] *= inverse_length;
  value[2] *= inverse_length;
  if (value[2] < 0.0f) {
    value[0] = -value[0];
    value[1] = -value[1];
    value[2] = -value[2];
  }
  return value;
}

RenderVec3 SurfaceNormal(const world::WaterHeightfield& surface,
                         const std::uint32_t row,
                         const std::uint32_t column) {
  const std::uint32_t previous_row = row == 0u ? row : row - 1u;
  const std::uint32_t next_row =
      std::min(row + 1u, surface.vertex_rows - 1u);
  const std::uint32_t previous_column = column == 0u ? column : column - 1u;
  const std::uint32_t next_column =
      std::min(column + 1u, surface.vertex_cols - 1u);
  const RenderVec3 row_tangent =
      Subtract(SurfacePosition(surface, next_row, column),
               SurfacePosition(surface, previous_row, column));
  const RenderVec3 column_tangent =
      Subtract(SurfacePosition(surface, row, next_column),
               SurfacePosition(surface, row, previous_column));
  return Normalize(Cross(row_tangent, column_tangent));
}

std::string NormalizeTextureIdentity(const std::string_view value) {
  std::string result(value);
  std::transform(result.begin(), result.end(), result.begin(), [](char c) {
    if (c == '\\') {
      return '/';
    }
    if (c >= 'A' && c <= 'Z') {
      return static_cast<char>(c - 'A' + 'a');
    }
    return c;
  });
  return result;
}

std::string FormatTextureFrame(const std::string_view pattern,
                               const std::uint32_t frame) {
  std::string result(pattern);
  const std::size_t marker = result.find("%d");
  if (marker != std::string::npos) {
    result.replace(marker, 2u, std::to_string(frame));
  }
  return result;
}

std::uint8_t UnitFloatToByte(const float value) noexcept {
  return static_cast<std::uint8_t>(
      std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

std::uint32_t RetailAnimationTick(const double milliseconds) noexcept {
  if (!(milliseconds > 0.0) || !std::isfinite(milliseconds)) {
    return 0u;
  }
  constexpr double kUnsignedTickPeriod = 4294967296.0;
  return static_cast<std::uint32_t>(
      std::fmod(std::floor(milliseconds), kUnsignedTickPeriod));
}

float RetailUnsignedToFloat(const std::uint32_t value) noexcept {
  return static_cast<float>(value >> 16u) * 65536.0f +
         static_cast<float>(value & 0xffffu);
}

float RetailLiquidTextureTranslation(const std::uint32_t tick_count_ms,
                                     const float rate) noexcept {
  if (rate == 0.0f) {
    return 0.0f;
  }
  const float period_float = 1000.0f / rate;
  if (!(period_float >= 1.0f)) {

    return 0.0f;
  }
  constexpr float kTwoToThe32 = 4294967296.0f;
  const std::uint32_t period =
      period_float >= kTwoToThe32
          ? std::numeric_limits<std::uint32_t>::max()
          : static_cast<std::uint32_t>(period_float);
  return RetailUnsignedToFloat(tick_count_ms % period) /
         RetailUnsignedToFloat(period);
}

std::size_t RetailLiquidTextureFrame(const std::uint32_t tick_count_ms,
                                     const std::uint32_t period_ms,
                                     const std::size_t frame_count) noexcept {
  if (frame_count <= 1u) {
    return 0u;
  }
  const std::uint32_t period = period_ms == 0u ? 1u : period_ms;
  const float phase =
      RetailUnsignedToFloat(tick_count_ms % period) /
      RetailUnsignedToFloat(period);

  const long frame = std::lrint(
      RetailUnsignedToFloat(static_cast<std::uint32_t>(frame_count)) * phase -
      0.5f);
  return static_cast<std::size_t>(frame);
}

std::array<std::uint8_t, 3> UnpackArgbRgb(
    const std::uint32_t argb) noexcept {
  return {static_cast<std::uint8_t>((argb >> 16u) & 0xffu),
          static_cast<std::uint8_t>((argb >> 8u) & 0xffu),
          static_cast<std::uint8_t>(argb & 0xffu)};
}

std::uint8_t InterpolateByteFixed(const std::uint8_t from,
                                  const std::uint8_t to,
                                  const std::uint32_t row) noexcept {
  const int fixed = (static_cast<int>(from) << 8) +
                    (static_cast<int>(to) - static_cast<int>(from)) *
                        static_cast<int>(row) * 4;
  return static_cast<std::uint8_t>(std::clamp(fixed >> 8, 0, 255));
}

using LiquidLookupPixels = std::array<std::uint8_t, 8u * 64u * 4u>;

LiquidLookupPixels BuildDepthLookup(const std::uint32_t shallow_argb,
                                    const std::uint32_t deep_argb,
                                    const float shallow_alpha,
                                    const float deep_alpha,
                                    const bool darken_last_row) {
  LiquidLookupPixels pixels{};
  const auto shallow = UnpackArgbRgb(shallow_argb);
  const auto deep = UnpackArgbRgb(deep_argb);
  const std::uint8_t shallow_a = UnitFloatToByte(shallow_alpha);
  const std::uint8_t deep_a = UnitFloatToByte(deep_alpha);
  for (std::uint32_t row = 0u; row < 64u; ++row) {
    std::array<std::uint8_t, 3> rgb{
        InterpolateByteFixed(shallow[0], deep[0], row),
        InterpolateByteFixed(shallow[1], deep[1], row),
        InterpolateByteFixed(shallow[2], deep[2], row),
    };
    if (darken_last_row && row == 63u) {
      for (std::uint8_t& component : rgb) {
        component = static_cast<std::uint8_t>(
            std::lround(static_cast<float>(component) * 0.9f));
      }
    }
    const std::uint8_t alpha =
        InterpolateByteFixed(shallow_a, deep_a, row);
    for (std::uint32_t column = 0u; column < 8u; ++column) {
      const std::size_t offset = (row * 8u + column) * 4u;
      pixels[offset + 0u] = rgb[0];
      pixels[offset + 1u] = rgb[1];
      pixels[offset + 2u] = rgb[2];
      pixels[offset + 3u] = alpha;
    }
  }
  return pixels;
}

LiquidLookupPixels BuildWmoLookup(const std::uint32_t water_argb,
                                  const float shallow_alpha,
                                  const float deep_alpha) {
  LiquidLookupPixels pixels{};
  const auto water = UnpackArgbRgb(water_argb);
  const std::uint8_t shallow_a = UnitFloatToByte(shallow_alpha);
  const std::uint8_t deep_a = UnitFloatToByte(deep_alpha);
  for (std::uint32_t row = 0u; row < 64u; ++row) {
    const std::uint8_t alpha =
        InterpolateByteFixed(shallow_a, deep_a, row);
    for (std::uint32_t column = 0u; column < 8u; ++column) {
      const std::size_t offset = (row * 8u + column) * 4u;
      const bool white_half = column >= 4u;
      pixels[offset + 0u] = white_half ? 255u : water[0];
      pixels[offset + 1u] = white_half ? 255u : water[1];
      pixels[offset + 2u] = white_half ? 255u : water[2];
      pixels[offset + 3u] = alpha;
    }
  }
  return pixels;
}

bgfx::TextureHandle CreateLiquidLookupTexture(
    const LiquidLookupPixels& pixels) {
  constexpr std::uint64_t flags = BGFX_SAMPLER_U_CLAMP |
                                  BGFX_SAMPLER_V_CLAMP |
                                  BGFX_SAMPLER_MIP_POINT;
  return bgfx::createTexture2D(
      8u, 64u, false, 1u, bgfx::TextureFormat::RGBA8, flags,
      bgfx::copy(pixels.data(), static_cast<std::uint32_t>(pixels.size())));
}

void UpdateLiquidLookupTexture(const bgfx::TextureHandle texture,
                               const LiquidLookupPixels& pixels) {
  bgfx::updateTexture2D(
      texture, 0u, 0u, 0u, 0u, 8u, 64u,
      bgfx::copy(pixels.data(), static_cast<std::uint32_t>(pixels.size())));
}

}

WaterRenderer::WaterRenderer(TextureManager& texture_manager)
    : texture_manager_(texture_manager) {}

WaterRenderer::~WaterRenderer() { Shutdown(); }

bool WaterRenderer::Initialize() {
  if (initialized_) {
    return true;
  }

  layout_.begin()
      .add(bgfx::Attrib::Position, 3u, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Normal, 3u, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Color0, 4u, bgfx::AttribType::Uint8, true)
      .add(bgfx::Attrib::TexCoord0, 2u, bgfx::AttribType::Float)
      .add(bgfx::Attrib::TexCoord1, 2u, bgfx::AttribType::Float)
      .end();
  water_ripple_layout_.begin()
      .add(bgfx::Attrib::Position, 3u, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Color0, 4u, bgfx::AttribType::Uint8, true)
      .add(bgfx::Attrib::TexCoord0, 2u, bgfx::AttribType::Float)
      .end();

  const auto renderer_type = bgfx::getRendererType();
  for (std::size_t index = 0u; index < water_programs_.size(); ++index) {
    water_programs_[index] =
        CreateEmbeddedProgram(kLiquidWaterProgramIds[index], renderer_type);
    water_no_spec_programs_[index] = CreateEmbeddedProgram(
        kLiquidWaterNoSpecProgramIds[index], renderer_type);
  }
  magma_program_ =
      CreateEmbeddedProgram(ShaderProgramId::LiquidMagma, renderer_type);
  procedural_water_program_ = CreateEmbeddedProgram(
      ShaderProgramId::LiquidProceduralWater, renderer_type);
  water_ripple_program_ =
      CreateEmbeddedProgram(ShaderProgramId::Particle, renderer_type);
  u_water_ripple_flags_ =
      bgfx::createUniform("u_particleFlags", bgfx::UniformType::Vec4);
  s_water_ripple_texture_ =
      bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
  water_ripple_textures_[0] = texture_manager_.AcquireTextureStrict(
      "XTextures\\splash\\wake.blp");
  water_ripple_textures_[1] = texture_manager_.AcquireTextureStrict(
      "XTextures\\splash\\splash.blp");

  for (std::uint8_t index = 0u; index < samplers_.size(); ++index) {
    const std::string name = "s_liquid" + std::to_string(index);
    samplers_[index] = bgfx::createUniform(name.c_str(),
                                           bgfx::UniformType::Sampler);
  }

  u_vs_params_ = bgfx::createUniform(
      "u_liquidVsParams", bgfx::UniformType::Vec4,
      static_cast<std::uint16_t>(liquid_vs_param::kCount));
  u_fs_params_ = bgfx::createUniform(
      "u_liquidFsParams", bgfx::UniformType::Vec4,
      static_cast<std::uint16_t>(liquid_fs_param::kCount));
  u_proc_vs_params_ = bgfx::createUniform(
      "u_liquidProcVsParams", bgfx::UniformType::Vec4,
      static_cast<std::uint16_t>(liquid_proc_vs_param::kCount));
  u_proc_fs_params_ = bgfx::createUniform(
      "u_liquidProcFsParams", bgfx::UniformType::Vec4,
      static_cast<std::uint16_t>(liquid_proc_fs_param::kCount));

  u_fog_params_ =
      bgfx::createUniform("u_fogParams", bgfx::UniformType::Vec4);
  u_fog_color_ =
      bgfx::createUniform("u_fogColor", bgfx::UniformType::Vec4);

  const WorldEnvironmentSnapshot initial_environment{};
  const LiquidLookupPixels river = BuildDepthLookup(
      initial_environment.sky.colors[16], initial_environment.sky.colors[17],
      initial_environment.sky.water_shallow_alpha,
      initial_environment.sky.water_deep_alpha, false);
  const LiquidLookupPixels ocean = BuildDepthLookup(
      initial_environment.sky.colors[14], initial_environment.sky.colors[15],
      initial_environment.sky.ocean_shallow_alpha,
      initial_environment.sky.ocean_deep_alpha, true);
  const LiquidLookupPixels wmo = BuildWmoLookup(
      initial_environment.sky.colors[17],
      initial_environment.sky.water_shallow_alpha,
      initial_environment.sky.water_deep_alpha);
  procedural_river_depth_ = CreateLiquidLookupTexture(river);
  procedural_ocean_depth_ = CreateLiquidLookupTexture(ocean);
  procedural_wmo_water_ = CreateLiquidLookupTexture(wmo);
  transient_vb_ = bgfx::createDynamicVertexBuffer(
      1024u, layout_, BGFX_BUFFER_ALLOW_RESIZE);

  initialized_ = true;
  const auto handle_is_valid = [](const auto handle) {
    return bgfx::isValid(handle);
  };
  const bool valid =
      std::all_of(water_programs_.begin(), water_programs_.end(),
                  handle_is_valid) &&
      std::all_of(water_no_spec_programs_.begin(),
                  water_no_spec_programs_.end(), handle_is_valid) &&
      bgfx::isValid(magma_program_) &&
      bgfx::isValid(procedural_water_program_) &&
      bgfx::isValid(water_ripple_program_) &&
      bgfx::isValid(u_water_ripple_flags_) &&
      bgfx::isValid(s_water_ripple_texture_) &&
      std::all_of(samplers_.begin(), samplers_.end(), handle_is_valid) &&
      bgfx::isValid(u_vs_params_) && bgfx::isValid(u_fs_params_) &&
      bgfx::isValid(u_proc_vs_params_) && bgfx::isValid(u_proc_fs_params_) &&
      bgfx::isValid(u_fog_params_) && bgfx::isValid(u_fog_color_) &&
      bgfx::isValid(procedural_river_depth_) &&
      bgfx::isValid(procedural_ocean_depth_) &&
      bgfx::isValid(procedural_wmo_water_) && bgfx::isValid(transient_vb_);
  if (!valid) {
    diagnostics::Log(diagnostics::LogLevel::kWarn,
                     "WaterRenderer: retail liquid resource creation failed");
    Shutdown();
    return false;
  }
  return true;
}

void WaterRenderer::ReplaceOwnedWaterHeightfields(
    const SurfaceOwnerId owner,
    std::vector<world::WaterHeightfield> heightfields) {
  heightfields.erase(
      std::remove_if(heightfields.begin(), heightfields.end(),
                     [](world::WaterHeightfield& heightfield) {
                       NormalizeHeightfield(heightfield);
                       return heightfield.heights.empty();
                     }),
      heightfields.end());
  if (heightfields.empty()) {
    RemoveOwnedWaterHeightfields(owner);
    return;
  }

  auto existing = surface_batches_.find(owner);
  if (existing == surface_batches_.end()) {
    SurfaceBatch batch{};
    batch.surfaces = std::move(heightfields);
    persistent_surface_count_ += batch.surfaces.size();
    surface_batches_.emplace(owner, std::move(batch));
    return;
  }
  persistent_surface_count_ -= existing->second.surfaces.size();
  existing->second.surfaces = std::move(heightfields);
  persistent_surface_count_ += existing->second.surfaces.size();
  existing->second.mesh_dirty = true;
}

void WaterRenderer::RemoveOwnedWaterHeightfields(const SurfaceOwnerId owner) {
  const auto existing = surface_batches_.find(owner);
  if (existing == surface_batches_.end()) {
    return;
  }
  persistent_surface_count_ -= existing->second.surfaces.size();
  DestroyBatchGpuResources(existing->second);
  surface_batches_.erase(existing);
}

void WaterRenderer::AddTransientWaterHeightfield(
    const world::WaterHeightfield& heightfield) {
  world::WaterHeightfield normalized = heightfield;
  NormalizeHeightfield(normalized);
  if (normalized.heights.empty()) {
    return;
  }
  transient_surfaces_.push_back(std::move(normalized));
  transient_mesh_dirty_ = true;
}

void WaterRenderer::SetTransientWaterHeightfields(
    const std::span<const std::shared_ptr<const world::WaterHeightfield>>
        sources) {
  if (std::equal(sources.begin(), sources.end(),
                 transient_surface_sources_.begin(),
                 transient_surface_sources_.end())) {

    return;
  }
  transient_surface_sources_.assign(sources.begin(), sources.end());
  transient_surfaces_.clear();
  transient_draw_ranges_.clear();
  transient_vertex_count_ = 0u;
  transient_index_count_ = 0u;
  for (const auto& source : transient_surface_sources_) {
    if (source) {
      AddTransientWaterHeightfield(*source);
    }
  }
  transient_mesh_dirty_ = true;
}

void WaterRenderer::ClearSurfaces() {
  for (auto& [owner, batch] : surface_batches_) {
    static_cast<void>(owner);
    DestroyBatchGpuResources(batch);
  }
  surface_batches_.clear();
  persistent_surface_count_ = 0u;
  ClearTransientSurfaces();
  water_ripples_ = {};
  next_ordinary_ripple_slot_ = 32u;
  next_local_player_ripple_slot_ = 0u;
}

void WaterRenderer::ClearTransientSurfaces() {
  transient_surface_sources_.clear();
  transient_surfaces_.clear();
  transient_draw_ranges_.clear();
  transient_vertex_count_ = 0u;
  transient_index_count_ = 0u;
  transient_mesh_dirty_ = true;
}

void WaterRenderer::SpawnWaterRipple(
    const world::SpawnWaterRippleCommand& command) {

  if (!openwow::world::CWorld_AreWaterRipplesEnabled()) {
    return;
  }
  if (command.control_points.empty() || command.duration_seconds <= 0.0f) {
    return;
  }

  std::uint32_t slot;
  if (command.use_local_player_pool) {
    slot = next_local_player_ripple_slot_++;
    if (next_local_player_ripple_slot_ >= 32u) {
      next_local_player_ripple_slot_ = 0u;
    }
  } else {
    slot = next_ordinary_ripple_slot_++;
    if (next_ordinary_ripple_slot_ >= water_ripples_.size()) {
      next_ordinary_ripple_slot_ = 32u;
    }
  }

  constexpr float kOpacityRampFraction = 0.4f;
  constexpr float kOpacityRampThreshold = 1.0f / 6.0f;
  const float max_opacity = command.opacity_base < kOpacityRampThreshold
                                ? command.opacity_base / kOpacityRampThreshold
                                : 1.0f;
  water_ripples_[slot] = WaterRipple{
      .position = command.position,
      .control_points = command.control_points,
      .rotation_radians = command.rotation_radians,
      .extent = command.initial_extent,
      .extent_rate = command.extent_rate,
      .opacity = 0.0f,
      .max_opacity = max_opacity,
      .opacity_ramp_up =
          max_opacity / (kOpacityRampFraction * command.duration_seconds),
      .opacity_ramp_down =
          -max_opacity /
          ((1.0f - kOpacityRampFraction) * command.duration_seconds),
      .remaining_lifetime = command.duration_seconds,
      .texture_bucket =
          static_cast<std::uint8_t>(command.use_splash_texture ? 1u : 0u),
      .active = true,
  };
}

void WaterRenderer::Update(const float delta_seconds) {
  if (!std::isfinite(delta_seconds) || delta_seconds <= 0.0f) {
    return;
  }
  animation_milliseconds_ += static_cast<double>(delta_seconds) * 1000.0;
  for (WaterRipple& ripple : water_ripples_) {
    if (!ripple.active) {
      continue;
    }
    ripple.remaining_lifetime -= delta_seconds;
    if (ripple.remaining_lifetime <= 0.0f) {
      ripple.active = false;
      continue;
    }
    ripple.extent += ripple.extent_rate * delta_seconds;

    float ramp_down_seconds = 0.0f;
    if (ripple.opacity_ramp_up != 0.0f) {
      const float next_opacity =
          ripple.opacity + ripple.opacity_ramp_up * delta_seconds;
      ripple.opacity = next_opacity;
      if (next_opacity > ripple.max_opacity) {
        ramp_down_seconds =
            delta_seconds - (next_opacity - ripple.max_opacity) /
                                ripple.opacity_ramp_up;
        ripple.opacity = ripple.max_opacity;
        ripple.opacity_ramp_up = 0.0f;
      }
    } else {
      ramp_down_seconds = delta_seconds;
    }
    ripple.opacity += ripple.opacity_ramp_down * ramp_down_seconds;
    if (ripple.opacity <= 0.0f) {
      ripple.active = false;
    }
  }
}

void WaterRenderer::SetLiquidStores(
    LiquidTypeLookupFn liquid_types,
    LiquidMaterialLookupFn liquid_materials) {
  liquid_type_lookup_ = std::move(liquid_types);
  liquid_material_lookup_ = std::move(liquid_materials);
  texture_sequences_.clear();
  for (auto& [owner, batch] : surface_batches_) {
    static_cast<void>(owner);
    batch.mesh_dirty = true;
  }
  transient_mesh_dirty_ = true;
}

std::optional<WaterRenderer::ResolvedMaterial>
WaterRenderer::ResolveMaterial(std::uint32_t liquid_type_id) const {
  if (!liquid_type_lookup_ || !liquid_material_lookup_) {
    return std::nullopt;
  }
  if (liquid_type_id == 0u) {
    liquid_type_id = 1u;
  }
  const data::dbc::LiquidTypeEntry* type =
      liquid_type_lookup_(liquid_type_id);
  if (type == nullptr && liquid_type_id != 1u) {
    type = liquid_type_lookup_(1u);
  }
  if (type == nullptr) {
    return std::nullopt;
  }
  const data::dbc::LiquidMaterialEntry* material =
      liquid_material_lookup_(type->liquid_material_id);
  if (material == nullptr || material->id < 1u || material->id > 3u) {
    return std::nullopt;
  }
  return ResolvedMaterial{
      .type = type,
      .material = material,
      .family = static_cast<MaterialFamily>(material->id),
      .second_queue = (material->flags & 1u) != 0u,
  };
}

bgfx::ProgramHandle WaterRenderer::ProgramFor(
    const MaterialFamily family, const std::size_t point_light_count) const {
  switch (family) {
    case MaterialFamily::kWater: {
      const std::size_t index =
          std::min(point_light_count, water_programs_.size() - 1u);
      return specular_enabled_ ? water_programs_[index]
                               : water_no_spec_programs_[index];
    }
    case MaterialFamily::kMagma:
      return magma_program_;
    case MaterialFamily::kProceduralWater:
      return procedural_water_program_;
  }
  return BGFX_INVALID_HANDLE;
}

bgfx::TextureHandle WaterRenderer::ResolveProceduralTexture(
    const std::string_view texture_name) const {
  const std::string identity = NormalizeTextureIdentity(texture_name);
  if (identity == "proceduralriverdepthtex") {
    return procedural_river_depth_;
  }
  if (identity == "proceduraloceandepthtex") {
    return procedural_ocean_depth_;
  }
  if (identity == "proceduralwmowatertex") {
    return procedural_wmo_water_;
  }
  return BGFX_INVALID_HANDLE;
}

bgfx::TextureHandle WaterRenderer::ResolveTexture(
    const std::string_view texture_name, const std::uint32_t period_ms,
    const std::uint8_t sampler_index) {
  if (texture_name.empty()) {
    return BGFX_INVALID_HANDLE;
  }
  const bgfx::TextureHandle procedural =
      ResolveProceduralTexture(texture_name);
  if (bgfx::isValid(procedural)) {
    return procedural;
  }

  const std::string key = NormalizeTextureIdentity(texture_name);
  TextureSequence& sequence = texture_sequences_[key];
  if (sequence.paths.empty()) {
    if (texture_name.find("%d") == std::string_view::npos) {
      sequence.paths.emplace_back(texture_name);
    } else {
      sequence.paths.reserve(kLiquidAnimationFrameCount);
      for (std::uint32_t frame = 1u; frame <= kLiquidAnimationFrameCount;
           ++frame) {
        sequence.paths.push_back(FormatTextureFrame(texture_name, frame));
      }
    }
    sequence.resident.resize(sequence.paths.size());
    sequence.queue_requested.assign(sequence.paths.size(), false);
  }

  for (std::size_t index = 0u; index < sequence.paths.size(); ++index) {
    if (sequence.queue_requested[index]) {
      continue;
    }
    if (texture_manager_.QueueTextureLoad(
            sequence.paths[index], TextureLoadFailurePolicy::kStrict,
            sampler_index == 0u ? TextureLoadPriority::kDemand
                                : TextureLoadPriority::kPrefetch)) {
      sequence.queue_requested[index] = true;
    }
  }

  std::size_t frame = 0u;
  if (sequence.paths.size() > 1u) {
    frame = RetailLiquidTextureFrame(
        RetailAnimationTick(animation_milliseconds_), period_ms,
        sequence.paths.size());
  }

  for (std::size_t index = 0u; index < sequence.paths.size(); ++index) {
    if (!sequence.resident[index].valid()) {
      sequence.resident[index] =
          texture_manager_.AcquireCachedTextureStrict(sequence.paths[index]);
    }
  }
  if (!sequence.resident[frame].valid()) {
    sequence.resident[frame] = texture_manager_.AcquireTextureAsync(
        sequence.paths[frame], TextureLoadFailurePolicy::kStrict,
        TextureLoadPriority::kDemand);
    if (!sequence.resident[frame].valid()) {
      return BGFX_INVALID_HANDLE;
    }
  }
  return BgfxTextureLeaseAccess::Get(sequence.resident[frame]);
}

std::optional<WaterRenderer::MaterialTextureBinding>
WaterRenderer::ResolveMaterialBinding(const ResolvedMaterial& resolved) {
  const data::dbc::LiquidTypeEntry& type = *resolved.type;
  MaterialTextureBinding binding{};
  switch (resolved.family) {
    case MaterialFamily::kWater:
      binding.texture_count = 2u;
      break;
    case MaterialFamily::kMagma:
      binding.texture_count = 1u;
      break;
    case MaterialFamily::kProceduralWater:
      binding.texture_count = 6u;
      break;
  }

  for (std::size_t index = 0u; index < binding.texture_count; ++index) {

    std::size_t liquid_type_texture = index;
    std::uint32_t period = kDefaultTexturePeriodMs;
    if (resolved.family == MaterialFamily::kWater) {
      liquid_type_texture = index == 0u ? 1u : 0u;
      if (index == 1u) {
        period = type.int_params[1];
      }
    } else if (resolved.family == MaterialFamily::kProceduralWater) {

      if (index == 2u || index == 3u) {
        period = type.int_params[index - 1u];
      } else if (index == 5u) {
        period = type.int_params[3u];
      }
    }
    const bgfx::TextureHandle texture =
        ResolveTexture(type.textures[liquid_type_texture], period,
                       static_cast<std::uint8_t>(index));
    if (!bgfx::isValid(texture)) {
      return std::nullopt;
    }
    binding.textures[index] = texture;
  }

  binding.uv0 = {1.0f, 0.0f, 0.0f, 0.0f};
  binding.uv1 = {1.0f, 0.0f, 0.0f, 0.0f};
  if (resolved.family == MaterialFamily::kWater) {

    binding.uv0 = {1.0f, type.float_params[2], 0.0f, 0.0f};
    binding.uv1 = {type.float_params[0],
                   type.float_params[1] * kRetailLiquidRotationMultiplier,
                   0.0f, 0.0f};
  } else if (resolved.family == MaterialFamily::kMagma) {
    const std::uint32_t tick = RetailAnimationTick(animation_milliseconds_);
    binding.uv0[2] = RetailLiquidTextureTranslation(tick, type.float_params[0]);
    binding.uv0[3] = RetailLiquidTextureTranslation(tick, type.float_params[1]);
  }

  const LiquidPixelShaderMaterialConstants pixel_constants =
      BuildLiquidPixelShaderMaterialConstants(type);
  binding.material_parameters = {pixel_constants.reciprocal_scale_and_params,
                                 pixel_constants.trailing_params};

  binding.procedural = resolved.family == MaterialFamily::kProceduralWater;
  if (binding.procedural) {
    binding.texture_constants = BuildProceduralLiquidTextureConstants(type);
  }
  return binding;
}

void WaterRenderer::ApplyMaterialTextures(
    const MaterialTextureBinding& binding, bgfx::Encoder* const encoder) const {
  const DrawEncoder draw{encoder};
  for (std::size_t index = 0u; index < binding.texture_count; ++index) {
    draw.setTexture(static_cast<std::uint8_t>(index), samplers_[index],
                    binding.textures[index]);
  }
}

void WaterRenderer::WriteMaterialParams(const MaterialTextureBinding& binding,
                                        const MaterialFamily family,
                                        LiquidPassParams& params) {
  if (family == MaterialFamily::kProceduralWater) {
    static_assert(std::tuple_size_v<decltype(binding.texture_constants.transforms)> ==
                  liquid_proc_vs_param::kCount -
                      liquid_proc_vs_param::kUvTransforms);
    std::copy(binding.texture_constants.transforms.begin(),
              binding.texture_constants.transforms.end(),
              params.proc_vs.begin() + liquid_proc_vs_param::kUvTransforms);
    static_assert(std::tuple_size_v<decltype(binding.material_parameters)> ==
                  liquid_proc_fs_param::kMaterialParamCount);
    std::copy(binding.material_parameters.begin(),
              binding.material_parameters.end(),
              params.proc_fs.begin() + liquid_proc_fs_param::kMaterialParams);
    return;
  }

  params.vs[liquid_vs_param::kUvTransform0] = binding.uv0;
  params.vs[liquid_vs_param::kUvTransform1] = binding.uv1;
}

void WaterRenderer::UpdateProceduralTextures(
    const WorldEnvironmentSnapshot& environment) {
  const ProceduralLookupState state{
      .colors = {environment.sky.colors[14], environment.sky.colors[15],
                 environment.sky.colors[16], environment.sky.colors[17]},
      .alpha = {environment.sky.water_shallow_alpha,
                environment.sky.water_deep_alpha,
                environment.sky.ocean_shallow_alpha,
                environment.sky.ocean_deep_alpha}};
  if (procedural_lookup_state_ == state) {
    return;
  }
  const LiquidLookupPixels river = BuildDepthLookup(
      environment.sky.colors[16], environment.sky.colors[17],
      environment.sky.water_shallow_alpha,
      environment.sky.water_deep_alpha, false);
  const LiquidLookupPixels ocean = BuildDepthLookup(
      environment.sky.colors[14], environment.sky.colors[15],
      environment.sky.ocean_shallow_alpha,
      environment.sky.ocean_deep_alpha, true);
  const LiquidLookupPixels wmo = BuildWmoLookup(
      environment.sky.colors[17], environment.sky.water_shallow_alpha,
      environment.sky.water_deep_alpha);
  UpdateLiquidLookupTexture(procedural_river_depth_, river);
  UpdateLiquidLookupTexture(procedural_ocean_depth_, ocean);
  UpdateLiquidLookupTexture(procedural_wmo_water_, wmo);
  procedural_lookup_state_ = state;
}

void WaterRenderer::EnsureBatchGpuResources(SurfaceBatch& batch) {
  if (!bgfx::isValid(batch.vb)) {
    batch.vb = bgfx::createDynamicVertexBuffer(
        1024u, layout_, BGFX_BUFFER_ALLOW_RESIZE);
  }
}

void WaterRenderer::DestroyBatchGpuResources(SurfaceBatch& batch) {
  if (IsRendererRuntimeAvailable()) {
    if (bgfx::isValid(batch.vb)) {
      bgfx::destroy(batch.vb);
    }
    if (bgfx::isValid(batch.ib)) {
      bgfx::destroy(batch.ib);
    }
  }
  batch.vb = BGFX_INVALID_HANDLE;
  batch.ib = BGFX_INVALID_HANDLE;
  batch.draw_ranges.clear();
  batch.vertex_count = 0u;
  batch.index_count = 0u;
}

void WaterRenderer::RebuildMesh(
    const std::vector<world::WaterHeightfield>& surfaces,
    bgfx::DynamicVertexBufferHandle& vb, bgfx::IndexBufferHandle& ib,
    std::vector<DrawRange>& draw_ranges, std::uint32_t& vertex_count,
    std::uint32_t& index_count, bool& mesh_dirty) {
  draw_ranges.clear();
  if (surfaces.empty()) {
    if (bgfx::isValid(ib)) {
      bgfx::destroy(ib);
    }
    ib = BGFX_INVALID_HANDLE;
    vertex_count = 0u;
    index_count = 0u;
    mesh_dirty = false;
    return;
  }

  std::vector<std::size_t> order(surfaces.size());
  std::iota(order.begin(), order.end(), 0u);
  std::stable_sort(order.begin(), order.end(), [&](const std::size_t lhs,
                                                   const std::size_t rhs) {
    const auto key = [&](const world::WaterHeightfield& surface) {
      const std::uint32_t id = surface.render_liquid_type_id != 0u
                                   ? surface.render_liquid_type_id
                                   : surface.liquid_type_id;
      const auto material = ResolveMaterial(id);
      return material.has_value()
                 ? std::tuple{material->second_queue,
                              static_cast<std::uint8_t>(material->family), id}
                 : std::tuple{true, std::uint8_t{0xffu}, id};
    };
    return key(surfaces[lhs]) < key(surfaces[rhs]);
  });

  std::vector<WaterVertex> vertices;
  std::vector<std::uint32_t> indices;
  static_assert(sizeof(WaterVertex) == 44u);

  for (const std::size_t surface_index : order) {
    const world::WaterHeightfield& surface = surfaces[surface_index];
    const std::size_t surface_vertex_count =
        static_cast<std::size_t>(surface.vertex_rows) * surface.vertex_cols;
    if (surface.vertex_rows < 2u || surface.vertex_cols < 2u ||
        surface.heights.size() != surface_vertex_count) {
      continue;
    }
    const std::uint32_t liquid_type_id =
        surface.render_liquid_type_id != 0u
            ? surface.render_liquid_type_id
            : (surface.liquid_type_id != 0u ? surface.liquid_type_id : 1u);
    const auto material = ResolveMaterial(liquid_type_id);
    if (!material.has_value()) {
      continue;
    }

    const bool has_depths =
        surface.vertex_depths.size() == surface_vertex_count;
    const bool has_texcoords =
        surface.vertex_texcoords.size() == surface_vertex_count;
    const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
    const std::uint32_t first_index =
        static_cast<std::uint32_t>(indices.size());
    RenderVec3 surface_min{std::numeric_limits<float>::max(),
                           std::numeric_limits<float>::max(),
                           std::numeric_limits<float>::max()};
    RenderVec3 surface_max{std::numeric_limits<float>::lowest(),
                           std::numeric_limits<float>::lowest(),
                           std::numeric_limits<float>::lowest()};
    for (std::uint32_t row = 0u; row < surface.vertex_rows; ++row) {
      for (std::uint32_t column = 0u; column < surface.vertex_cols; ++column) {
        const std::size_t index = VertexIndex(surface, row, column);
        WaterVertex vertex{};
        vertex.position = SurfacePosition(surface, row, column);
        for (std::size_t axis = 0u; axis < vertex.position.size(); ++axis) {
          surface_min[axis] = std::min(surface_min[axis], vertex.position[axis]);
          surface_max[axis] = std::max(surface_max[axis], vertex.position[axis]);
        }
        vertex.normal = SurfaceNormal(surface, row, column);

        vertex.color = surface.vertex_color;
        const std::array<float, 2> generated_texcoord{
            vertex.position[0] * kLiquidWorldTextureScale,
            vertex.position[1] * kLiquidWorldTextureScale,
        };
        vertex.texcoord0 =
            has_texcoords ? surface.vertex_texcoords[index]
                          : generated_texcoord;
        vertex.texcoord1 = {
            surface.depth_lane_u,
            has_depths ? surface.vertex_depths[index] : 0.0f};
        vertices.push_back(vertex);
      }
    }

    for (std::uint32_t row = 0u; row + 1u < surface.vertex_rows; ++row) {
      for (std::uint32_t column = 0u;
           column + 1u < surface.vertex_cols; ++column) {
        if (!IsCellRendered(surface, row, column)) {
          continue;
        }
        const std::uint32_t i00 =
            base + row * surface.vertex_cols + column;
        const std::uint32_t i10 = i00 + 1u;
        const std::uint32_t i01 = i00 + surface.vertex_cols;
        const std::uint32_t i11 = i01 + 1u;
        indices.insert(indices.end(), {i00, i01, i10, i10, i01, i11});
      }
    }

    if (!surface.fringe_indices.empty()) {
      const auto fringe_base = static_cast<std::uint32_t>(vertices.size());
      for (const world::LiquidFringeVertex& source : surface.fringe_vertices) {
        WaterVertex vertex{};
        vertex.position = source.position;
        for (std::size_t axis = 0u; axis < vertex.position.size(); ++axis) {
          surface_min[axis] = std::min(surface_min[axis], vertex.position[axis]);
          surface_max[axis] = std::max(surface_max[axis], vertex.position[axis]);
        }
        vertex.normal = source.normal;
        vertex.color = surface.vertex_color;

        vertex.texcoord0 =
            has_texcoords ? source.texcoord
                          : std::array<float, 2>{
                                vertex.position[0] * kLiquidWorldTextureScale,
                                vertex.position[1] * kLiquidWorldTextureScale};
        vertex.texcoord1 = {surface.depth_lane_u, source.depth};
        vertices.push_back(vertex);
      }
      for (const std::uint32_t fringe_index : surface.fringe_indices) {
        indices.push_back(fringe_base + fringe_index);
      }
    }

    const std::uint32_t count =
        static_cast<std::uint32_t>(indices.size()) - first_index;
    if (count == 0u) {
      vertices.resize(base);
      continue;
    }
    const RenderVec3 lighting_extent{(surface_max[0] - surface_min[0]) * 0.5f,
                                     (surface_max[1] - surface_min[1]) * 0.5f,
                                     (surface_max[2] - surface_min[2]) * 0.5f};
    draw_ranges.push_back(
        {.liquid_type_id = liquid_type_id,
         .first_index = first_index,
         .index_count = count,
         .family = material->family,
         .second_queue = material->second_queue,
         .lighting_position = {(surface_min[0] + surface_max[0]) * 0.5f,
                               (surface_min[1] + surface_max[1]) * 0.5f,
                               (surface_min[2] + surface_max[2]) * 0.5f},

         .lighting_radius = std::sqrt(lighting_extent[0] * lighting_extent[0] +
                                      lighting_extent[1] * lighting_extent[1] +
                                      lighting_extent[2] * lighting_extent[2])});
  }

  vertex_count = static_cast<std::uint32_t>(vertices.size());
  index_count = static_cast<std::uint32_t>(indices.size());
  if (!vertices.empty()) {
    bgfx::update(vb, 0u,
                 bgfx::copy(vertices.data(), static_cast<std::uint32_t>(
                                                 vertices.size() *
                                                 sizeof(WaterVertex))));
  }
  if (bgfx::isValid(ib)) {
    bgfx::destroy(ib);
  }
  ib = BGFX_INVALID_HANDLE;
  if (!indices.empty()) {
    ib = bgfx::createIndexBuffer(
        bgfx::copy(indices.data(), static_cast<std::uint32_t>(
                                       indices.size() * sizeof(std::uint32_t))),
        BGFX_BUFFER_INDEX32);
  }
  mesh_dirty = false;
}

WaterRenderer::LiquidPassParams WaterRenderer::BuildPassFrameConstants(
    const RenderVec3& camera_position,
    const WorldEnvironmentSnapshot& environment) const {
  const RenderVec4 ambient{environment.ambient[0], environment.ambient[1],
                           environment.ambient[2], 1.0f};
  const RenderVec4 diffuse{environment.diffuse[0], environment.diffuse[1],
                           environment.diffuse[2], 1.0f};
  const RenderVec4 specular{environment.specular[0], environment.specular[1],
                            environment.specular[2], 6.0f};
  const RenderVec4 light_dir{environment.surface_to_light[0],
                             environment.surface_to_light[1],
                             environment.surface_to_light[2], 0.0f};
  const RenderVec4 light_ray{-environment.surface_to_light[0],
                             -environment.surface_to_light[1],
                             -environment.surface_to_light[2], 0.0f};
  const RenderVec4 camera{camera_position[0], camera_position[1],
                          camera_position[2], 1.0f};

  LiquidPassParams params{};
  params.vs[liquid_vs_param::kAmbient] = ambient;
  params.vs[liquid_vs_param::kDiffuse] = diffuse;
  params.vs[liquid_vs_param::kSpecular] = specular;
  params.vs[liquid_vs_param::kLightDir] = light_dir;
  params.vs[liquid_vs_param::kCamera] = camera;

  params.fs[liquid_fs_param::kFogParams] = environment.fog.params;
  params.fs[liquid_fs_param::kFogColor] = environment.fog.color;

  const ProceduralLiquidVertexConstants waves =
      BuildProceduralLiquidVertexConstants(
          procedural_wave_state_, RetailAnimationTick(animation_milliseconds_));
  params.proc_vs[liquid_proc_vs_param::kWavePhases] = waves.phases;
  params.proc_vs[liquid_proc_vs_param::kWaveInverseFalloff] =
      waves.inverse_falloff_radii;
  std::copy(waves.waves.begin(), waves.waves.end(),
            params.proc_vs.begin() + liquid_proc_vs_param::kWaveRecords);
  params.proc_vs[liquid_proc_vs_param::kWaveInverseSpatialPeriod] =
      waves.inverse_spatial_periods;
  params.proc_vs[liquid_proc_vs_param::kWaveAmplitudes] = waves.amplitudes;

  params.proc_fs[liquid_proc_fs_param::kFogParams] = environment.fog.params;
  params.proc_fs[liquid_proc_fs_param::kFogColor] = environment.fog.color;
  params.proc_fs[liquid_proc_fs_param::kSpecular] = specular;
  params.proc_fs[liquid_proc_fs_param::kLightRay] = light_ray;
  params.proc_fs[liquid_proc_fs_param::kCamera] = camera;
  return params;
}

void WaterRenderer::RenderDrawRanges(
    const std::uint8_t view_id, const bgfx::DynamicVertexBufferHandle vb,
    const bgfx::IndexBufferHandle ib, const std::uint32_t vertex_count,
    const std::vector<DrawRange>& draw_ranges,
    const WorldEnvironmentSnapshot& environment, LiquidPassParams& params,
    const bool second_queue, bgfx::Encoder* const encoder) {
  if (draw_ranges.empty() || !bgfx::isValid(vb) || !bgfx::isValid(ib)) {
    return;
  }
  const DrawEncoder draw{encoder};

  constexpr std::uint64_t state =
      BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
      BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CW | BGFX_STATE_MSAA |
      BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                            BGFX_STATE_BLEND_INV_SRC_ALPHA);

  std::optional<std::uint32_t> bound_liquid_type_id;
  bool bound_ok = false;
  MaterialTextureBinding bound_binding{};
  MaterialFamily bound_family{MaterialFamily::kWater};

  for (const DrawRange& range : draw_ranges) {
    if (range.second_queue != second_queue) {
      continue;
    }
    if (!bound_liquid_type_id.has_value() ||
        *bound_liquid_type_id != range.liquid_type_id) {
      bound_liquid_type_id = range.liquid_type_id;
      const auto material = ResolveMaterial(range.liquid_type_id);
      bound_ok = false;
      if (material.has_value()) {
        bound_family = material->family;
        const std::optional<MaterialTextureBinding> binding =
            ResolveMaterialBinding(*material);
        if (binding.has_value()) {
          bound_binding = *binding;
          bound_ok = true;
        }
      }
    }
    if (!bound_ok || bound_family != range.family) {
      continue;
    }
    ApplyMaterialTextures(bound_binding, encoder);
    WriteMaterialParams(bound_binding, range.family, params);
    SpatialPointLightSelection selected{};
    if (range.family == MaterialFamily::kWater) {
      selected = SelectNearestSpatialPointLights(environment.point_lights,
                                                 range.lighting_position,
                                                 range.lighting_radius);
    }
    const std::size_t selected_count =
        std::min(selected.count, kMaxLiquidDrawPointLights);
    for (std::size_t index = 0u; index < selected_count; ++index) {
      const SpatialPointLight& light = *selected.lights[index];
      params.vs[liquid_vs_param::kPointLightPosition + index] = {
          light.position[0], light.position[1], light.position[2], 1.0f};
      params.vs[liquid_vs_param::kPointLightDiffuse + index] = {
          light.diffuse[0], light.diffuse[1], light.diffuse[2], 0.0f};
      params.vs[liquid_vs_param::kPointLightAttenuation + index] = {
          light.attenuation[0], light.attenuation[1], light.attenuation[2],
          0.0f};
    }
    const bgfx::ProgramHandle program = ProgramFor(range.family, selected_count);
    if (!bgfx::isValid(program)) {
      continue;
    }

    if (range.family == MaterialFamily::kProceduralWater) {
      draw.setUniform(u_proc_vs_params_, params.proc_vs.data(),
                      static_cast<std::uint16_t>(liquid_proc_vs_param::kCount));
      draw.setUniform(u_proc_fs_params_, params.proc_fs.data(),
                      static_cast<std::uint16_t>(liquid_proc_fs_param::kCount));
    } else {
      draw.setUniform(u_vs_params_, params.vs.data(),
                      static_cast<std::uint16_t>(liquid_vs_param::kCount));
      draw.setUniform(u_fs_params_, params.fs.data(),
                      static_cast<std::uint16_t>(liquid_fs_param::kCount));
    }

    draw.setState(state);
    draw.setVertexBuffer(0u, vb, 0u, vertex_count);
    draw.setIndexBuffer(ib, range.first_index, range.index_count);
    draw.submit(view_id, program);
  }
}

void WaterRenderer::RenderWaterRipples(
    const std::uint8_t view_id, const WorldEnvironmentSnapshot& environment,
    bgfx::Encoder* const encoder) {
  if (!bgfx::isValid(water_ripple_program_) ||
      !bgfx::isValid(u_water_ripple_flags_) ||
      !bgfx::isValid(s_water_ripple_texture_)) {
    return;
  }
  const DrawEncoder draw{encoder};

  constexpr std::uint64_t state =
      BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
      BGFX_STATE_DEPTH_TEST_LEQUAL | BGFX_STATE_MSAA |
      BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                            BGFX_STATE_BLEND_INV_SRC_ALPHA);

  const RenderVec4 particle_flags{1.0f, 1.0f, 0.0f, 1.0f};

  for (std::uint8_t bucket = 0u; bucket < 2u; ++bucket) {
    std::vector<RippleVertex> vertices;
    for (const WaterRipple& ripple : water_ripples_) {
      if (!ripple.active || ripple.texture_bucket != bucket ||
          ripple.extent <= 0.0f) {
        continue;
      }
      const std::size_t remaining_index_capacity =
          std::numeric_limits<std::uint16_t>::max() - vertices.size();
      const std::size_t point_count =
          std::min(ripple.control_points.size(), remaining_index_capacity);
      const float cosine = std::cos(ripple.rotation_radians);
      const float sine = std::sin(ripple.rotation_radians);
      const float inverse_diameter = 0.5f / ripple.extent;
      const auto alpha = static_cast<std::uint8_t>(
          std::clamp(ripple.opacity * 255.0f, 0.0f, 255.0f));
      vertices.reserve(vertices.size() + point_count);
      for (std::size_t index = 0u; index < point_count; ++index) {
        const auto& point = ripple.control_points[index];
        const float local_x = point[0] - ripple.position[0];
        const float local_y = point[1] - ripple.position[1];
        vertices.push_back(RippleVertex{
            .position = point,
            .color = {255u, 255u, 255u, alpha},
            .texcoord = {
                (cosine * local_x - sine * local_y) * inverse_diameter +
                    0.5f,
                (sine * local_x + cosine * local_y) * inverse_diameter +
                    0.5f,
            },
        });
      }
    }

    vertices.resize(vertices.size() - vertices.size() % 3u);
    if (vertices.empty()) {
      continue;
    }

    const bgfx::TextureHandle texture =
        BgfxTextureLeaseAccess::Get(water_ripple_textures_[bucket]);
    if (!bgfx::isValid(texture)) {
      continue;
    }
    const std::uint32_t vertex_count =
        static_cast<std::uint32_t>(vertices.size());
    if (bgfx::getAvailTransientVertexBuffer(vertex_count,
                                             water_ripple_layout_) <
            vertex_count ||
        bgfx::getAvailTransientIndexBuffer(vertex_count) < vertex_count) {
      continue;
    }
    bgfx::TransientVertexBuffer vertex_buffer;
    bgfx::TransientIndexBuffer index_buffer;
    bgfx::allocTransientVertexBuffer(&vertex_buffer, vertex_count,
                                     water_ripple_layout_);
    bgfx::allocTransientIndexBuffer(&index_buffer, vertex_count);

    if (vertex_buffer.stride == 0u ||
        vertex_buffer.size / vertex_buffer.stride < vertex_count ||
        index_buffer.size / sizeof(std::uint16_t) < vertex_count) {
      continue;
    }
    std::memcpy(vertex_buffer.data, vertices.data(),
                vertices.size() * sizeof(RippleVertex));
    auto* const indices =
        reinterpret_cast<std::uint16_t*>(index_buffer.data);
    std::iota(indices, indices + vertices.size(), std::uint16_t{0u});

    draw.setVertexBuffer(0u, &vertex_buffer);
    draw.setIndexBuffer(&index_buffer);
    draw.setUniform(u_water_ripple_flags_, particle_flags.data());

    draw.setUniform(u_fog_params_, environment.fog.params.data());
    draw.setUniform(u_fog_color_, environment.fog.color.data());
    draw.setTexture(0u, s_water_ripple_texture_, texture);
    draw.setState(state);
    draw.submit(view_id, water_ripple_program_);
  }
}

void WaterRenderer::Render(const std::uint8_t view_id,
                           const RenderMatrix4x4& view_mtx,
                           const RenderMatrix4x4& proj_mtx,
                           const RenderVec3& camera_position,
                           const WorldEnvironmentSnapshot& environment,
                           bgfx::Encoder* const encoder) {
  const bool has_ripples = std::any_of(
      water_ripples_.begin(), water_ripples_.end(),
      [](const WaterRipple& ripple) { return ripple.active; });
  if (!initialized_ ||
      (surface_batches_.empty() && transient_surfaces_.empty() &&
       !has_ripples)) {
    return;
  }
  UpdateProceduralTextures(environment);

  bgfx::setViewMode(view_id, bgfx::ViewMode::Sequential);

  for (auto& [owner, batch] : surface_batches_) {
    static_cast<void>(owner);
    EnsureBatchGpuResources(batch);
    if (batch.mesh_dirty) {
      RebuildMesh(batch.surfaces, batch.vb, batch.ib, batch.draw_ranges,
                  batch.vertex_count, batch.index_count, batch.mesh_dirty);
    }
  }
  if (transient_mesh_dirty_) {
    RebuildMesh(transient_surfaces_, transient_vb_, transient_ib_,
                transient_draw_ranges_, transient_vertex_count_,
                transient_index_count_, transient_mesh_dirty_);
  }

  bgfx::setViewTransform(view_id, view_mtx.data(), proj_mtx.data());

  LiquidPassParams params =
      BuildPassFrameConstants(camera_position, environment);

  for (const bool second_queue : {false, true}) {
    for (auto& [owner, batch] : surface_batches_) {
      static_cast<void>(owner);
      RenderDrawRanges(view_id, batch.vb, batch.ib, batch.vertex_count,
                       batch.draw_ranges, environment, params, second_queue,
                       encoder);
    }
    RenderDrawRanges(view_id, transient_vb_, transient_ib_,
                     transient_vertex_count_, transient_draw_ranges_,
                     environment, params, second_queue, encoder);
  }
  RenderWaterRipples(view_id, environment, encoder);
}

void WaterRenderer::Shutdown() {
  if (!initialized_) {
    return;
  }
  for (auto& [owner, batch] : surface_batches_) {
    static_cast<void>(owner);
    DestroyBatchGpuResources(batch);
  }
  if (IsRendererRuntimeAvailable()) {
    if (bgfx::isValid(transient_vb_)) {
      bgfx::destroy(transient_vb_);
    }
    if (bgfx::isValid(transient_ib_)) {
      bgfx::destroy(transient_ib_);
    }
    for (const bgfx::ProgramHandle program : water_programs_) {
      if (bgfx::isValid(program)) {
        bgfx::destroy(program);
      }
    }
    for (const bgfx::ProgramHandle program : water_no_spec_programs_) {
      if (bgfx::isValid(program)) {
        bgfx::destroy(program);
      }
    }
    for (const bgfx::ProgramHandle program :
         {magma_program_, procedural_water_program_, water_ripple_program_}) {
      if (bgfx::isValid(program)) {
        bgfx::destroy(program);
      }
    }
    for (const bgfx::UniformHandle sampler : samplers_) {
      if (bgfx::isValid(sampler)) {
        bgfx::destroy(sampler);
      }
    }
    for (const bgfx::UniformHandle uniform :
         {u_vs_params_, u_fs_params_, u_proc_vs_params_, u_proc_fs_params_,
          u_fog_params_, u_fog_color_, u_water_ripple_flags_,
          s_water_ripple_texture_}) {
      if (bgfx::isValid(uniform)) {
        bgfx::destroy(uniform);
      }
    }
    for (const bgfx::TextureHandle texture :
         {procedural_river_depth_, procedural_ocean_depth_,
          procedural_wmo_water_}) {
      if (bgfx::isValid(texture)) {
        bgfx::destroy(texture);
      }
    }
  }

  surface_batches_.clear();
  transient_surfaces_.clear();
  transient_draw_ranges_.clear();
  texture_sequences_.clear();
  procedural_wave_state_ = {};
  persistent_surface_count_ = 0u;
  transient_vertex_count_ = 0u;
  transient_index_count_ = 0u;
  water_programs_.fill(BGFX_INVALID_HANDLE);
  water_no_spec_programs_.fill(BGFX_INVALID_HANDLE);
  magma_program_ = BGFX_INVALID_HANDLE;
  procedural_water_program_ = BGFX_INVALID_HANDLE;
  water_ripple_program_ = BGFX_INVALID_HANDLE;
  samplers_.fill(BGFX_INVALID_HANDLE);
  u_vs_params_ = BGFX_INVALID_HANDLE;
  u_fs_params_ = BGFX_INVALID_HANDLE;
  u_proc_vs_params_ = BGFX_INVALID_HANDLE;
  u_proc_fs_params_ = BGFX_INVALID_HANDLE;
  u_fog_params_ = BGFX_INVALID_HANDLE;
  u_fog_color_ = BGFX_INVALID_HANDLE;
  u_water_ripple_flags_ = BGFX_INVALID_HANDLE;
  s_water_ripple_texture_ = BGFX_INVALID_HANDLE;
  procedural_river_depth_ = BGFX_INVALID_HANDLE;
  procedural_ocean_depth_ = BGFX_INVALID_HANDLE;
  procedural_wmo_water_ = BGFX_INVALID_HANDLE;
  procedural_lookup_state_.reset();
  water_ripple_textures_ = {};
  water_ripples_ = {};
  next_ordinary_ripple_slot_ = 32u;
  next_local_player_ripple_slot_ = 0u;
  animation_milliseconds_ = 0.0;
  transient_vb_ = BGFX_INVALID_HANDLE;
  transient_ib_ = BGFX_INVALID_HANDLE;
  initialized_ = false;
}

void WaterRenderer::Reset() {
  for (auto& [owner, batch] : surface_batches_) {
    static_cast<void>(owner);
    DestroyBatchGpuResources(batch);
  }
  surface_batches_.clear();
  transient_surfaces_.clear();
  transient_draw_ranges_.clear();
  texture_sequences_.clear();
  procedural_wave_state_ = {};
  persistent_surface_count_ = 0u;
  transient_vertex_count_ = 0u;
  transient_index_count_ = 0u;
  transient_mesh_dirty_ = true;
  animation_milliseconds_ = 0.0;
  procedural_lookup_state_.reset();
  water_ripples_ = {};
  next_ordinary_ripple_slot_ = 32u;
  next_local_player_ripple_slot_ = 0u;
}

}

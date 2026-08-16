#include "openwow/render/world/terrain/terrain_renderer.h"
#include "openwow/render/backend/bgfx/bgfx_texture_lease.h"

#include "openwow/data/texture_cache.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/render/resources/shaders/shader_registry.h"
#include "openwow/render/resources/textures/texture_manager.h"
#include "openwow/render/scene/shadow_data.h"
#include "openwow/world/world_render_pipeline.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace openwow::render {

PreparedTerrainMaterialTextures PrepareTerrainMaterialTextures(
    const PreparedTerrainTile &prepared,
    const std::function<std::vector<std::uint8_t>(const std::string &)> &loader) {
  return TerrainRenderer::PrepareMaterialTextures(prepared, loader);
}

namespace {

constexpr std::size_t kAlphaPixelBytes = 4u;

constexpr float kDiffuseRepeatsPerChunk = 8.0f;

using PreparedUploadsByRow = std::unordered_map<std::uint32_t, const PreparedTextureUpload *>;

[[nodiscard]] std::optional<TextureSliceBucket> ChooseChunkLayerBucket(
    const PreparedTerrainChunk &source, const int layer_count,
    const PreparedUploadsByRow &prepared_by_row, const TextureSliceArrays &slice_arrays) {
  std::optional<TextureSliceBucket> anchor;
  std::optional<TextureSliceBucket> first_bucketable;
  for (int layer = 0; layer < layer_count; ++layer) {
    if (source.texture_paths[layer].empty()) {
      continue;
    }
    const std::uint32_t row =
        openwow::data::HashTextureCachePath(source.texture_paths[layer]);
    const auto entry = prepared_by_row.find(row);
    if (entry == prepared_by_row.end() || entry->second == nullptr) {
      return std::nullopt;
    }
    const PreparedTextureUpload &upload = *entry->second;
    const bool solid = slice_arrays.IsSolidColor(row, upload);
    TextureSliceBucket bucket{};
    if (!TextureSliceArrays::TryMakeBucket(upload, bucket)) {

      if (solid) {
        continue;
      }
      return std::nullopt;
    }
    if (!first_bucketable.has_value()) {
      first_bucketable = bucket;
    }
    if (solid) {
      continue;
    }
    if (!anchor.has_value()) {
      anchor = bucket;
    } else if (*anchor != bucket) {
      return std::nullopt;
    }
  }

  return anchor.has_value() ? anchor : first_bucketable;
}

}

namespace terrain_vs_param {

inline constexpr std::size_t kSunDir = 0u;
inline constexpr std::size_t kLightAmbient = 1u;
inline constexpr std::size_t kLightDiffuse = 2u;
inline constexpr std::size_t kTerrainParams = 3u;

inline constexpr std::size_t kFrameConstantCount = 4u;

inline constexpr std::size_t kPointLightCount = 4u;
inline constexpr std::size_t kPointLightPosition = 5u;
inline constexpr std::size_t kPointLightDiffuse = 8u;
inline constexpr std::size_t kPointLightAttenuation = 11u;
inline constexpr std::size_t kCount = 14u;

}

namespace terrain_fs_param {

inline constexpr std::size_t kFogParams = 0u;
inline constexpr std::size_t kFogColor = 1u;
inline constexpr std::size_t kShadowMod = 2u;
inline constexpr std::size_t kColor = 3u;
inline constexpr std::size_t kFrameConstantCount = 4u;
inline constexpr std::size_t kCount = 4u;

}

static_assert(terrain_vs_param::kPointLightPosition +
                  TerrainRenderer::kMaxTerrainDrawPointLights ==
              terrain_vs_param::kPointLightDiffuse);
static_assert(terrain_vs_param::kPointLightDiffuse +
                  TerrainRenderer::kMaxTerrainDrawPointLights ==
              terrain_vs_param::kPointLightAttenuation);
static_assert(terrain_vs_param::kPointLightAttenuation +
                  TerrainRenderer::kMaxTerrainDrawPointLights ==
              terrain_vs_param::kCount);
static_assert(terrain_fs_param::kFrameConstantCount == terrain_fs_param::kCount);

TerrainRenderer::~TerrainRenderer() {
  Shutdown();
}

bool TerrainRenderer::Initialize() {
  if (initialized_)
    return true;

  layout_.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
      .add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)

      .add(bgfx::Attrib::TexCoord2, 4, bgfx::AttribType::Uint8, true)
      .end();

  const auto type = bgfx::getRendererType();

  program_ = CreateEmbeddedProgram(ShaderProgramId::Terrain, type);
  splat_program_ = CreateEmbeddedProgram(ShaderProgramId::TerrainSplat, type);
  splat_array_program_ = CreateEmbeddedProgram(ShaderProgramId::TerrainSplatArray, type);

  s_terrain_tex_[0] = bgfx::createUniform("s_terrainTex0", bgfx::UniformType::Sampler);
  s_terrain_tex_[1] = bgfx::createUniform("s_terrainTex1", bgfx::UniformType::Sampler);
  s_terrain_tex_[2] = bgfx::createUniform("s_terrainTex2", bgfx::UniformType::Sampler);
  s_terrain_tex_[3] = bgfx::createUniform("s_terrainTex3", bgfx::UniformType::Sampler);
  s_terrain_layers_ = bgfx::createUniform("s_terrainLayers", bgfx::UniformType::Sampler);
  s_terrain_alpha_ = bgfx::createUniform("s_terrainAlpha", bgfx::UniformType::Sampler);

  u_vs_params_ = bgfx::createUniform("u_terrainVsParams", bgfx::UniformType::Vec4,
                                     static_cast<std::uint16_t>(terrain_vs_param::kCount));
  u_fs_params_ = bgfx::createUniform("u_terrainFsParams", bgfx::UniformType::Vec4,
                                     static_cast<std::uint16_t>(terrain_fs_param::kCount));

  if (bgfx::isValid(splat_array_program_) && bgfx::isValid(s_terrain_layers_)) {
    static_cast<void>(slice_arrays_.Initialize());
  }

  if (!PipelineResourcesAreValid()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kError,
                              "TerrainRenderer: required pipeline resource creation failed");
    DestroyPipelineResources();
    return false;
  }

  initialized_ = true;
  return true;
}

PreparedTerrainMaterialTextures TerrainRenderer::PrepareMaterialTextures(
    const PreparedTerrainTile &prepared,
    const std::function<std::vector<std::uint8_t>(const std::string &)> &loader) {
  PreparedTerrainMaterialTextures materials;
  std::unordered_set<std::string> queued;
  for (const auto &chunk : prepared.chunks) {
    const int layer_count = std::clamp(chunk.layer_count, 0, kMaxTerrainLayers);
    for (int layer = 0; layer < layer_count; ++layer) {
      const auto &path = chunk.texture_paths[layer];
      if (path.empty() || !queued.insert(path).second) {
        continue;
      }
      auto upload = TextureManager::PrepareTextureUploadFromLoader(path, loader);
      if (upload.valid) {
        materials.uploads.push_back(std::move(upload));
      }
    }
  }
  return materials;
}

void TerrainRenderer::ResolveChunkLayerSlices(
    TerrainChunkGpu &chunk,
    std::array<std::uint8_t, kMaxTerrainLayers> &out_slices) const {
  chunk.layer_array_tex = BGFX_INVALID_HANDLE;
  out_slices.fill(0u);
  if (chunk.layer_count <= 0) {
    return;
  }

  std::uint16_t array = kInvalidTextureSliceArray;
  for (int layer = 0; layer < chunk.layer_count; ++layer) {
    const TextureSliceLocation location = chunk.layer_slice_leases[layer].location();

    if (!location.valid() || location.slice > UINT8_MAX ||
        (array != kInvalidTextureSliceArray && location.array != array)) {
      out_slices.fill(0u);
      return;
    }
    array = location.array;
    out_slices[static_cast<std::size_t>(layer)] = static_cast<std::uint8_t>(location.slice);
  }
  const bgfx::TextureHandle array_texture = slice_arrays_.ArrayTexture(array);
  if (!bgfx::isValid(array_texture)) {
    out_slices.fill(0u);
    return;
  }

  for (int layer = chunk.layer_count; layer < kMaxTerrainLayers; ++layer) {
    out_slices[static_cast<std::size_t>(layer)] = out_slices[0];
  }
  chunk.layer_array_tex = array_texture;
}

void TerrainRenderer::UploadPreparedAdt(const PreparedTerrainTile &prepared,
                                        const PreparedTerrainMaterialTextures &materials,
                                        const int32_t tile_x, const int32_t tile_y) {
  if (!initialized_) {
    return;
  }
  for (const auto &upload : materials.uploads) {
    static_cast<void>(texture_manager_.CommitPreparedTexture(upload));
  }
  if (!initialized_ || prepared.vertices.empty()) {
    return;
  }

  PreparedUploadsByRow prepared_by_row;
  prepared_by_row.reserve(materials.uploads.size());
  for (const auto &upload : materials.uploads) {
    if (upload.valid && !upload.path.empty()) {
      prepared_by_row.emplace(openwow::data::HashTextureCachePath(upload.path), &upload);
    }
  }

  RemoveAdt(tile_x, tile_y);

  TerrainTileGpu tile;
  tile.tile_x = tile_x;
  tile.tile_y = tile_y;

  std::array<std::array<std::uint8_t, kMaxTerrainLayers>, kChunksPerTile> chunk_layer_slices{};
  for (std::size_t chunk_index = 0; chunk_index < prepared.chunks.size(); ++chunk_index) {
    const auto &source = prepared.chunks[chunk_index];
    auto &chunk = tile.chunks[chunk_index];
    chunk.tile_x = tile_x;
    chunk.tile_y = tile_y;
    chunk.chunk_x = source.chunk_x;
    chunk.chunk_y = source.chunk_y;
    if (!source.valid || source.vertex_start > prepared.vertices.size() ||
        source.vertex_count > prepared.vertices.size() - source.vertex_start ||
        source.hole_index_start > prepared.hole_indices.size() ||
        source.hole_index_count > prepared.hole_indices.size() - source.hole_index_start) {
      continue;
    }
    chunk.vertex_start = source.vertex_start;
    chunk.vertex_count = source.vertex_count;
    chunk.world_x = source.bounds_min[0] + (source.bounds_max[0] - source.bounds_min[0]) * 0.5f;
    chunk.world_y = source.bounds_min[1] + (source.bounds_max[1] - source.bounds_min[1]) * 0.5f;
    chunk.world_z = source.bounds_min[2] + (source.bounds_max[2] - source.bounds_min[2]) * 0.5f;

    {
      const float extent_x = (source.bounds_max[0] - source.bounds_min[0]) * 0.5f;
      const float extent_y = (source.bounds_max[1] - source.bounds_min[1]) * 0.5f;
      const float extent_z = (source.bounds_max[2] - source.bounds_min[2]) * 0.5f;
      chunk.bounds_radius =
          std::sqrt(extent_x * extent_x + extent_y * extent_y + extent_z * extent_z);
    }
    std::memcpy(chunk.bounds_min, source.bounds_min, sizeof(chunk.bounds_min));
    std::memcpy(chunk.bounds_max, source.bounds_max, sizeof(chunk.bounds_max));
    chunk.layer_count = std::clamp(source.layer_count, 0, kMaxTerrainLayers);

    const std::optional<TextureSliceBucket> chunk_bucket =
        ChooseChunkLayerBucket(source, chunk.layer_count, prepared_by_row, slice_arrays_);
    for (int layer = 0; layer < chunk.layer_count; ++layer) {
      if (!source.texture_paths[layer].empty()) {
        chunk.layer_texture_leases[layer] =
            texture_manager_.AcquireCachedTexture(source.texture_paths[layer]);
        chunk.layer_tex[layer] = BgfxTextureLeaseAccess::Get(chunk.layer_texture_leases[layer]);

        if (chunk.layer_texture_leases[layer].valid()) {
          const std::uint32_t row =
              openwow::data::HashTextureCachePath(source.texture_paths[layer]);
          const auto upload = prepared_by_row.find(row);
          chunk.layer_slice_leases[layer] = slice_arrays_.Acquire(
              row, upload != prepared_by_row.end() ? upload->second : nullptr,
              chunk_bucket.has_value() ? &*chunk_bucket : nullptr);
        }
      }
      if (!bgfx::isValid(chunk.layer_tex[layer])) {
        chunk.layer_tex[layer] = texture_manager_.GetCheckerTexture();
      }
    }
    for (int layer = chunk.layer_count; layer < kMaxTerrainLayers; ++layer) {
      chunk.layer_tex[layer] = texture_manager_.GetWhiteTexture();
    }
    ResolveChunkLayerSlices(chunk, chunk_layer_slices[chunk_index]);
    chunk.valid = true;
  }

  const auto vertex_bytes =
      static_cast<std::uint32_t>(prepared.vertices.size() * sizeof(TerrainVertex));
  const bgfx::Memory *const vertex_memory = bgfx::alloc(vertex_bytes);
  std::memcpy(vertex_memory->data, prepared.vertices.data(), vertex_bytes);
  {
    auto *const vertices = reinterpret_cast<TerrainVertex *>(vertex_memory->data);
    for (std::size_t chunk_index = 0; chunk_index < tile.chunks.size(); ++chunk_index) {
      const TerrainChunkGpu &chunk = tile.chunks[chunk_index];
      if (!chunk.valid || !bgfx::isValid(chunk.layer_array_tex)) {
        continue;
      }
      const auto &slices = chunk_layer_slices[chunk_index];
      for (std::uint32_t vertex = 0; vertex < chunk.vertex_count; ++vertex) {
        std::memcpy(vertices[chunk.vertex_start + vertex].layer_slice, slices.data(),
                    slices.size());
      }
    }
  }
  tile.vb = bgfx::createVertexBuffer(vertex_memory, layout_);
  if (!bgfx::isValid(tile.vb)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                              "TerrainRenderer: tile vertex buffer creation failed for (" +
                                  std::to_string(tile_x) + "," + std::to_string(tile_y) + ")");
    return;
  }

  {
    const auto &lod0 = TerrainLodManager::GetLodIndexData(0);
    const auto &lod0_indices = lod0.indices;
    const std::uint32_t lod0_count = lod0.count;

    std::array<std::uint16_t, kChunksPerTile> material_order{};
    std::size_t ordered_chunks = 0u;
    for (std::size_t chunk_index = 0; chunk_index < prepared.chunks.size(); ++chunk_index) {
      if (tile.chunks[chunk_index].valid) {
        material_order[ordered_chunks++] = static_cast<std::uint16_t>(chunk_index);
      }
    }
    std::stable_sort(
        material_order.begin(),
        material_order.begin() + static_cast<std::ptrdiff_t>(ordered_chunks),
        [&tile](const std::uint16_t lhs, const std::uint16_t rhs) {
          const TerrainChunkGpu &left = tile.chunks[lhs];
          const TerrainChunkGpu &right = tile.chunks[rhs];
          const bool left_splat = left.layer_count > 0;
          const bool right_splat = right.layer_count > 0;
          if (left_splat != right_splat) {
            return left_splat < right_splat;
          }
          const bool left_array = bgfx::isValid(left.layer_array_tex);
          const bool right_array = bgfx::isValid(right.layer_array_tex);
          if (left_array != right_array) {
            return left_array < right_array;
          }
          if (left_array) {

            return left.layer_array_tex.idx < right.layer_array_tex.idx;
          }
          for (int layer = 0; layer < kMaxTerrainLayers; ++layer) {
            if (left.layer_tex[layer].idx != right.layer_tex[layer].idx) {
              return left.layer_tex[layer].idx < right.layer_tex[layer].idx;
            }
          }

          return false;
        });

    std::vector<std::uint16_t> batch_indices;
    batch_indices.reserve(static_cast<std::size_t>(kChunksPerTile) * lod0_count);
    for (std::size_t ordinal = 0; ordinal < ordered_chunks; ++ordinal) {
      const std::size_t chunk_index = material_order[ordinal];
      auto &chunk = tile.chunks[chunk_index];
      const auto &source = prepared.chunks[chunk_index];
      const bool uses_hole_indices = source.hole_index_count != 0u;
      const std::uint16_t *local = uses_hole_indices
                                       ? prepared.hole_indices.data() + source.hole_index_start
                                       : lod0_indices.data();
      const std::uint32_t local_count = uses_hole_indices ? source.hole_index_count : lod0_count;
      if (local_count == 0u) {
        chunk.valid = false;
        continue;
      }

      if (chunk.vertex_start + chunk.vertex_count > UINT16_MAX + 1u) {
        chunk.valid = false;
        continue;
      }
      chunk.batch_index_start = static_cast<std::uint32_t>(batch_indices.size());
      chunk.batch_index_count = local_count;
      for (std::uint32_t index = 0; index < local_count; ++index) {
        batch_indices.push_back(static_cast<std::uint16_t>(chunk.vertex_start + local[index]));
      }
    }

    if (batch_indices.empty()) {
      bgfx::destroy(tile.vb);
      return;
    }
    const bgfx::Memory *index_memory =
        bgfx::copy(batch_indices.data(),
                   static_cast<std::uint32_t>(batch_indices.size() * sizeof(std::uint16_t)));
    tile.index_buffer = bgfx::createIndexBuffer(index_memory);
    if (!bgfx::isValid(tile.index_buffer)) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                                "TerrainRenderer: tile index buffer creation failed for (" +
                                    std::to_string(tile_x) + "," + std::to_string(tile_y) + ")");
      bgfx::destroy(tile.vb);
      return;
    }
  }

  const std::size_t expected_alpha_bytes =
      static_cast<std::size_t>(kAlphaAtlasSize) * kAlphaAtlasSize * kAlphaPixelBytes;
  if (prepared.has_alpha_layers && prepared.alpha_atlas_rgba.size() >= expected_alpha_bytes) {
    const bgfx::Memory *alpha_memory = bgfx::copy(prepared.alpha_atlas_rgba.data(),
                                                  static_cast<std::uint32_t>(expected_alpha_bytes));
    tile.alpha_atlas = bgfx::createTexture2D(
        kAlphaAtlasSize, kAlphaAtlasSize, false, 1, bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, alpha_memory);
    if (!bgfx::isValid(tile.alpha_atlas)) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                                "TerrainRenderer: tile alpha atlas creation failed for (" +
                                    std::to_string(tile_x) + "," + std::to_string(tile_y) +
                                    "); using fallback terrain material");
      for (auto &chunk : tile.chunks) {
        chunk.layer_count = 0;
      }
    }
  } else if (prepared.has_alpha_layers) {
    for (auto &chunk : tile.chunks) {
      chunk.layer_count = 0;
    }
  }

  tile.valid = true;
  loaded_tiles_.push_back(std::move(tile));
}

bool TerrainRenderer::PipelineResourcesAreValid() const noexcept {
  bool texture_samplers_valid = true;
  for (const auto sampler : s_terrain_tex_) {
    texture_samplers_valid = texture_samplers_valid && bgfx::isValid(sampler);
  }

  return bgfx::isValid(program_) && bgfx::isValid(splat_program_) && texture_samplers_valid &&
         bgfx::isValid(s_terrain_alpha_) && bgfx::isValid(u_vs_params_) &&
         bgfx::isValid(u_fs_params_);
}

void TerrainRenderer::DestroyPipelineResources() {
  const auto destroy_program = [](bgfx::ProgramHandle &handle) {
    if (bgfx::isValid(handle)) {
      bgfx::destroy(handle);
    }
    handle = BGFX_INVALID_HANDLE;
  };
  const auto destroy_uniform = [](bgfx::UniformHandle &handle) {
    if (bgfx::isValid(handle)) {
      bgfx::destroy(handle);
    }
    handle = BGFX_INVALID_HANDLE;
  };

  destroy_program(program_);
  destroy_program(splat_program_);
  destroy_program(splat_array_program_);
  for (auto &sampler : s_terrain_tex_) {
    destroy_uniform(sampler);
  }
  destroy_uniform(s_terrain_layers_);
  destroy_uniform(s_terrain_alpha_);
  destroy_uniform(u_vs_params_);
  destroy_uniform(u_fs_params_);
}

const world::Frustum *TerrainRenderer::ResolveChunkCullFrustum() const {
  if (frustum_ == nullptr) {
    return nullptr;
  }

  if (!openwow::world::CWorld_HasRenderFlag(
          openwow::world::WorldRenderFlag::kTerrainCulling)) {
    return nullptr;
  }
  return frustum_;
}

void TerrainRenderer::Render(uint8_t view_id, const WorldEnvironmentSnapshot &environment,
                             const DrawSortDepth &sort_depth, bgfx::Encoder *const encoder) {
  if (!initialized_ || loaded_tiles_.empty())
    return;

  const DrawEncoder draw{encoder};

  const uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                         BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CW | BGFX_STATE_MSAA;

  const std::array<float, 4> shadow_mod = openwow::world::CWorld_GetTerrainShadowModColor();
  const RenderVec4 terrain_shadow_mod{
      shadow_mod[0], shadow_mod[1], shadow_mod[2],
      openwow::world::CWorld_HasRenderFlag(openwow::world::WorldRenderFlag::kTerrainShadows)
          ? 1.0f
          : 0.0f};

  const bool has_splat = bgfx::isValid(splat_program_);

  const bool has_splat_array =
      bgfx::isValid(splat_array_program_) && bgfx::isValid(s_terrain_layers_);

  std::array<RenderVec4, terrain_vs_param::kCount> vs_params{};
  vs_params[terrain_vs_param::kSunDir] = {environment.surface_to_light[0],
                                          environment.surface_to_light[1],
                                          environment.surface_to_light[2], 0.0f};
  vs_params[terrain_vs_param::kLightAmbient] = {environment.ambient[0], environment.ambient[1],
                                                environment.ambient[2], 0.0f};
  vs_params[terrain_vs_param::kLightDiffuse] = {environment.diffuse[0], environment.diffuse[1],
                                                environment.diffuse[2], 0.0f};
  vs_params[terrain_vs_param::kTerrainParams] = {kDiffuseRepeatsPerChunk, 0.0f, 0.0f, 0.0f};

  std::array<RenderVec4, terrain_fs_param::kCount> fs_params{};
  fs_params[terrain_fs_param::kFogParams] = environment.fog.params;
  fs_params[terrain_fs_param::kFogColor] = environment.fog.color;
  fs_params[terrain_fs_param::kShadowMod] = terrain_shadow_mod;
  fs_params[terrain_fs_param::kColor] = RenderVec4{1.0f, 1.0f, 1.0f, 1.0f};

  const auto make_batch_key = [&](const TerrainChunkGpu &gpu, const std::uint32_t lod,
                                  const TerrainProgramKind program) {
    TerrainBatchKey key;
    key.texture_indices.fill(bgfx::kInvalidHandle);
    key.lod = lod;
    key.program = program;
    if (program == TerrainProgramKind::kSplat) {
      for (std::size_t layer = 0; layer < key.texture_indices.size(); ++layer) {
        key.texture_indices[layer] = gpu.layer_tex[layer].idx;
      }
    } else if (program == TerrainProgramKind::kSplatArray) {

      key.layer_array_index = gpu.layer_array_tex.idx;
    }

    const RenderVec3 lighting_position{gpu.world_x, gpu.world_y, gpu.world_z};
    const SpatialPointLightSelection selected = SelectNearestSpatialPointLights(
        environment.point_lights, lighting_position, gpu.bounds_radius);
    key.point_light_count =
        static_cast<std::uint8_t>(std::min(selected.count, key.point_light_indices.size()));
    for (std::size_t index = 0; index < key.point_light_count; ++index) {
      key.point_light_indices[index] =
          static_cast<std::uint32_t>(selected.lights[index] - environment.point_lights.data());
    }
    return key;
  };

  const auto key_less = [](const TerrainBatchItem &lhs, const TerrainBatchItem &rhs) {
    if (lhs.key.program != rhs.key.program) {
      return lhs.key.program < rhs.key.program;
    }
    if (lhs.key.lod != rhs.key.lod) {
      return lhs.key.lod < rhs.key.lod;
    }
    if (lhs.key.layer_array_index != rhs.key.layer_array_index) {
      return lhs.key.layer_array_index < rhs.key.layer_array_index;
    }
    if (lhs.key.texture_indices != rhs.key.texture_indices) {
      return lhs.key.texture_indices < rhs.key.texture_indices;
    }
    if (lhs.key.point_light_count != rhs.key.point_light_count) {
      return lhs.key.point_light_count < rhs.key.point_light_count;
    }
    if (lhs.key.point_light_indices != rhs.key.point_light_indices) {
      return lhs.key.point_light_indices < rhs.key.point_light_indices;
    }

    return lhs.chunk->batch_index_start < rhs.chunk->batch_index_start;
  };

  const auto submit_batch = [&](const TerrainTileGpu &tile, const TerrainBatchKey &key,
                                const std::uint32_t depth) {
    draw.setState(state);

    for (std::size_t index = 0; index < kMaxTerrainDrawPointLights; ++index) {
      RenderVec4 position{};
      RenderVec4 light_diffuse{};
      RenderVec4 attenuation{};
      if (index < key.point_light_count) {
        const std::uint32_t light_index = key.point_light_indices[index];
        if (light_index < environment.point_lights.size()) {
          const SpatialPointLight &light = environment.point_lights[light_index];
          position = {light.position[0], light.position[1], light.position[2], 1.0f};
          light_diffuse = {light.diffuse[0], light.diffuse[1], light.diffuse[2], 0.0f};
          attenuation = {light.attenuation[0], light.attenuation[1], light.attenuation[2], 0.0f};
        }
      }
      vs_params[terrain_vs_param::kPointLightPosition + index] = position;
      vs_params[terrain_vs_param::kPointLightDiffuse + index] = light_diffuse;
      vs_params[terrain_vs_param::kPointLightAttenuation + index] = attenuation;
    }
    vs_params[terrain_vs_param::kPointLightCount] = {static_cast<float>(key.point_light_count),
                                                     0.0f, 0.0f, 0.0f};

    draw.setUniform(u_vs_params_, vs_params.data(),
                    static_cast<std::uint16_t>(terrain_vs_param::kCount));
    draw.setUniform(u_fs_params_, fs_params.data(),
                    static_cast<std::uint16_t>(terrain_fs_param::kCount));

    constexpr std::uint32_t sampler_flags = DiffuseSamplerFlags();
    if (key.program == TerrainProgramKind::kSplat) {
      for (std::size_t layer = 0; layer < key.texture_indices.size(); ++layer) {
        draw.setTexture(static_cast<std::uint8_t>(layer), s_terrain_tex_[layer],
                        bgfx::TextureHandle{key.texture_indices[layer]}, sampler_flags);
      }
      draw.setTexture(4, s_terrain_alpha_, tile.alpha_atlas,
                      BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    } else if (key.program == TerrainProgramKind::kSplatArray) {

      draw.setTexture(0, s_terrain_layers_,
                      bgfx::TextureHandle{key.layer_array_index}, sampler_flags);
      draw.setTexture(4, s_terrain_alpha_, tile.alpha_atlas,
                      BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    }

    if (shadow_data_ != nullptr) {
      shadow_data_->BindShadowState(encoder);
    }
    const bgfx::ProgramHandle program =
        key.program == TerrainProgramKind::kSplatArray ? splat_array_program_
        : key.program == TerrainProgramKind::kSplat    ? splat_program_
                                                       : program_;
    draw.submit(view_id, program, depth);
  };

  const world::Frustum *const cull_frustum = ResolveChunkCullFrustum();

  batch_items_.reserve(kChunksPerTile);
  for (const auto &tile : loaded_tiles_) {
    if (!tile.valid || !bgfx::isValid(tile.vb) || !bgfx::isValid(tile.index_buffer))
      continue;

    batch_items_.clear();
    for (const auto &gpu : tile.chunks) {
      if (!gpu.valid || gpu.batch_index_count == 0u)
        continue;

      if (cull_frustum != nullptr &&
          !cull_frustum->TestAABB(gpu.bounds_min[0], gpu.bounds_min[1], gpu.bounds_min[2],
                                  gpu.bounds_max[0], gpu.bounds_max[1], gpu.bounds_max[2])) {
        continue;
      }

      TerrainProgramKind program = TerrainProgramKind::kSolid;
      if (gpu.layer_count > 0 && bgfx::isValid(tile.alpha_atlas)) {
        if (has_splat_array && bgfx::isValid(gpu.layer_array_tex)) {
          program = TerrainProgramKind::kSplatArray;
        } else if (has_splat) {
          program = TerrainProgramKind::kSplat;
        }
      }
      batch_items_.push_back(TerrainBatchItem{
          .chunk = &gpu,
          .key = make_batch_key(gpu, 0u, program),
      });
    }

    if (batch_items_.empty()) {
      continue;
    }

    std::sort(batch_items_.begin(), batch_items_.end(), key_less);

    const auto chunk_sort_depth = [&sort_depth](const TerrainChunkGpu &chunk) {
      const float distance = sort_depth.DistanceToAabb(RenderVec3View{chunk.bounds_min, 3u},
                                                       RenderVec3View{chunk.bounds_max, 3u});
      return sort_depth.ForBandedDistance(distance, openwow::data::terrain::kChunkSize);
    };

    std::size_t first_item = 0u;
    while (first_item < batch_items_.size()) {
      const TerrainBatchItem &first = batch_items_[first_item];
      const std::uint32_t first_index = first.chunk->batch_index_start;
      std::uint32_t batch_index_count = first.chunk->batch_index_count;

      std::uint32_t batch_depth = chunk_sort_depth(*first.chunk);
      std::size_t end_item = first_item + 1u;
      while (end_item < batch_items_.size() &&
             batch_items_[end_item].key == first.key &&
             batch_items_[end_item].chunk->batch_index_start ==
                 first_index + batch_index_count) {
        batch_index_count += batch_items_[end_item].chunk->batch_index_count;
        batch_depth = std::min(batch_depth, chunk_sort_depth(*batch_items_[end_item].chunk));
        ++end_item;
      }

      draw.setVertexBuffer(0, tile.vb);
      draw.setIndexBuffer(tile.index_buffer, first_index, batch_index_count);
      submit_batch(tile, first.key, batch_depth);

      first_item = end_item;
    }
  }
}

void TerrainRenderer::RemoveAdt(const int32_t tile_x, const int32_t tile_y) {
  const auto tile = std::find_if(loaded_tiles_.begin(), loaded_tiles_.end(),
                                 [tile_x, tile_y](const TerrainTileGpu &candidate) {
                                   return candidate.tile_x == tile_x && candidate.tile_y == tile_y;
                                 });
  if (tile == loaded_tiles_.end()) {
    return;
  }
  DestroyTileGpu(*tile);
  loaded_tiles_.erase(tile);

  slice_arrays_.ReclaimEmptyArrays();
}

void TerrainRenderer::ClearTerrain() {
  for (auto &tile : loaded_tiles_) {
    DestroyTileGpu(tile);
  }
  loaded_tiles_.clear();

  slice_arrays_.ReclaimEmptyArrays();
}

void TerrainRenderer::Shutdown() {
  if (!initialized_)
    return;
  ClearTerrain();
  slice_arrays_.Shutdown();
  DestroyPipelineResources();

  initialized_ = false;
}

void TerrainRenderer::DestroyTileGpu(TerrainTileGpu &gpu) {
  if (bgfx::isValid(gpu.vb)) {
    bgfx::destroy(gpu.vb);
  }
  if (bgfx::isValid(gpu.alpha_atlas)) {
    bgfx::destroy(gpu.alpha_atlas);
  }
  if (bgfx::isValid(gpu.index_buffer)) {
    bgfx::destroy(gpu.index_buffer);
  }

  gpu = {};
}

void TerrainRenderer::SetViewDistance(uint32_t distance) {
  view_distance_ = std::clamp(distance, 1u, 10u);
}

void TerrainRenderer::SetTextureQuality(uint32_t quality) {
  texture_quality_ = std::min(quality, 2u);
}

uint32_t TerrainRenderer::total_triangles() const {
  uint32_t total = 0;
  for (const auto &tile : loaded_tiles_) {
    for (const auto &chunk : tile.chunks) {
      if (chunk.valid) {
        total += chunk.batch_index_count / 3u;
      }
    }
  }
  return total;
}

void TerrainRenderer::Reset() {
  view_distance_ = 4;
  texture_quality_ = 2;
  frustum_ = nullptr;
}

}

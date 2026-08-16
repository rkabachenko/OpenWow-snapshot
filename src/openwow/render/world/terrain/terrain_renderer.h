#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include <bgfx/bgfx.h>

#include "openwow/render/api/draw_encoder.h"
#include "openwow/render/api/draw_sort_depth.h"
#include "openwow/render/api/math/render_math_types.h"
#include "openwow/render/resources/textures/texture_manager.h"
#include "openwow/render/resources/textures/texture_slice_arrays.h"
#include "openwow/render/world/environment/spatial_point_light.h"
#include "openwow/render/world/environment/world_environment.h"
#include "openwow/render/world/terrain/terrain_lod.h"
#include "openwow/render/world/terrain/terrain_mesh.h"
#include "openwow/world/coordinates/frustum.h"
#include "openwow/world/terrain/terrain_lod_subdivision.h"

namespace openwow::render {

class ShadowRenderData;

struct TerrainChunkGpu {

  uint32_t vertex_start{0};
  uint32_t vertex_count{0};

  uint32_t batch_index_start{0};
  uint32_t batch_index_count{0};

  float bounds_min[3]{};
  float bounds_max[3]{};

  float world_x{0.0f};
  float world_y{0.0f};
  float world_z{0.0f};
  float bounds_radius{0.0f};

  int32_t tile_x{0};
  int32_t tile_y{0};
  uint32_t chunk_x{0};
  uint32_t chunk_y{0};

  int layer_count{0};
  bgfx::TextureHandle layer_tex[kMaxTerrainLayers]{BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
                                                   BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
  std::array<TextureLease, kMaxTerrainLayers> layer_texture_leases{};

  std::array<TextureSliceLease, kMaxTerrainLayers> layer_slice_leases{};

  bgfx::TextureHandle layer_array_tex = BGFX_INVALID_HANDLE;
  bool valid{false};
};

struct TerrainTileGpu {
  bgfx::VertexBufferHandle vb = BGFX_INVALID_HANDLE;

  bgfx::IndexBufferHandle index_buffer = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle alpha_atlas = BGFX_INVALID_HANDLE;
  std::array<TerrainChunkGpu, 256> chunks{};
  int32_t tile_x{0};
  int32_t tile_y{0};
  bool valid{false};
};

struct TerrainGpuHandleBudget {
  std::size_t vertex_buffers{0};
  std::size_t index_buffers{0};
  std::size_t alpha_textures{0};
  std::size_t chunk_metadata_entries{0};

  [[nodiscard]] constexpr std::size_t total_handles() const noexcept {
    return vertex_buffers + index_buffers + alpha_textures;
  }
};

struct PreparedTerrainMaterialTextures {
  std::vector<PreparedTextureUpload> uploads;
};

[[nodiscard]] PreparedTerrainMaterialTextures PrepareTerrainMaterialTextures(
    const PreparedTerrainTile &prepared,
    const std::function<std::vector<std::uint8_t>(const std::string &)> &loader);

using TerrainPointLight = SpatialPointLight;

class TerrainRenderer {
public:
  explicit TerrainRenderer(TextureManager &texture_manager) : texture_manager_(texture_manager) {}
  ~TerrainRenderer();

  TerrainRenderer(const TerrainRenderer &) = delete;
  TerrainRenderer &operator=(const TerrainRenderer &) = delete;

  bool Initialize();

  [[nodiscard]] static PreparedTerrainMaterialTextures PrepareMaterialTextures(
      const PreparedTerrainTile &prepared,
      const std::function<std::vector<std::uint8_t>(const std::string &)> &loader);

  void UploadPreparedAdt(const PreparedTerrainTile &prepared,
                         const PreparedTerrainMaterialTextures &materials, int32_t tile_x,
                         int32_t tile_y);

  void RemoveAdt(int32_t tile_x, int32_t tile_y);

  void Render(uint8_t view_id, const WorldEnvironmentSnapshot &environment,
              const DrawSortDepth &sort_depth, bgfx::Encoder *encoder = nullptr);

  void SetFrustum(const world::Frustum *frustum) {
    frustum_ = frustum;
  }

  void SetShadowRenderData(const ShadowRenderData *data) {
    shadow_data_ = data;
  }

  void Shutdown();

  void ClearTerrain();

  [[nodiscard]] std::size_t loaded_chunk_count() const {
    std::size_t count = 0;
    for (const auto &tile : loaded_tiles_) {
      for (const auto &chunk : tile.chunks) {
        count += chunk.valid ? 1u : 0u;
      }
    }
    return count;
  }
  [[nodiscard]] std::size_t loaded_tile_count() const {
    return loaded_tiles_.size();
  }

  static constexpr std::size_t kChunksPerTile = 256u;
  static constexpr std::uint16_t kAlphaAtlasChunksPerAxis = 16u;
  static constexpr std::uint16_t kAlphaAtlasSize =
      static_cast<std::uint16_t>(kAlphaMapSize * kAlphaAtlasChunksPerAxis);
  static_assert(kAlphaAtlasSize == kTerrainAlphaAtlasSize);

  [[nodiscard]] static constexpr TerrainGpuHandleBudget
  PersistentHandleBudget(const std::size_t tile_count) noexcept {
    return {
        .vertex_buffers = tile_count,
        .index_buffers = tile_count,
        .alpha_textures = tile_count,
        .chunk_metadata_entries = tile_count * kChunksPerTile,
    };
  }

  [[nodiscard]] static constexpr std::uint32_t DiffuseSamplerFlags() noexcept {
    return BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC;
  }

  void SetViewDistance(uint32_t distance);
  void SetTextureQuality(uint32_t quality);
  [[nodiscard]] uint32_t view_distance() const {
    return view_distance_;
  }
  [[nodiscard]] uint32_t texture_quality() const {
    return texture_quality_;
  }

  [[nodiscard]] uint32_t total_triangles() const;

  void Reset();

public:

  static constexpr std::size_t kMaxTerrainDrawPointLights = 3u;

private:
  static constexpr std::uint32_t kInvalidPointLightIndex = UINT32_MAX;

  enum class TerrainProgramKind : std::uint8_t {

    kSolid,

    kSplat,

    kSplatArray,
  };

  struct TerrainBatchKey {

    std::uint16_t layer_array_index{bgfx::kInvalidHandle};

    std::array<std::uint16_t, kMaxTerrainLayers> texture_indices{};
    std::array<std::uint32_t, kMaxTerrainDrawPointLights> point_light_indices{
        kInvalidPointLightIndex, kInvalidPointLightIndex, kInvalidPointLightIndex};
    std::uint32_t lod{0};
    std::uint8_t point_light_count{0};
    TerrainProgramKind program{TerrainProgramKind::kSolid};

    [[nodiscard]] bool operator==(const TerrainBatchKey &) const = default;
  };

  struct TerrainBatchItem {
    const TerrainChunkGpu *chunk{nullptr};
    TerrainBatchKey key{};
  };

  TextureManager &texture_manager_;

  void ResolveChunkLayerSlices(TerrainChunkGpu &chunk,
                               std::array<std::uint8_t, kMaxTerrainLayers> &out_slices) const;
  void DestroyTileGpu(TerrainTileGpu &gpu);
  [[nodiscard]] bool PipelineResourcesAreValid() const noexcept;
  void DestroyPipelineResources();

  [[nodiscard]] const world::Frustum *ResolveChunkCullFrustum() const;

  bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle splat_program_ = BGFX_INVALID_HANDLE;

  bgfx::ProgramHandle splat_array_program_ = BGFX_INVALID_HANDLE;

  bgfx::UniformHandle s_terrain_tex_[kMaxTerrainLayers]{BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
                                                        BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};

  bgfx::UniformHandle s_terrain_layers_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle s_terrain_alpha_ = BGFX_INVALID_HANDLE;

  bgfx::UniformHandle u_vs_params_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_fs_params_ = BGFX_INVALID_HANDLE;

  TextureSliceArrays slice_arrays_;

  const world::Frustum *frustum_{nullptr};
  const ShadowRenderData *shadow_data_{nullptr};

  bgfx::VertexLayout layout_{};
  std::vector<TerrainTileGpu> loaded_tiles_;
  std::vector<TerrainBatchItem> batch_items_;
  bool initialized_{false};
  uint32_t view_distance_{4};
  uint32_t texture_quality_{2};
};

}

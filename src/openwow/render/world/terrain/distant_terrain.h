#pragma once

#include "openwow/data/terrain/wdl_file.h"
#include "openwow/world/coordinates/frustum.h"
#include "openwow/render/api/draw_encoder.h"
#include "openwow/render/api/draw_sort_depth.h"
#include "openwow/render/api/math/render_math_types.h"
#include "openwow/render/world/environment/world_environment.h"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <vector>

namespace openwow::render {

struct DistantTerrainTileMesh {
  std::vector<RenderVec3> positions;

  std::vector<std::uint16_t> indices;

  std::vector<std::uint16_t> maho_indices;
};

struct DistantTerrainRenderStats {
  std::uint64_t environment_generation{0};
  std::uint32_t visible_tile_count{0};
  std::uint32_t culled_tile_count{0};
  std::uint32_t detailed_tile_count{0};
  std::uint32_t submitted_draw_count{0};
  std::uint32_t submitted_index_count{0};
};

[[nodiscard]] DistantTerrainTileMesh
BuildDistantTerrainTileMesh(int storage_x, int storage_y,
                            const openwow::data::terrain::WdlTileHeights &heights,
                            const openwow::data::terrain::WdlTileHoles *holes = nullptr);

class DistantTerrainRenderer {
public:
  DistantTerrainRenderer() = default;
  ~DistantTerrainRenderer();

  DistantTerrainRenderer(const DistantTerrainRenderer &) = delete;
  DistantTerrainRenderer &operator=(const DistantTerrainRenderer &) = delete;

  bool Initialize();

  void Shutdown();

  void LoadWdl(const openwow::data::terrain::WdlFile &wdl);

  void SetDetailedTile(int world_tile_x, int world_tile_y, bool detailed);

  void SetFrustum(const world::Frustum *frustum) {
    frustum_ = frustum;
  }

  void Render(bgfx::ViewId view, const WorldEnvironmentSnapshot& environment,
              const DrawSortDepth& sort_depth, bgfx::Encoder *encoder = nullptr);

  void Clear();

  [[nodiscard]] bool initialized() const {
    return initialized_;
  }
  [[nodiscard]] const DistantTerrainRenderStats& render_stats() const noexcept {
    return render_stats_;
  }

private:
  struct DistantTileMesh {
    std::uint32_t index_start = 0;
    std::uint32_t index_count = 0;
    std::uint32_t maho_index_start = 0;
    std::uint32_t maho_index_count = 0;
    RenderVec3 bounds_min{};
    RenderVec3 bounds_max{};
    bool valid = false;
  };

  struct Vertex {
    float x, y, z;
    static bgfx::VertexLayout layout;
    static void InitLayout();
  };

  static constexpr int kTilesPerAxis = 64;

  std::array<std::array<DistantTileMesh, kTilesPerAxis>, kTilesPerAxis> tiles_{};

  std::array<std::uint64_t, kTilesPerAxis> detailed_tile_rows_{};
  const world::Frustum *frustum_{nullptr};

  bgfx::VertexBufferHandle vertex_buffer_ = BGFX_INVALID_HANDLE;
  bgfx::IndexBufferHandle index_buffer_ = BGFX_INVALID_HANDLE;
  bgfx::IndexBufferHandle maho_index_buffer_ = BGFX_INVALID_HANDLE;

  bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;

  bgfx::UniformHandle u_fs_params_ = BGFX_INVALID_HANDLE;

  DistantTerrainRenderStats render_stats_{};
  bool initialized_{false};
};

}

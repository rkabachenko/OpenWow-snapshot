
#include "openwow/render/world/terrain/distant_terrain.h"
#include "openwow/render/resources/shaders/shader_registry.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace openwow::render {

namespace {

constexpr float kRetailMapHalfSize = 17066.666f;
constexpr float kRetailChunkSize = 33.333332f;
constexpr float kRetailHalfChunkSize = 16.666666f;
constexpr std::uint16_t kWdlOuterVertexCount = 17u * 17u;

}

namespace distant_terrain_fs_param {

inline constexpr std::size_t kFogColor = 0u;
inline constexpr std::size_t kFogParams = 1u;
inline constexpr std::size_t kFrameConstantCount = 2u;
inline constexpr std::size_t kCount = 2u;

}

static_assert(distant_terrain_fs_param::kFrameConstantCount ==
              distant_terrain_fs_param::kCount);

DistantTerrainTileMesh
BuildDistantTerrainTileMesh(const int storage_x, const int storage_y,
                            const openwow::data::terrain::WdlTileHeights &heights,
                            const openwow::data::terrain::WdlTileHoles *holes) {
  DistantTerrainTileMesh mesh;
  mesh.positions.reserve(17u * 17u + 16u * 16u);
  mesh.indices.reserve(16u * 16u * 12u);
  mesh.maho_indices.reserve(16u * 16u * 12u);

  const float origin_x = kRetailMapHalfSize - static_cast<float>(storage_y * 16) * kRetailChunkSize;
  const float origin_y = kRetailMapHalfSize - static_cast<float>(storage_x * 16) * kRetailChunkSize;

  for (std::size_t y = 0; y < 17u; ++y) {
    for (std::size_t x = 0; x < 17u; ++x) {
      mesh.positions.push_back({
          origin_x - static_cast<float>(y) * kRetailChunkSize,
          origin_y - static_cast<float>(x) * kRetailChunkSize,
          static_cast<float>(heights.outer[y][x]),
      });
    }
  }
  for (std::size_t y = 0; y < 16u; ++y) {
    for (std::size_t x = 0; x < 16u; ++x) {
      mesh.positions.push_back({
          origin_x - kRetailHalfChunkSize - static_cast<float>(y) * kRetailChunkSize,
          origin_y - kRetailHalfChunkSize - static_cast<float>(x) * kRetailChunkSize,
          static_cast<float>(heights.inner[y][x]),
      });
    }
  }

  for (std::uint16_t y = 0u; y < 16u; ++y) {
    const std::uint16_t hole_mask =
        holes == nullptr ? 0u : holes->rows[static_cast<std::size_t>(y)];
    for (std::uint16_t x = 0u; x < 16u; ++x) {
      const auto top_left = static_cast<std::uint16_t>(y * 17u + x);
      const auto top_right = static_cast<std::uint16_t>(top_left + 1u);
      const auto bottom_left = static_cast<std::uint16_t>(top_left + 17u);
      const auto bottom_right = static_cast<std::uint16_t>(top_left + 18u);
      const auto center = static_cast<std::uint16_t>(kWdlOuterVertexCount + y * 16u + x);
      const std::array<std::uint16_t, 12> fan{
          center, top_right,   top_left,     center, bottom_right, top_right,
          center, bottom_left, bottom_right, center, top_left,     bottom_left,
      };
      auto &target = (hole_mask & static_cast<std::uint16_t>(1u << x)) != 0u ? mesh.maho_indices
                                                                             : mesh.indices;
      target.insert(target.end(), fan.begin(), fan.end());
    }
  }

  return mesh;
}

bgfx::VertexLayout DistantTerrainRenderer::Vertex::layout;

void DistantTerrainRenderer::Vertex::InitLayout() {
  static bool initialized = false;
  if (initialized)
    return;
  initialized = true;
  layout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).end();
}

DistantTerrainRenderer::~DistantTerrainRenderer() {
  Shutdown();
}

bool DistantTerrainRenderer::Initialize() {
  if (initialized_)
    return true;
  Vertex::InitLayout();

  program_ = CreateEmbeddedProgram(ShaderProgramId::DistantTerrain, bgfx::getRendererType());
  u_fs_params_ =
      bgfx::createUniform("u_distantTerrainFsParams", bgfx::UniformType::Vec4,
                          static_cast<std::uint16_t>(distant_terrain_fs_param::kCount));
  if (!bgfx::isValid(program_) || !bgfx::isValid(u_fs_params_)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "DistantTerrainRenderer: shader load failed");
    if (bgfx::isValid(program_)) {
      bgfx::destroy(program_);
    }
    if (bgfx::isValid(u_fs_params_)) {
      bgfx::destroy(u_fs_params_);
    }
    program_ = BGFX_INVALID_HANDLE;
    u_fs_params_ = BGFX_INVALID_HANDLE;
    return false;
  }

  initialized_ = true;
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo, "DistantTerrainRenderer: initialized");
  return true;
}

void DistantTerrainRenderer::Shutdown() {
  if (!initialized_)
    return;
  Clear();
  if (bgfx::isValid(program_)) {
    bgfx::destroy(program_);
  }
  if (bgfx::isValid(u_fs_params_)) {
    bgfx::destroy(u_fs_params_);
  }
  program_ = BGFX_INVALID_HANDLE;
  u_fs_params_ = BGFX_INVALID_HANDLE;
  initialized_ = false;
}

void DistantTerrainRenderer::LoadWdl(const openwow::data::terrain::WdlFile &wdl) {
  Clear();

  const auto tile_count = static_cast<std::size_t>(wdl.CountTilesWithData());
  std::vector<RenderVec3> positions;
  std::vector<std::uint32_t> indices;
  std::vector<std::uint32_t> maho_indices;
  positions.reserve(tile_count * (17u * 17u + 16u * 16u));
  indices.reserve(tile_count * 16u * 16u * 12u);
  maho_indices.reserve(tile_count * 16u * 16u * 12u);

  int count = 0;
  for (int ty = 0; ty < 64; ++ty) {
    for (int tx = 0; tx < 64; ++tx) {
      const auto *heights = wdl.tile_heights[ty][tx];
      if (heights == nullptr) {
        continue;
      }

      const auto tile = BuildDistantTerrainTileMesh(tx, ty, *heights, wdl.tile_holes[ty][tx]);
      if (tile.positions.empty() || (tile.indices.empty() && tile.maho_indices.empty())) {
        continue;
      }

      auto &draw = tiles_[ty][tx];
      const auto base_vertex = static_cast<std::uint32_t>(positions.size());
      draw.index_start = static_cast<std::uint32_t>(indices.size());
      draw.index_count = static_cast<std::uint32_t>(tile.indices.size());
      draw.maho_index_start = static_cast<std::uint32_t>(maho_indices.size());
      draw.maho_index_count = static_cast<std::uint32_t>(tile.maho_indices.size());

      draw.bounds_min.fill(std::numeric_limits<float>::max());
      draw.bounds_max.fill(std::numeric_limits<float>::lowest());
      for (const auto &position : tile.positions) {
        for (std::size_t axis = 0; axis < position.size(); ++axis) {
          draw.bounds_min[axis] = std::min(draw.bounds_min[axis], position[axis]);
          draw.bounds_max[axis] = std::max(draw.bounds_max[axis], position[axis]);
        }
      }

      positions.insert(positions.end(), tile.positions.begin(), tile.positions.end());
      for (const auto index : tile.indices) {
        indices.push_back(base_vertex + index);
      }
      for (const auto index : tile.maho_indices) {
        maho_indices.push_back(base_vertex + index);
      }
      draw.valid = true;
      ++count;
    }
  }

  if (positions.empty() || (indices.empty() && maho_indices.empty())) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                       "DistantTerrainRenderer: WDL contains no terrain meshes");
    return;
  }

  const bgfx::Memory *vertex_memory = bgfx::copy(
      positions.data(), static_cast<std::uint32_t>(positions.size() * sizeof(RenderVec3)));
  static_assert(sizeof(RenderVec3) == sizeof(Vertex));
  vertex_buffer_ = bgfx::createVertexBuffer(vertex_memory, Vertex::layout);

  if (!indices.empty()) {
    const bgfx::Memory *index_memory = bgfx::copy(
        indices.data(), static_cast<std::uint32_t>(indices.size() * sizeof(std::uint32_t)));
    index_buffer_ = bgfx::createIndexBuffer(index_memory, BGFX_BUFFER_INDEX32);
  }
  if (!maho_indices.empty()) {
    const bgfx::Memory *maho_index_memory =
        bgfx::copy(maho_indices.data(),
                   static_cast<std::uint32_t>(maho_indices.size() * sizeof(std::uint32_t)));
    maho_index_buffer_ = bgfx::createIndexBuffer(maho_index_memory, BGFX_BUFFER_INDEX32);
  }

  const bool buffers_valid = bgfx::isValid(vertex_buffer_) &&
                             (indices.empty() || bgfx::isValid(index_buffer_)) &&
                             (maho_indices.empty() || bgfx::isValid(maho_index_buffer_));
  if (!buffers_valid) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "DistantTerrainRenderer: aggregate buffer creation failed");
    Clear();
    return;
  }

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "DistantTerrainRenderer: built " + std::to_string(count) +
                         " distant tile meshes in three shared buffers");
}

void DistantTerrainRenderer::SetDetailedTile(const int world_tile_x,
                                             const int world_tile_y,
                                             const bool detailed) {
  if (world_tile_x < 0 || world_tile_x >= kTilesPerAxis || world_tile_y < 0 ||
      world_tile_y >= kTilesPerAxis) {
    return;
  }
  const std::uint64_t bit = std::uint64_t{1} << world_tile_y;
  auto &row = detailed_tile_rows_[static_cast<std::size_t>(world_tile_x)];
  if (detailed) {
    row |= bit;
  } else {
    row &= ~bit;
  }
}

void DistantTerrainRenderer::Render(bgfx::ViewId view,
                                    const WorldEnvironmentSnapshot& environment,
                                    const DrawSortDepth& sort_depth,
                                    bgfx::Encoder *const encoder) {
  render_stats_ = {};
  render_stats_.environment_generation = environment.generation;
  if (!initialized_ || !bgfx::isValid(program_) || !bgfx::isValid(vertex_buffer_)) {
    return;
  }

  const DrawEncoder draw{encoder};

  const std::uint64_t base_state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                                   BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA;

  std::array<RenderVec4, distant_terrain_fs_param::kCount> fs_params{};
  fs_params[distant_terrain_fs_param::kFogColor] = environment.fog.color;
  fs_params[distant_terrain_fs_param::kFogParams] = environment.fog.params;

  const auto submit_range = [&](const bgfx::IndexBufferHandle index_buffer,
                                const std::uint32_t start, const std::uint32_t count,
                                const std::uint64_t state, const std::uint32_t depth) {
    if (count == 0u || !bgfx::isValid(index_buffer)) {
      return;
    }

    draw.setVertexBuffer(0, vertex_buffer_);
    draw.setIndexBuffer(index_buffer, start, count);
    draw.setUniform(u_fs_params_, fs_params.data(),
                    static_cast<std::uint16_t>(distant_terrain_fs_param::kCount));
    draw.setState(state);
    draw.submit(view, program_, depth);
    ++render_stats_.submitted_draw_count;
    render_stats_.submitted_index_count += count;
  };

  for (int ty = 0; ty < kTilesPerAxis; ++ty) {

    const std::uint64_t detailed_row = detailed_tile_rows_[static_cast<std::size_t>(ty)];
    for (int tx = 0; tx < kTilesPerAxis; ++tx) {
      const auto &mesh = tiles_[ty][tx];
      if (!mesh.valid)
        continue;

      if (((detailed_row >> tx) & std::uint64_t{1}) != 0u) {
        ++render_stats_.detailed_tile_count;
        continue;
      }

      if (frustum_ != nullptr &&
          !frustum_->TestAABB(mesh.bounds_min[0], mesh.bounds_min[1], mesh.bounds_min[2],
                              mesh.bounds_max[0], mesh.bounds_max[1], mesh.bounds_max[2])) {
        ++render_stats_.culled_tile_count;
        continue;
      }

      ++render_stats_.visible_tile_count;

      const std::uint32_t tile_depth =
          sort_depth.ForAabb(RenderVec3View{mesh.bounds_min}, RenderVec3View{mesh.bounds_max});

      submit_range(index_buffer_, mesh.index_start, mesh.index_count, base_state, tile_depth);
      submit_range(maho_index_buffer_, mesh.maho_index_start, mesh.maho_index_count,
                   base_state | BGFX_STATE_CULL_CW, tile_depth);
    }
  }
}

void DistantTerrainRenderer::Clear() {
  if (bgfx::isValid(vertex_buffer_))
    bgfx::destroy(vertex_buffer_);
  if (bgfx::isValid(index_buffer_))
    bgfx::destroy(index_buffer_);
  if (bgfx::isValid(maho_index_buffer_))
    bgfx::destroy(maho_index_buffer_);
  vertex_buffer_ = BGFX_INVALID_HANDLE;
  index_buffer_ = BGFX_INVALID_HANDLE;
  maho_index_buffer_ = BGFX_INVALID_HANDLE;
  tiles_ = {};
  detailed_tile_rows_ = {};
  frustum_ = nullptr;
  render_stats_ = {};
}

}

#include "openwow/render/world/detail_doodads/detail_doodad_renderer.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/model/m2_model.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/render/api/draw_encoder.h"
#include "openwow/render/backend/bgfx/bgfx_texture_lease.h"
#include "openwow/render/m2/m2_skin_geometry.h"
#include "openwow/render/m2/m2_skin_profile.h"
#include "openwow/render/m2/m2_texture_unit_preparation.h"
#include "openwow/render/resources/shaders/shader_registry.h"
#include "openwow/render/resources/textures/texture_manager.h"
#include "openwow/render/world/environment/world_model_lighting.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/world/coordinates/frustum.h"
#include "openwow/world/terrain/ground_effect_map.h"
#include "openwow/world/world_render_pipeline.h"

#include <algorithm>
#include <array>
#include <bgfx/bgfx.h>
#include <chrono>
#include <cmath>
#include <exception>
#include <future>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace openwow::render {
namespace {

constexpr std::uint32_t kRetailDefaultEffectDensity = 8u;
constexpr std::uint32_t kMaximumEffectInstancesPerCell = 64u;
constexpr std::uint32_t kMaximumGeneratedChunksPerFrame = 8u;
constexpr std::uint32_t kMaximumPublishedChunksPerFrame = 4u;
constexpr std::size_t kMaximumConcurrentModelPreparations = 4u;
constexpr std::uint32_t kMaximumGroundEffectDensity = 256u;
constexpr std::size_t kMaximumChunkVertexCount =
    static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) + 1u;
constexpr float kRetailDetailFadeStartRatio = 0.85f;
constexpr float kMinimumTerrainNormalZ = 0.4f;
constexpr float kMinimumScale = 0.67f;
constexpr float kScaleRange = 0.66f;
constexpr float kTwoPi = 6.28318530717958647692f;
constexpr std::string_view kDetailDoodadModelPrefix = "World/NoDXT/Detail/";

std::uint64_t MakeTileKey(const std::int32_t tile_x,
                          const std::int32_t tile_y) noexcept {
  return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(tile_x)) <<
          32u) |
         static_cast<std::uint32_t>(tile_y);
}

std::string NormalizeAssetPath(std::string path) {
  std::replace(path.begin(), path.end(), '\\', '/');
  while (!path.empty() &&
         (path.front() == '/' || path.back() == '\0' || path.back() == ' ')) {
    if (path.front() == '/') {
      path.erase(path.begin());
    } else {
      path.pop_back();
    }
  }
  return path;
}

std::string BuildDetailDoodadModelPath(const std::string_view dbc_path) {
  std::string path(kDetailDoodadModelPrefix);
  path.append(dbc_path);
  path = NormalizeAssetPath(std::move(path));
  const std::size_t slash = path.find_last_of('/');
  const std::size_t dot = path.find_last_of('.');
  if (dot == std::string::npos ||
      (slash != std::string::npos && dot < slash)) {
    return {};
  }
  path.resize(dot);
  path += ".m2";
  return path;
}

class DetailDoodadRandom {
 public:
  explicit DetailDoodadRandom(std::uint32_t seed)
      : state_(seed != 0u ? seed : 0x335a1234u) {}

  std::uint32_t Next() noexcept {
    state_ ^= state_ << 13u;
    state_ ^= state_ >> 17u;
    state_ ^= state_ << 5u;
    return state_;
  }

  float Unit() noexcept {
    return static_cast<float>(Next() >> 8u) * (1.0f / 16777216.0f);
  }

 private:
  std::uint32_t state_;
};

struct DetailModelVertex {
  std::array<float, 3> position{};
  std::array<float, 2> texcoord{};
};

struct PreparedDetailModelBatch {
  std::uint32_t first_index{};
  std::uint32_t index_count{};
  std::string texture_path;
  std::uint32_t sampler_flags{};
};

struct PreparedDetailModel {
  std::uint32_t doodad_id{};
  std::string model_path;
  std::vector<DetailModelVertex> vertices;
  std::vector<std::uint16_t> indices;
  std::vector<PreparedDetailModelBatch> batches;
  std::string warning;
  std::string error;
};

PreparedDetailModel PrepareDetailModel(
    const std::uint32_t doodad_id, const std::string& model_path,
    const DetailDoodadRenderer::LoadFileCallback& loader) {
  PreparedDetailModel prepared{.doodad_id = doodad_id,
                               .model_path = model_path};
  if (!loader) {
    prepared.error = "file loader is unavailable";
    return prepared;
  }
  const auto model_bytes = loader(model_path);
  if (model_bytes.empty()) {
    prepared.error = "model file is missing";
    return prepared;
  }
  auto model_result = data::model::LoadM2FromBytes(model_bytes);
  if (!model_result.ok) {
    prepared.error = "M2 parse failed: " + model_result.error;
    return prepared;
  }
  auto& model = model_result.model;
  const auto skin_profile = m2::SelectM2SkinProfile(model);
  if (!skin_profile.has_value()) {
    prepared.error = "M2 skin profile is unavailable";
    return prepared;
  }
  const std::string skin_path =
      m2::BuildM2SkinProfilePath(model_path, *skin_profile);
  const auto skin_bytes = loader(skin_path);
  if (skin_bytes.empty()) {
    prepared.error = "skin file is missing: " + skin_path;
    return prepared;
  }
  auto skin_result = data::model::LoadSkinFromBytes(skin_bytes);
  if (!skin_result.ok) {
    prepared.error = "skin parse failed: " + skin_result.error;
    return prepared;
  }

  const auto texture_unit_preparation =
      m2::PrepareM2SkinTextureUnitsForRender(model, skin_result.skin);
  if (texture_unit_preparation.HasInvalidRenderInputs()) {
    prepared.warning =
        "skin contains invalid texture-unit inputs; valid diffuse batches "
        "will continue";
  }

  m2::M2SkinGeometry geometry;
  if (!m2::BuildM2SkinGeometry(model, skin_result.skin, &geometry)) {
    prepared.error = "detail geometry preparation failed: " + geometry.error;
    return prepared;
  }
  prepared.vertices.reserve(geometry.vertices.size());
  for (const auto& source : geometry.vertices) {
    prepared.vertices.push_back({
        .position = {source.position[0], source.position[1],
                     source.position[2]},
        .texcoord = {source.texcoord0[0], source.texcoord0[1]},
    });
  }
  prepared.indices = std::move(geometry.indices);

  for (const auto& texture_unit : geometry.normalized_skin.texture_units) {
    const auto shader =
        m2::ResolveM2SkinTextureUnitShader(model, texture_unit);
    if (!shader.valid || !shader.draws ||
        texture_unit.submesh_index >=
            geometry.normalized_skin.submeshes.size()) {
      continue;
    }
    const auto& submesh =
        geometry.normalized_skin.submeshes[texture_unit.submesh_index];
    if (submesh.index_count == 0u ||
        submesh.index_start > prepared.indices.size() ||
        submesh.index_count > prepared.indices.size() - submesh.index_start) {
      continue;
    }
    const auto combos =
        m2::ResolveM2SkinTextureUnitCombos(model, texture_unit);
    if (combos.primary_texture_index >= model.textures.size()) {
      continue;
    }
    const auto& texture = model.textures[combos.primary_texture_index];
    if (texture.type != 0u || texture.name_text.empty()) {
      continue;
    }
    std::uint32_t sampler_flags = 0u;
    if ((texture.flags & 0x1u) != 0u) {
      sampler_flags |= BGFX_SAMPLER_U_CLAMP;
    }
    if ((texture.flags & 0x2u) != 0u) {
      sampler_flags |= BGFX_SAMPLER_V_CLAMP;
    }
    prepared.batches.push_back({
        .first_index = submesh.index_start,
        .index_count = submesh.index_count,
        .texture_path = NormalizeAssetPath(texture.name_text),
        .sampler_flags = sampler_flags,
    });
  }
  if (prepared.vertices.empty() || prepared.indices.empty() ||
      prepared.batches.empty()) {
    prepared.error = "detail model has no drawable diffuse geometry";
  }
  return prepared;
}

struct TerrainSurfaceSample {
  RenderVec3 position{};
  RenderVec3 normal{};
  std::array<float, 3> barycentric{};
  std::array<std::size_t, 3> vertex_indices{};
};

constexpr std::size_t OuterVertexIndex(const std::size_t row,
                                       const std::size_t column) noexcept {
  return row * 17u + column;
}

constexpr std::size_t InnerVertexIndex(const std::size_t row,
                                       const std::size_t column) noexcept {
  return row * 17u + 9u + column;
}

RenderVec3 TerrainVertexPosition(const data::terrain::TerrainChunk& chunk,
                                 const std::size_t vertex_index,
                                 const std::size_t cell_row,
                                 const std::size_t cell_column) {
  const bool inner = vertex_index == InnerVertexIndex(cell_row, cell_column);
  std::size_t row = 0u;
  std::size_t column = 0u;
  if (inner) {
    row = cell_row;
    column = cell_column;
  } else {
    row = vertex_index / 17u;
    column = vertex_index % 17u;
  }
  const float row_offset =
      static_cast<float>(row) + (inner ? 0.5f : 0.0f);
  const float column_offset =
      static_cast<float>(column) + (inner ? 0.5f : 0.0f);
  return {chunk.header.position_x - row_offset * data::terrain::kUnitSize,
          chunk.header.position_y - column_offset * data::terrain::kUnitSize,
          chunk.header.position_z + chunk.heights[vertex_index]};
}

std::optional<TerrainSurfaceSample> SampleTerrainCell(
    const data::terrain::TerrainChunk& chunk, const std::size_t cell_row,
    const std::size_t cell_column, const float row_fraction,
    const float column_fraction) {
  const std::size_t top_left = OuterVertexIndex(cell_row, cell_column);
  const std::size_t top_right = OuterVertexIndex(cell_row, cell_column + 1u);
  const std::size_t bottom_left =
      OuterVertexIndex(cell_row + 1u, cell_column);
  const std::size_t bottom_right =
      OuterVertexIndex(cell_row + 1u, cell_column + 1u);
  const std::size_t center = InnerVertexIndex(cell_row, cell_column);
  const std::array<std::array<std::size_t, 3>, 4> triangles{{
      {bottom_left, center, top_left},
      {center, top_right, top_left},
      {center, bottom_left, bottom_right},
      {center, bottom_right, top_right},
  }};
  const float world_x =
      chunk.header.position_x -
      (static_cast<float>(cell_row) + row_fraction) *
          data::terrain::kUnitSize;
  const float world_y =
      chunk.header.position_y -
      (static_cast<float>(cell_column) + column_fraction) *
          data::terrain::kUnitSize;

  for (const auto& triangle : triangles) {
    const RenderVec3 a = TerrainVertexPosition(
        chunk, triangle[0], cell_row, cell_column);
    const RenderVec3 b = TerrainVertexPosition(
        chunk, triangle[1], cell_row, cell_column);
    const RenderVec3 c = TerrainVertexPosition(
        chunk, triangle[2], cell_row, cell_column);
    const float denominator =
        (b[1] - c[1]) * (a[0] - c[0]) +
        (c[0] - b[0]) * (a[1] - c[1]);
    if (std::abs(denominator) <= 1.0e-8f) {
      continue;
    }
    const float first =
        ((b[1] - c[1]) * (world_x - c[0]) +
         (c[0] - b[0]) * (world_y - c[1])) /
        denominator;
    const float second =
        ((c[1] - a[1]) * (world_x - c[0]) +
         (a[0] - c[0]) * (world_y - c[1])) /
        denominator;
    const float third = 1.0f - first - second;
    constexpr float kBarycentricEpsilon = -0.0001f;
    if (first < kBarycentricEpsilon || second < kBarycentricEpsilon ||
        third < kBarycentricEpsilon) {
      continue;
    }

    const RenderVec3 edge_a{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const RenderVec3 edge_b{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    RenderVec3 normal{
        edge_a[1] * edge_b[2] - edge_a[2] * edge_b[1],
        edge_a[2] * edge_b[0] - edge_a[0] * edge_b[2],
        edge_a[0] * edge_b[1] - edge_a[1] * edge_b[0],
    };
    if (normal[2] < 0.0f) {
      normal[0] = -normal[0];
      normal[1] = -normal[1];
      normal[2] = -normal[2];
    }
    const float length = std::sqrt(normal[0] * normal[0] +
                                   normal[1] * normal[1] +
                                   normal[2] * normal[2]);
    if (!(length > 1.0e-8f)) {
      return std::nullopt;
    }
    for (float& component : normal) {
      component /= length;
    }
    return TerrainSurfaceSample{
        .position = {world_x, world_y,
                     a[2] * first + b[2] * second + c[2] * third},
        .normal = normal,
        .barycentric = {first, second, third},
        .vertex_indices = triangle,
    };
  }
  return std::nullopt;
}

std::uint8_t InterpolateVertexColorChannel(
    const data::terrain::TerrainChunk& chunk,
    const TerrainSurfaceSample& sample, const std::size_t channel) {
  float value = 0.0f;
  for (std::size_t vertex = 0u; vertex < 3u; ++vertex) {
    const auto& color = chunk.vertex_colors[sample.vertex_indices[vertex]];
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&color);
    value += static_cast<float>(bytes[channel]) *
             sample.barycentric[vertex];
  }
  return static_cast<std::uint8_t>(
      std::clamp(std::lround(value), 0l, 255l));
}

std::uint8_t SampleTerrainShadowVisibility(
    const data::terrain::TerrainChunk& chunk, const std::size_t cell_row,
    const std::size_t cell_column, const float row_fraction,
    const float column_fraction) {
  constexpr std::size_t kShadowSide = 64u;
  constexpr std::size_t kShadowBytesPerRow = kShadowSide / 8u;
  if (chunk.shadow_map.size() < kShadowSide * kShadowBytesPerRow) {
    return 255u;
  }
  const std::size_t row = std::min<std::size_t>(
      static_cast<std::size_t>((static_cast<float>(cell_row) + row_fraction) *
                               8.0f),
      kShadowSide - 1u);
  const std::size_t column = std::min<std::size_t>(
      static_cast<std::size_t>(
          (static_cast<float>(cell_column) + column_fraction) * 8.0f),
      kShadowSide - 1u);
  return (chunk.shadow_map[row * kShadowBytesPerRow + column / 8u] &
          static_cast<std::uint8_t>(1u << (column & 7u))) != 0u
             ? 0u
             : 255u;
}

float SquaredHorizontalDistanceToBounds(const RenderVec3& point,
                                        const RenderAabb& bounds) noexcept {
  const float dx = point[0] < bounds[0]
                       ? bounds[0] - point[0]
                       : (point[0] > bounds[3] ? point[0] - bounds[3] : 0.0f);
  const float dy = point[1] < bounds[1]
                       ? bounds[1] - point[1]
                       : (point[1] > bounds[4] ? point[1] - bounds[4] : 0.0f);
  return dx * dx + dy * dy;
}

}

struct DetailDoodadRenderer::Impl {
  struct DetailInstance {
    std::uint32_t doodad_id{};
    RenderVec3 position{};
    RenderVec3 terrain_normal{};
    float yaw{};
    float scale{1.0f};
    std::uint32_t color{0xffffffffu};
  };

  struct GpuDraw {
    std::uint32_t first_index{};
    std::uint32_t index_count{};
    std::string texture_path;
    std::uint32_t sampler_flags{};
    TextureLease texture;
  };

  struct ChunkState {
    bool generated{};
    bool gpu_ready{};
    std::vector<DetailInstance> instances;
    RenderAabb bounds{};
    bgfx::VertexBufferHandle vertices = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle indices = BGFX_INVALID_HANDLE;
    std::vector<GpuDraw> draws;
  };

  struct TileState {
    std::shared_ptr<const data::terrain::AdtFile> adt;
    std::int32_t tile_x{};
    std::int32_t tile_y{};
    std::array<ChunkState, data::terrain::kTotalChunks> chunks{};
  };

  enum class ModelState : std::uint8_t { kQueued, kLoading, kReady, kFailed };

  struct ModelResource {
    std::uint32_t doodad_id{};
    std::string model_path;
    ModelState state{ModelState::kQueued};
    std::future<PreparedDetailModel> future;
    std::vector<DetailModelVertex> vertices;
    std::vector<std::uint16_t> indices;
    std::vector<PreparedDetailModelBatch> batches;
  };

  struct Vertex {
    RenderVec3 position{};
    RenderVec3 terrain_normal{};
    std::array<float, 2> texcoord{};
    std::uint32_t color{};
  };
  static_assert(sizeof(Vertex) == 36u);

  explicit Impl(TextureManager& texture_manager)
      : texture_manager(texture_manager) {}

  void DestroyChunkGpu(ChunkState& chunk) {
    if (bgfx::isValid(chunk.vertices)) {
      bgfx::destroy(chunk.vertices);
    }
    if (bgfx::isValid(chunk.indices)) {
      bgfx::destroy(chunk.indices);
    }
    chunk.vertices = BGFX_INVALID_HANDLE;
    chunk.indices = BGFX_INVALID_HANDLE;
    chunk.draws.clear();
    chunk.gpu_ready = false;
  }

  void ResetGeneratedChunks() {
    for (auto& [_, tile] : tiles) {
      for (auto& chunk : tile.chunks) {
        DestroyChunkGpu(chunk);
        chunk.instances.clear();
        chunk.generated = false;
        chunk.bounds = {};
      }
    }
  }

  void DestroyTile(TileState& tile) {
    for (auto& chunk : tile.chunks) {
      DestroyChunkGpu(chunk);
    }
  }

  void Clear() {
    for (auto& [_, tile] : tiles) {
      DestroyTile(tile);
    }
    tiles.clear();
    for (auto& [_, model] : models) {
      if (model.state == ModelState::kLoading && model.future.valid()) {
        retired_preparations.push_back(std::move(model.future));
      }
    }
    models.clear();
    missing_effect_rows.clear();
    missing_doodad_rows.clear();
    invalid_doodad_paths.clear();
  }

  void DrainRetiredPreparations(const bool wait) {
    auto future = retired_preparations.begin();
    while (future != retired_preparations.end()) {
      if (!wait && future->wait_for(std::chrono::seconds(0)) !=
                       std::future_status::ready) {
        ++future;
        continue;
      }
      try {
        auto prepared = future->get();
        if (!prepared.error.empty()) {
          diagnostics::Log(
              diagnostics::LogLevel::kWarn,
              "DetailDoodadRenderer: retired model preparation failed id=" +
                  std::to_string(prepared.doodad_id) + " path=" +
                  prepared.model_path + " reason=" + prepared.error);
        }
      } catch (const std::exception& exception) {
        diagnostics::Log(
            diagnostics::LogLevel::kWarn,
            "DetailDoodadRenderer: retired model preparation failed reason=" +
                std::string(exception.what()));
      } catch (...) {
        diagnostics::Log(
            diagnostics::LogLevel::kWarn,
            "DetailDoodadRenderer: retired model preparation failed "
            "reason=unknown exception");
      }
      future = retired_preparations.erase(future);
    }
  }

  ModelResource* QueueModel(const std::uint32_t doodad_id) {
    if (auto found = models.find(doodad_id); found != models.end()) {
      return &found->second;
    }
    if (dbc == nullptr) {
      return nullptr;
    }
    const auto* row = dbc->ground_effect_doodad().LookupEntry(doodad_id);
    if (row == nullptr) {
      if (missing_doodad_rows.insert(doodad_id).second) {
        diagnostics::Log(
            diagnostics::LogLevel::kWarn,
            "DetailDoodadRenderer: GroundEffectDoodad row is missing id=" +
                std::to_string(doodad_id));
      }
      return nullptr;
    }
    const std::string model_path =
        BuildDetailDoodadModelPath(row->doodad_path);
    if (model_path.empty()) {
      if (invalid_doodad_paths.insert(doodad_id).second) {
        diagnostics::Log(
            diagnostics::LogLevel::kWarn,
            "DetailDoodadRenderer: invalid GroundEffectDoodad model path id=" +
                std::to_string(doodad_id) + " path=" +
                std::string(row->doodad_path));
      }
      return nullptr;
    }
    auto [inserted, _] = models.emplace(
        doodad_id, ModelResource{.doodad_id = doodad_id,
                                 .model_path = model_path});
    return &inserted->second;
  }

  void StartQueuedModels() {
    std::size_t active = retired_preparations.size();
    for (const auto& [_, model] : models) {
      active += model.state == ModelState::kLoading ? 1u : 0u;
    }
    for (auto& [_, model] : models) {
      if (active >= kMaximumConcurrentModelPreparations) {
        break;
      }
      if (model.state != ModelState::kQueued) {
        continue;
      }
      const auto loader = load_file;
      const auto doodad_id = model.doodad_id;
      const auto path = model.model_path;
      try {
        model.future = std::async(
            std::launch::async,
            [loader, doodad_id, path] {
              return PrepareDetailModel(doodad_id, path, loader);
            });
        model.state = ModelState::kLoading;
        ++active;
      } catch (const std::exception& exception) {
        model.state = ModelState::kFailed;
        diagnostics::Log(
            diagnostics::LogLevel::kWarn,
            "DetailDoodadRenderer: model preparation dispatch failed id=" +
                std::to_string(doodad_id) + " path=" + path +
                " reason=" + exception.what());
      }
    }
  }

  void PumpPreparedModels() {
    DrainRetiredPreparations(false);
    for (auto& [_, model] : models) {
      if (model.state != ModelState::kLoading || !model.future.valid() ||
          model.future.wait_for(std::chrono::seconds(0)) !=
              std::future_status::ready) {
        continue;
      }
      try {
        auto prepared = model.future.get();
        if (!prepared.error.empty()) {
          model.state = ModelState::kFailed;
          diagnostics::Log(
              diagnostics::LogLevel::kWarn,
              "DetailDoodadRenderer: model preparation failed id=" +
                  std::to_string(model.doodad_id) + " path=" +
                  model.model_path + " reason=" + prepared.error);
          continue;
        }
        if (!prepared.warning.empty()) {
          diagnostics::Log(
              diagnostics::LogLevel::kWarn,
              "DetailDoodadRenderer: partial model preparation id=" +
                  std::to_string(model.doodad_id) + " path=" +
                  model.model_path + " reason=" + prepared.warning);
        }
        model.vertices = std::move(prepared.vertices);
        model.indices = std::move(prepared.indices);
        model.batches = std::move(prepared.batches);
        model.state = ModelState::kReady;
      } catch (const std::exception& exception) {
        model.state = ModelState::kFailed;
        diagnostics::Log(
            diagnostics::LogLevel::kWarn,
            "DetailDoodadRenderer: model preparation failed id=" +
                std::to_string(model.doodad_id) + " path=" +
                model.model_path + " reason=" + exception.what());
      } catch (...) {
        model.state = ModelState::kFailed;
        diagnostics::Log(
            diagnostics::LogLevel::kWarn,
            "DetailDoodadRenderer: model preparation failed id=" +
                std::to_string(model.doodad_id) + " path=" +
                model.model_path + " reason=unknown exception");
      }
    }
  }

  std::uint32_t PackTerrainColor(
      const data::terrain::TerrainChunk& terrain_chunk,
      const TerrainSurfaceSample& sample, const std::uint32_t doodad_flags,
      const std::size_t cell_row, const std::size_t cell_column,
      const float row_fraction, const float column_fraction) const {
    std::uint8_t red = 255u;
    std::uint8_t green = 255u;
    std::uint8_t blue = 255u;
    if ((doodad_flags & 0x2u) == 0u &&
        terrain_chunk.vertex_colors.size() == data::terrain::kVerticesPerChunk) {
      red = InterpolateVertexColorChannel(terrain_chunk, sample, 2u);
      green = InterpolateVertexColorChannel(terrain_chunk, sample, 1u);
      blue = InterpolateVertexColorChannel(terrain_chunk, sample, 0u);
    }
    const std::uint8_t shadow = SampleTerrainShadowVisibility(
        terrain_chunk, cell_row, cell_column, row_fraction,
        column_fraction);
    return static_cast<std::uint32_t>(red) |
           (static_cast<std::uint32_t>(green) << 8u) |
           (static_cast<std::uint32_t>(blue) << 16u) |
           (static_cast<std::uint32_t>(shadow) << 24u);
  }

  void GenerateChunk(TileState& tile, const std::size_t chunk_index) {
    auto& state = tile.chunks[chunk_index];
    if (state.generated || tile.adt == nullptr || dbc == nullptr) {
      return;
    }
    state.generated = true;
    const auto& terrain_chunk = tile.adt->chunks[chunk_index];
    const auto effect_ids =
        world::BuildTerrainChunkGroundEffectIdGrid(terrain_chunk);

    const std::uint32_t absolute_chunk_x =
        static_cast<std::uint32_t>(tile.tile_x * data::terrain::kChunksPerSide) +
        terrain_chunk.header.index_x;
    const std::uint32_t absolute_chunk_y =
        static_cast<std::uint32_t>(tile.tile_y * data::terrain::kChunksPerSide) +
        terrain_chunk.header.index_y;
    DetailDoodadRandom random((absolute_chunk_y << 16u) ^ absolute_chunk_x);
    std::vector<std::uint8_t> selected_cells;
    selected_cells.reserve(ground_effect_density);
    for (std::uint32_t index = 0u; index < ground_effect_density; ++index) {
      const std::uint32_t cell_column = random.Next() & 7u;
      const std::uint32_t cell_row = random.Next() & 7u;
      selected_cells.push_back(
          static_cast<std::uint8_t>(cell_row * 8u + cell_column));
    }

    const std::uint32_t selected_cell_count = ground_effect_density;
    for (std::uint32_t selected_cell = 0u;
         selected_cell < selected_cell_count; ++selected_cell) {
      const std::size_t cell_index = selected_cells[selected_cell];
      const std::uint32_t effect_id = effect_ids[cell_index];
      if (effect_id == 0u) {
        continue;
      }
      const auto* effect = dbc->ground_effect_texture().LookupEntry(effect_id);
      if (effect == nullptr) {
        if (missing_effect_rows.insert(effect_id).second) {
          diagnostics::Log(
              diagnostics::LogLevel::kWarn,
              "DetailDoodadRenderer: GroundEffectTexture row is missing id=" +
                  std::to_string(effect_id) + " tile=" +
                  std::to_string(tile.tile_x) + "," +
                  std::to_string(tile.tile_y) + " chunk=" +
                  std::to_string(chunk_index));
        }
        continue;
      }

      std::array<std::uint32_t, 16> weighted_doodads{};
      std::uint32_t cursor = 0u;
      std::uint32_t weight_sum = 0u;
      for (std::size_t slot = 0u; slot < effect->doodad_id.size(); ++slot) {
        const std::uint32_t weight = static_cast<std::uint32_t>(
            std::max(effect->doodad_weight[slot], 0));
        weight_sum += weight;
        for (std::uint32_t count = 0u; count < weight; ++count) {
          weighted_doodads[cursor & 0x0fu] = effect->doodad_id[slot];
          cursor += 13u;
        }
      }
      for (std::uint32_t fill = std::min(weight_sum, 16u); fill < 16u;
           ++fill) {
        weighted_doodads[cursor & 0x0fu] =
            effect->doodad_id[fill & 3u];
        cursor += 13u;
      }

      const std::uint32_t instance_count = std::min(
          effect->density != 0u ? effect->density
                                : kRetailDefaultEffectDensity,
          kMaximumEffectInstancesPerCell);
      const std::size_t cell_row =
          cell_index / world::kTerrainGroundEffectCellsPerSide;
      const std::size_t cell_column =
          cell_index % world::kTerrainGroundEffectCellsPerSide;
      for (std::uint32_t instance_index = 0u;
           instance_index < instance_count; ++instance_index) {
        const std::uint32_t doodad_id =
            weighted_doodads[(instance_index + selected_cell) & 0x0fu];
        const float row_fraction = random.Unit();
        const float column_fraction = random.Unit();
        if (doodad_id == 0u) {
          continue;
        }
        const auto* doodad =
            dbc->ground_effect_doodad().LookupEntry(doodad_id);
        if (doodad == nullptr || QueueModel(doodad_id) == nullptr) {
          if (doodad == nullptr && missing_doodad_rows.insert(doodad_id).second) {
            diagnostics::Log(
                diagnostics::LogLevel::kWarn,
                "DetailDoodadRenderer: GroundEffectDoodad row is missing id=" +
                    std::to_string(doodad_id) + " effect=" +
                    std::to_string(effect_id));
          }
          continue;
        }

        const auto surface = SampleTerrainCell(
            terrain_chunk, cell_row, cell_column, row_fraction,
            column_fraction);
        if (!surface.has_value() ||
            surface->normal[2] < kMinimumTerrainNormalZ) {
          continue;
        }
        state.instances.push_back({
            .doodad_id = doodad_id,
            .position = surface->position,
            .terrain_normal = surface->normal,
            .yaw = random.Unit() * kTwoPi,
            .scale = kMinimumScale + random.Unit() * kScaleRange,
            .color = PackTerrainColor(
                terrain_chunk, *surface, doodad->flags, cell_row,
                cell_column, row_fraction, column_fraction),
        });
      }
    }

    const float chunk_min_x =
        terrain_chunk.header.position_x - data::terrain::kChunkSize;
    const float chunk_min_y =
        terrain_chunk.header.position_y - data::terrain::kChunkSize;
    float minimum_z = std::numeric_limits<float>::max();
    float maximum_z = std::numeric_limits<float>::lowest();
    for (const float height : terrain_chunk.heights) {
      minimum_z = std::min(minimum_z,
                           terrain_chunk.header.position_z + height);
      maximum_z = std::max(maximum_z,
                           terrain_chunk.header.position_z + height);
    }
    state.bounds = {chunk_min_x, chunk_min_y, minimum_z,
                    terrain_chunk.header.position_x,
                    terrain_chunk.header.position_y, maximum_z};
    if (state.instances.empty()) {
      state.gpu_ready = true;
    }
  }

  bool BuildChunkGpu(ChunkState& chunk) {
    if (!chunk.generated || chunk.gpu_ready) {
      return false;
    }
    for (const auto& instance : chunk.instances) {
      const auto found = models.find(instance.doodad_id);
      if (found != models.end() &&
          (found->second.state == ModelState::kQueued ||
           found->second.state == ModelState::kLoading)) {
        return false;
      }
    }

    using DrawKey = std::pair<std::string, std::uint32_t>;
    std::map<DrawKey, std::vector<std::uint16_t>> indices_by_material;
    std::vector<Vertex> vertices;
    for (const auto& instance : chunk.instances) {
      const auto found = models.find(instance.doodad_id);
      if (found == models.end() || found->second.state != ModelState::kReady) {
        continue;
      }
      const auto& model = found->second;
      if (model.vertices.size() > kMaximumChunkVertexCount ||
          vertices.size() >
              kMaximumChunkVertexCount - model.vertices.size()) {
        diagnostics::Log(diagnostics::LogLevel::kWarn,
                         "DetailDoodadRenderer: 16-bit chunk vertex capacity "
                         "exceeded");
        break;
      }
      const std::uint32_t vertex_base =
          static_cast<std::uint32_t>(vertices.size());
      const float cosine = std::cos(instance.yaw);
      const float sine = std::sin(instance.yaw);
      for (const auto& source : model.vertices) {
        const float local_x = source.position[0] * instance.scale;
        const float local_y = source.position[1] * instance.scale;
        vertices.push_back({
            .position = {
                instance.position[0] + cosine * local_x - sine * local_y,
                instance.position[1] + sine * local_x + cosine * local_y,
                instance.position[2] + source.position[2] * instance.scale,
            },
            .terrain_normal = instance.terrain_normal,
            .texcoord = source.texcoord,
            .color = instance.color,
        });
      }
      for (const auto& batch : model.batches) {
        auto& destination = indices_by_material[
            {batch.texture_path, batch.sampler_flags}];
        destination.reserve(destination.size() + batch.index_count);
        for (std::uint32_t index = 0u; index < batch.index_count; ++index) {
          const std::uint16_t source_index =
              model.indices[batch.first_index + index];
          if (source_index >= model.vertices.size()) {
            continue;
          }
          destination.push_back(
              static_cast<std::uint16_t>(vertex_base + source_index));
        }
      }
    }

    std::vector<std::uint16_t> indices;
    std::vector<GpuDraw> draws;
    for (auto& [key, material_indices] : indices_by_material) {
      if (material_indices.empty()) {
        continue;
      }
      const std::uint32_t first_index =
          static_cast<std::uint32_t>(indices.size());
      indices.insert(indices.end(), material_indices.begin(),
                     material_indices.end());
      draws.push_back({
          .first_index = first_index,
          .index_count = static_cast<std::uint32_t>(material_indices.size()),
          .texture_path = key.first,
          .sampler_flags = key.second,
          .texture = texture_manager.AcquireTextureAsync(
              key.first, TextureLoadFailurePolicy::kStrict,
              TextureLoadPriority::kDemand),
      });
    }
    chunk.gpu_ready = true;
    if (vertices.empty() || indices.empty() || draws.empty()) {
      return true;
    }
    chunk.bounds = {vertices.front().position[0], vertices.front().position[1],
                    vertices.front().position[2], vertices.front().position[0],
                    vertices.front().position[1], vertices.front().position[2]};
    for (const auto& vertex : vertices) {
      chunk.bounds[0] = std::min(chunk.bounds[0], vertex.position[0]);
      chunk.bounds[1] = std::min(chunk.bounds[1], vertex.position[1]);
      chunk.bounds[2] = std::min(chunk.bounds[2], vertex.position[2]);
      chunk.bounds[3] = std::max(chunk.bounds[3], vertex.position[0]);
      chunk.bounds[4] = std::max(chunk.bounds[4], vertex.position[1]);
      chunk.bounds[5] = std::max(chunk.bounds[5], vertex.position[2]);
    }
    if (vertices.size() >
            std::numeric_limits<std::uint32_t>::max() / sizeof(Vertex) ||
        indices.size() > std::numeric_limits<std::uint32_t>::max() /
                             sizeof(std::uint16_t)) {
      diagnostics::Log(
          diagnostics::LogLevel::kWarn,
          "DetailDoodadRenderer: generated chunk buffers exceed backend limits");
      return true;
    }
    chunk.vertices = bgfx::createVertexBuffer(
        bgfx::copy(vertices.data(), static_cast<std::uint32_t>(
                                        vertices.size() * sizeof(Vertex))),
        vertex_layout);
    chunk.indices = bgfx::createIndexBuffer(
        bgfx::copy(indices.data(), static_cast<std::uint32_t>(
                                       indices.size() * sizeof(std::uint16_t))));
    if (!bgfx::isValid(chunk.vertices) || !bgfx::isValid(chunk.indices)) {
      diagnostics::Log(
          diagnostics::LogLevel::kWarn,
          "DetailDoodadRenderer: generated chunk GPU publication failed");
      DestroyChunkGpu(chunk);
      chunk.gpu_ready = true;
      return true;
    }
    chunk.draws = std::move(draws);
    return true;
  }

  TextureManager& texture_manager;
  LoadFileCallback load_file;
  const data::dbc::DbcLoader* dbc{};
  std::unordered_map<std::uint64_t, TileState> tiles;
  std::unordered_map<std::uint32_t, ModelResource> models;
  std::vector<std::future<PreparedDetailModel>> retired_preparations;
  std::set<std::uint32_t> missing_effect_rows;
  std::set<std::uint32_t> missing_doodad_rows;
  std::set<std::uint32_t> invalid_doodad_paths;
  std::uint32_t ground_effect_density{16u};
  float ground_effect_distance{70.0f};
  bgfx::VertexLayout vertex_layout{};
  bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle sampler = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle lighting_uniform = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle params_uniform = BGFX_INVALID_HANDLE;
  bool initialized{};
};

DetailDoodadRenderer::DetailDoodadRenderer(TextureManager& texture_manager)
    : impl_(std::make_unique<Impl>(texture_manager)) {}

DetailDoodadRenderer::~DetailDoodadRenderer() {
  Shutdown();
}

bool DetailDoodadRenderer::Initialize() {
  if (impl_->initialized) {
    return true;
  }
  impl_->vertex_layout
      .begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
      .end();
  impl_->program = CreateEmbeddedProgram(ShaderProgramId::DetailDoodad,
                                         bgfx::getRendererType());
  impl_->sampler =
      bgfx::createUniform("s_detailDoodadTex", bgfx::UniformType::Sampler);
  impl_->lighting_uniform = bgfx::createUniform(
      "u_detailDoodadLighting", bgfx::UniformType::Vec4, 3u);
  impl_->params_uniform = bgfx::createUniform(
      "u_detailDoodadParams", bgfx::UniformType::Vec4, 4u);
  impl_->initialized = bgfx::isValid(impl_->program) &&
                       bgfx::isValid(impl_->sampler) &&
                       bgfx::isValid(impl_->lighting_uniform) &&
                       bgfx::isValid(impl_->params_uniform);
  if (!impl_->initialized) {
    diagnostics::Log(diagnostics::LogLevel::kWarn,
                     "DetailDoodadRenderer: shader initialization failed");
    Shutdown();
  }
  return impl_->initialized;
}

void DetailDoodadRenderer::Shutdown() {
  impl_->Clear();
  impl_->DrainRetiredPreparations(true);
  if (bgfx::isValid(impl_->params_uniform)) {
    bgfx::destroy(impl_->params_uniform);
  }
  if (bgfx::isValid(impl_->lighting_uniform)) {
    bgfx::destroy(impl_->lighting_uniform);
  }
  if (bgfx::isValid(impl_->sampler)) {
    bgfx::destroy(impl_->sampler);
  }
  if (bgfx::isValid(impl_->program)) {
    bgfx::destroy(impl_->program);
  }
  impl_->params_uniform = BGFX_INVALID_HANDLE;
  impl_->lighting_uniform = BGFX_INVALID_HANDLE;
  impl_->sampler = BGFX_INVALID_HANDLE;
  impl_->program = BGFX_INVALID_HANDLE;
  impl_->initialized = false;
}

void DetailDoodadRenderer::SetFileLoader(LoadFileCallback loader) {
  impl_->load_file = std::move(loader);
}

void DetailDoodadRenderer::BindDbc(const data::dbc::DbcLoader* const dbc) {
  if (impl_->dbc == dbc) {
    return;
  }
  impl_->Clear();
  impl_->dbc = dbc;
}

void DetailDoodadRenderer::LoadFromAdt(
    std::shared_ptr<const data::terrain::AdtFile> adt,
    const std::int32_t tile_x, const std::int32_t tile_y) {
  if (!adt) {
    return;
  }
  const auto key = MakeTileKey(tile_x, tile_y);
  if (auto found = impl_->tiles.find(key); found != impl_->tiles.end()) {
    impl_->DestroyTile(found->second);
    impl_->tiles.erase(found);
  }
  impl_->tiles.emplace(
      key, Impl::TileState{.adt = std::move(adt),
                           .tile_x = tile_x,
                           .tile_y = tile_y});
}

void DetailDoodadRenderer::UnloadTile(const std::int32_t tile_x,
                                      const std::int32_t tile_y) {
  const auto found = impl_->tiles.find(MakeTileKey(tile_x, tile_y));
  if (found == impl_->tiles.end()) {
    return;
  }
  impl_->DestroyTile(found->second);
  impl_->tiles.erase(found);
}

void DetailDoodadRenderer::Clear() {
  impl_->Clear();
}

void DetailDoodadRenderer::Update(const RenderVec3& camera_position) {
  if (!impl_->initialized) {
    return;
  }
  auto& cvars = ui::game::CVarSystem::Instance();
  const std::uint32_t density = static_cast<std::uint32_t>(std::clamp(
      cvars.GetCVarInt("groundEffectDensity"), 0,
      static_cast<int>(kMaximumGroundEffectDensity)));
  const float distance =
      std::max(0.0f, cvars.GetCVarFloat("groundEffectDist"));
  if (density != impl_->ground_effect_density) {
    impl_->ground_effect_density = density;
    impl_->ResetGeneratedChunks();
  }
  impl_->ground_effect_distance = distance;

  impl_->PumpPreparedModels();
  if (!world::CWorld_HasRenderFlag(world::WorldRenderFlag::kDetailDoodads) ||
      density == 0u || !(distance > 0.0f) || impl_->dbc == nullptr) {
    return;
  }

  struct Candidate {
    Impl::TileState* tile{};
    std::size_t chunk_index{};
    float distance_squared{};
  };
  std::vector<Candidate> candidates;
  const float distance_squared = distance * distance;
  for (auto& [_, tile] : impl_->tiles) {
    if (!tile.adt) {
      continue;
    }
    for (std::size_t chunk_index = 0u; chunk_index < tile.chunks.size();
         ++chunk_index) {
      if (tile.chunks[chunk_index].generated) {
        continue;
      }
      const auto& chunk = tile.adt->chunks[chunk_index];
      const RenderAabb bounds{
          chunk.header.position_x - data::terrain::kChunkSize,
          chunk.header.position_y - data::terrain::kChunkSize,
          -std::numeric_limits<float>::max(), chunk.header.position_x,
          chunk.header.position_y, std::numeric_limits<float>::max()};
      const float chunk_distance =
          SquaredHorizontalDistanceToBounds(camera_position, bounds);
      if (chunk_distance < distance_squared) {
        candidates.push_back({.tile = &tile,
                              .chunk_index = chunk_index,
                              .distance_squared = chunk_distance});
      }
    }
  }
  std::sort(candidates.begin(), candidates.end(),
            [](const Candidate& lhs, const Candidate& rhs) {
              return lhs.distance_squared < rhs.distance_squared;
            });
  const std::size_t generation_count = std::min<std::size_t>(
      candidates.size(), kMaximumGeneratedChunksPerFrame);
  for (std::size_t index = 0u; index < generation_count; ++index) {
    impl_->GenerateChunk(*candidates[index].tile,
                         candidates[index].chunk_index);
  }

  impl_->StartQueuedModels();
  impl_->PumpPreparedModels();
  std::uint32_t published = 0u;
  for (auto& [_, tile] : impl_->tiles) {
    for (auto& chunk : tile.chunks) {
      if (published >= kMaximumPublishedChunksPerFrame) {
        return;
      }
      if (chunk.generated && !chunk.gpu_ready &&
          impl_->BuildChunkGpu(chunk)) {
        ++published;
      }
    }
  }
}

void DetailDoodadRenderer::Render(
    const std::uint8_t view_id, const float* const view_mtx,
    const float* const projection_mtx, const world::Frustum* const frustum,
    const RenderVec3& camera_position, const WorldM2SceneState& scene_state,
    bgfx::Encoder* const encoder, const bool shadow_caster_pass) {
  if (!impl_->initialized ||
      !world::CWorld_HasRenderFlag(world::WorldRenderFlag::kDetailDoodads) ||
      !(impl_->ground_effect_distance > 0.0f)) {
    return;
  }
  bgfx::setViewTransform(view_id, view_mtx, projection_mtx);
  const RenderVec3 surface_to_light =
      WorldM2SurfaceToLightDirection(scene_state);
  const std::array<RenderVec4, 3> lighting{{
      {surface_to_light[0], surface_to_light[1], surface_to_light[2], 0.0f},
      {scene_state.ambient_color[0], scene_state.ambient_color[1],
       scene_state.ambient_color[2], 0.0f},
      {scene_state.diffuse_color[0], scene_state.diffuse_color[1],
       scene_state.diffuse_color[2], 0.0f},
  }};
  const float fade_start =
      impl_->ground_effect_distance * kRetailDetailFadeStartRatio;
  const float fade_width =
      std::max(impl_->ground_effect_distance - fade_start, 0.0001f);
  const std::array<RenderVec4, 4> params{{
      {camera_position[0], camera_position[1], camera_position[2],
       impl_->ground_effect_distance},
      {fade_start, 1.0f / fade_width,
       static_cast<float>(world::CWorld_GetDetailDoodadAlpha()) / 255.0f,
       0.0f},
      scene_state.fog.params,
      scene_state.fog.color,
  }};
  const DrawEncoder draw{encoder};
  const RenderMatrix4x4 identity = kRenderIdentityMatrix4x4;
  const std::uint64_t state = shadow_caster_pass
                                  ? BGFX_STATE_WRITE_Z |
                                        BGFX_STATE_DEPTH_TEST_LESS
                                  : BGFX_STATE_WRITE_RGB |
                                        BGFX_STATE_WRITE_A |
                                        BGFX_STATE_WRITE_Z |
                                        BGFX_STATE_DEPTH_TEST_LESS |
                                        BGFX_STATE_BLEND_FUNC(
                                            BGFX_STATE_BLEND_SRC_ALPHA,
                                            BGFX_STATE_BLEND_INV_SRC_ALPHA) |
                                        BGFX_STATE_MSAA;
  const float distance_squared =
      impl_->ground_effect_distance * impl_->ground_effect_distance;
  for (auto& [_, tile] : impl_->tiles) {
    for (auto& chunk : tile.chunks) {
      if (!chunk.gpu_ready || !bgfx::isValid(chunk.vertices) ||
          !bgfx::isValid(chunk.indices) ||
          SquaredHorizontalDistanceToBounds(camera_position, chunk.bounds) >=
              distance_squared ||
          (frustum != nullptr &&
           !frustum->TestAABB(chunk.bounds[0], chunk.bounds[1], chunk.bounds[2],
                              chunk.bounds[3], chunk.bounds[4],
                              chunk.bounds[5]))) {
        continue;
      }
      for (auto& batch : chunk.draws) {
        if (!batch.texture) {
          batch.texture = impl_->texture_manager.AcquireTextureAsync(
              batch.texture_path, TextureLoadFailurePolicy::kStrict,
              TextureLoadPriority::kDemand);
        }
        const bgfx::TextureHandle texture =
            batch.texture
                ? BgfxTextureLeaseAccess::Get(batch.texture)
                : bgfx::TextureHandle{bgfx::kInvalidHandle};
        if (!bgfx::isValid(texture)) {
          continue;
        }
        draw.setTransform(identity.data());
        draw.setUniform(impl_->lighting_uniform, lighting.data(),
                        static_cast<std::uint16_t>(lighting.size()));
        draw.setUniform(impl_->params_uniform, params.data(),
                        static_cast<std::uint16_t>(params.size()));
        draw.setVertexBuffer(0, chunk.vertices);
        draw.setIndexBuffer(chunk.indices, batch.first_index,
                            batch.index_count);
        draw.setTexture(0, impl_->sampler, texture, batch.sampler_flags);
        draw.setState(state);
        draw.submit(view_id, impl_->program);
      }
    }
  }
}

}

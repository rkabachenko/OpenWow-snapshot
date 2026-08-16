#include "openwow/world/streaming/world_map.h"

#include "openwow/data/formats/dbc/area_environment_rules.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_table_registry.h"
#include "openwow/data/terrain/adt_file.h"
#include "openwow/data/terrain/wdt_file.h"
#include "openwow/data/wmo/wmo_file.h"
#include "openwow/debug/inspection/render_debug.h"
#include "openwow/world/environment/day_night.h"
#include "openwow/world/coordinates/frustum.h"
#include "openwow/world/environment/lighting.h"
#include "openwow/world/coordinates/map_placement.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/world/collision/collision.h"
#include "openwow/world/environment/chunk_ambient_audio.h"
#include "openwow/world/liquid/liquid_vertex_lookup.h"
#include "openwow/world/environment/weather.h"
#include "openwow/world/environment/wmo_fog.h"
#include "openwow/world/liquid/wmo_liquid_surface.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <limits>
#include <sstream>
#include <thread>

namespace openwow::world {

namespace {

constexpr float kMinutesPerDay = 1440.0f;
constexpr float kSecondsToMilliseconds = 1000.0f;
constexpr float kCriticalSpawnSurfaceRadius = 96.0f;
constexpr float kWmoHighPriorityRadius = 384.0f;

constexpr float kWmoRoomQueryDepth = 1000.0f;
constexpr float kWmoRoomQueryStartLift = 0.1f;

constexpr float kRetailCameraRoomRayDepth = 1760.0f;

constexpr std::uint32_t kRetailRoomExcludedGroupFlags = 0x00410080u;

[[nodiscard]] std::size_t AreaEnvironmentQuerySlot(
    const float x, const float y, const float z,
    const std::size_t slot_count) noexcept {
  constexpr std::uint64_t kSlotHashSeed = 0x9e3779b97f4a7c15ull;
  constexpr std::uint64_t kMurmur3FinalizerMultiplier = 0xff51afd7ed558ccdull;
  constexpr unsigned kMurmur3FinalizerShift = 33u;
  std::uint64_t hash = kSlotHashSeed;
  for (const float component : {x, y, z}) {
    hash ^= static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(component));
    hash *= kMurmur3FinalizerMultiplier;
    hash ^= hash >> kMurmur3FinalizerShift;
  }
  return static_cast<std::size_t>(hash) & (slot_count - 1u);
}

[[nodiscard]] Vec3 UnitRgb(const std::uint32_t argb) {
  constexpr float scale = 1.0f / 255.0f;
  return {
      static_cast<float>((argb >> 16u) & 0xffu) * scale,
      static_cast<float>((argb >> 8u) & 0xffu) * scale,
      static_cast<float>(argb & 0xffu) * scale,
  };
}

[[nodiscard]] Vec3 Normalize(Vec3 value) {
  const float length_squared =
      value[0] * value[0] + value[1] * value[1] + value[2] * value[2];
  if (length_squared <= 2.3841858e-7f) {
    return value;
  }
  const float inverse_length = 1.0f / std::sqrt(length_squared);
  for (float& component : value) {
    component *= inverse_length;
  }
  return value;
}

[[nodiscard]] float BoundsDistanceSquared3D(
    const std::array<float, 6>& bounds, const float x, const float y,
    const float z) {
  const float dx = x < bounds[0] ? bounds[0] - x
                                 : (x > bounds[3] ? x - bounds[3] : 0.0f);
  const float dy = y < bounds[1] ? bounds[1] - y
                                 : (y > bounds[4] ? y - bounds[4] : 0.0f);
  const float dz = z < bounds[2] ? bounds[2] - z
                                 : (z > bounds[5] ? z - bounds[5] : 0.0f);
  return dx * dx + dy * dy + dz * dz;
}

[[nodiscard]] std::string BuildWmoGroupPath(const std::string& root_path,
                                            const std::uint32_t group_index) {
  std::string path = root_path;
  if (path.size() > 4u) {
    path.resize(path.size() - 4u);
  }
  char suffix[16];
  std::snprintf(suffix, sizeof(suffix), "_%03u.wmo", group_index);
  path += suffix;
  return path;
}

[[nodiscard]] std::vector<std::uint32_t> CollectWmoGroupMaterialIndices(
    const data::wmo::WmoGroup& group, const std::size_t material_count) {
  std::vector<std::uint32_t> indices;
  indices.reserve(group.renderBatches.size());
  for (const auto& batch : group.renderBatches) {
    const auto index = static_cast<std::uint32_t>(batch.materialId);
    if (index < material_count) {
      indices.push_back(index);
    }
  }
  std::sort(indices.begin(), indices.end());
  indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
  return indices;
}

[[nodiscard]] core::TaskPriority WmoTaskPriority(
    const float distance_squared) {
  if (distance_squared <=
      kCriticalSpawnSurfaceRadius * kCriticalSpawnSurfaceRadius) {
    return core::TaskPriority::Critical;
  }
  if (distance_squared <= kWmoHighPriorityRadius * kWmoHighPriorityRadius) {
    return core::TaskPriority::High;
  }
  return core::TaskPriority::Normal;
}

void SortTileLoadOrderByVisibleAreaDistance(std::vector<TileCoord> &coords, const float focus_x,
                                            const float focus_y) {
  std::stable_sort(coords.begin(), coords.end(),
                   [focus_x, focus_y](const TileCoord &lhs, const TileCoord &rhs) {
                     const float lhs_distance_sq = TileBoundsDistanceSq2D(focus_x, focus_y, lhs);
                     const float rhs_distance_sq = TileBoundsDistanceSq2D(focus_x, focus_y, rhs);
                     return rhs_distance_sq > lhs_distance_sq;
                   });
}

std::uint8_t MapLiquidTypeToRendererType(const std::uint16_t liquid_type) {
  if (liquid_type == 2u || liquid_type == 14u || liquid_type == 41u) {
    return 1u;
  }
  if (liquid_type == 4u || liquid_type == 19u || liquid_type == 61u) {
    return 2u;
  }
  if (liquid_type == 7u || liquid_type == 20u || liquid_type == 81u) {
    return 3u;
  }
  return 0u;
}

bool BoundsOverlap(const std::array<float, 6> &lhs, const std::array<float, 6> &rhs) {
  return lhs[0] <= rhs[3] && lhs[3] >= rhs[0] && lhs[1] <= rhs[4] && lhs[4] >= rhs[1] &&
         lhs[2] <= rhs[5] && lhs[5] >= rhs[2];
}

void VisitMovementTriangle(
    const Vec3& a, const Vec3& b,
    const Vec3& c, const std::array<float, 6>& world_bounds,
    const std::uint64_t owner_id, const std::uint64_t facet_id,
    const std::uint8_t source_flags, const bool orient_up,
    const CollisionFacetVisitor& visitor) {
  const std::array<float, 6> triangle_bounds{
      std::min({a[0], b[0], c[0]}), std::min({a[1], b[1], c[1]}),
      std::min({a[2], b[2], c[2]}), std::max({a[0], b[0], c[0]}),
      std::max({a[1], b[1], c[1]}), std::max({a[2], b[2], c[2]})};
  if (!BoundsOverlap(triangle_bounds, world_bounds)) {
    return;
  }

  const Vec3 edge_a{b[0] - a[0], b[1] - a[1],
                                  b[2] - a[2]};
  const Vec3 edge_b{c[0] - a[0], c[1] - a[1],
                                  c[2] - a[2]};
  std::array<float, 3> normal{
      edge_a[1] * edge_b[2] - edge_a[2] * edge_b[1],
      edge_a[2] * edge_b[0] - edge_a[0] * edge_b[2],
      edge_a[0] * edge_b[1] - edge_a[1] * edge_b[0]};
  if (orient_up && normal[2] < 0.0f) {
    normal[0] = -normal[0];
    normal[1] = -normal[1];
    normal[2] = -normal[2];
  }
  const float normal_length = std::sqrt(
      normal[0] * normal[0] + normal[1] * normal[1] +
      normal[2] * normal[2]);
  if (normal_length <= 1.0e-8f) {
    return;
  }
  normal[0] /= normal_length;
  normal[1] /= normal_length;
  normal[2] /= normal_length;

  CollisionFacetView facet;
  facet.vertices = {{{a[0], a[1], a[2]},
                     {b[0], b[1], b[2]},
                     {c[0], c[1], c[2]}}};
  facet.normal = normal;
  facet.owner_id = owner_id;
  facet.facet_id = facet_id;
  facet.source_flags = source_flags;
  visitor(facet);
}

void VisitWaterHeightfieldFacets(
    const WaterHeightfield& surface,
    const std::array<float, 6>& world_bounds, const std::uint64_t owner_id,
    const CollisionFacetVisitor& visitor) {
  if (surface.vertex_rows < 2u || surface.vertex_cols < 2u ||
      surface.vertex_rows >
          std::numeric_limits<std::size_t>::max() / surface.vertex_cols) {
    return;
  }
  const std::size_t vertex_count =
      static_cast<std::size_t>(surface.vertex_rows) * surface.vertex_cols;
  const bool explicit_positions = surface.positions.size() == vertex_count;
  if (!explicit_positions && surface.heights.size() != vertex_count) {
    return;
  }
  const std::size_t cell_rows = surface.vertex_rows - 1u;
  const std::size_t cell_columns = surface.vertex_cols - 1u;
  if (cell_rows > std::numeric_limits<std::size_t>::max() / cell_columns) {
    return;
  }
  const std::size_t cell_count = cell_rows * cell_columns;
  const bool has_mask = surface.render_mask.size() == cell_count;

  const auto vertex = [&](const std::uint32_t row,
                          const std::uint32_t column) {
    const std::size_t index =
        static_cast<std::size_t>(row) * surface.vertex_cols + column;
    if (explicit_positions) {
      return surface.positions[index];
    }
    return Vec3{
        surface.origin_x + static_cast<float>(row) * surface.step_x,
        surface.origin_z + static_cast<float>(column) * surface.step_z,
        surface.heights[index]};
  };

  for (std::uint32_t row = 0u; row + 1u < surface.vertex_rows; ++row) {
    for (std::uint32_t column = 0u; column + 1u < surface.vertex_cols;
         ++column) {
      const std::size_t cell =
          static_cast<std::size_t>(row) * cell_columns + column;
      if ((has_mask && surface.render_mask[cell] == 0u) ||
          cell > (std::numeric_limits<std::uint32_t>::max() - 1u) / 2u) {
        continue;
      }
      const Vec3 v00 = vertex(row, column);
      const Vec3 v10 = vertex(row, column + 1u);
      const Vec3 v01 = vertex(row + 1u, column);
      const Vec3 v11 = vertex(row + 1u, column + 1u);
      const std::uint32_t facet = static_cast<std::uint32_t>(cell * 2u);

      VisitMovementTriangle(v00, v11, v01, world_bounds, owner_id, facet,
                            0u, true, visitor);
      VisitMovementTriangle(v00, v10, v11, world_bounds, owner_id, facet + 1u,
                            0u, true, visitor);
    }
  }
}

constexpr std::size_t kTerrainChunkCellGridSide = 8u;
constexpr std::size_t kTerrainChunkCellCount =
    kTerrainChunkCellGridSide * kTerrainChunkCellGridSide;
constexpr std::size_t kTerrainAlphaMapSide = 64u;
constexpr std::size_t kTerrainAlphaPixelsPerCell = kTerrainAlphaMapSide / kTerrainChunkCellGridSide;
constexpr float kGroundTypeQueryMapHalfSize = kWorldGridCenter;
constexpr float kGroundTypeQueryMapFullSize = 34133.332f;

[[nodiscard]] std::array<std::uint32_t, kTerrainChunkCellCount>
BuildChunkGroundEffectIdGrid(const data::terrain::TerrainChunk &chunk, const bool big_alpha) {
  std::array<std::uint32_t, kTerrainChunkCellCount> effect_ids{};
  const std::size_t layer_count = std::min<std::size_t>(chunk.layers.size(), 4u);
  if (layer_count == 0u) {
    return effect_ids;
  }

  std::array<std::vector<std::uint8_t>, 4> layer_alpha_maps{};
  for (std::size_t layer_index = 1u; layer_index < layer_count; ++layer_index) {
    const auto &layer = chunk.layers[layer_index];
    if ((layer.flags & data::terrain::AlphaMapFlags::kHasAlpha) == 0u ||
        layer.alpha_map_offset >= chunk.alpha_data.size()) {
      continue;
    }

    layer_alpha_maps[layer_index] = data::terrain::DecompressAlphaMap(
        chunk.alpha_data.data() + layer.alpha_map_offset,
        chunk.alpha_data.size() - layer.alpha_map_offset, layer.flags, big_alpha,
        (chunk.header.flags & data::terrain::McnkFlags::kDoNotFixAlphaMap) == 0u);
  }

  for (std::size_t cell_row = 0; cell_row < kTerrainChunkCellGridSide; ++cell_row) {
    for (std::size_t cell_col = 0; cell_col < kTerrainChunkCellGridSide; ++cell_col) {
      const std::size_t cell_index = cell_row * kTerrainChunkCellGridSide + cell_col;
      if (IsTerrainHoleCell(chunk.holes, static_cast<int>(cell_row),
                            static_cast<int>(cell_col))) {
        continue;
      }

      std::array<std::uint32_t, 4> layer_weight_sums{};
      for (std::size_t alpha_row = 0; alpha_row < kTerrainAlphaPixelsPerCell; ++alpha_row) {
        for (std::size_t alpha_col = 0; alpha_col < kTerrainAlphaPixelsPerCell; ++alpha_col) {
          const std::size_t pixel_row = cell_row * kTerrainAlphaPixelsPerCell + alpha_row;
          const std::size_t pixel_col = cell_col * kTerrainAlphaPixelsPerCell + alpha_col;
          const std::size_t pixel_index = pixel_row * kTerrainAlphaMapSide + pixel_col;

          std::uint32_t secondary_weight = 0u;
          for (std::size_t layer_index = 1u; layer_index < layer_count; ++layer_index) {
            const auto &alpha_map = layer_alpha_maps[layer_index];
            const std::uint32_t alpha =
                pixel_index < alpha_map.size() ? alpha_map[pixel_index] : 0u;
            layer_weight_sums[layer_index] += alpha;
            secondary_weight += alpha;
          }

          const std::uint32_t base_weight = static_cast<std::uint8_t>(255u - secondary_weight);
          layer_weight_sums[0] += base_weight;
        }
      }

      std::size_t best_layer = 0u;
      for (std::size_t layer_index = 1u; layer_index < layer_count; ++layer_index) {
        if (layer_weight_sums[layer_index] > layer_weight_sums[best_layer]) {
          best_layer = layer_index;
        }
      }

      effect_ids[cell_index] = chunk.layers[best_layer].effect_id;
    }
  }

  return effect_ids;
}

[[nodiscard]] std::uint32_t ResolveGroundTypeFromEffectId(const openwow::data::dbc::DbcLoader &dbc,
                                                          const std::uint32_t effect_id) {
  if (effect_id == 0u) {
    return 0u;
  }

  const auto *const entry = dbc.ground_effect_texture().LookupEntry(effect_id);
  if (entry == nullptr) {
    return 0u;
  }

  return entry->density;
}

constexpr float kRetailAdtLiquidFixedTexcoordScale = 0.01171875f;
constexpr float kRetailLiquidWorldTexcoordPeriod = 16.666666f;

WaterHeightfield BuildMh2oWaterHeightfield(
    const data::terrain::TerrainChunk &chunk,
    const data::terrain::AdtWaterLayer &layer) {
  WaterHeightfield heightfield{};
  heightfield.origin_x =
      chunk.header.position_x - static_cast<float>(layer.y_offset) * data::terrain::kUnitSize;
  heightfield.origin_z =
      chunk.header.position_y - static_cast<float>(layer.x_offset) * data::terrain::kUnitSize;
  heightfield.step_x = -data::terrain::kUnitSize;
  heightfield.step_z = -data::terrain::kUnitSize;
  heightfield.vertex_rows = static_cast<std::uint32_t>(layer.height) + 1u;
  heightfield.vertex_cols = static_cast<std::uint32_t>(layer.width) + 1u;
  heightfield.type = MapLiquidTypeToRendererType(layer.liquid_type);
  heightfield.depth_level = 2u;
  heightfield.liquid_type_id = layer.liquid_type;

  const std::size_t vertex_count =
      static_cast<std::size_t>(heightfield.vertex_rows) * heightfield.vertex_cols;
  heightfield.heights.reserve(vertex_count);
  for (std::size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
    heightfield.heights.push_back(data::terrain::AdtWaterLayer_GetMh2oHeight(layer, vertex_index));
  }
  heightfield.vertex_texcoords.reserve(vertex_count);
  for (std::uint32_t row = 0u; row < heightfield.vertex_rows; ++row) {
    for (std::uint32_t col = 0u; col < heightfield.vertex_cols; ++col) {
      const std::size_t vertex_index =
          static_cast<std::size_t>(row) * heightfield.vertex_cols + col;
      if (layer.liquid_object == static_cast<std::uint16_t>(
                                     data::terrain::AdtWaterLayerObjectKind::
                                         HeightAndWordPayload) &&
          vertex_index < layer.aux_word_payload.size()) {
        const std::uint32_t packed = layer.aux_word_payload[vertex_index];
        heightfield.vertex_texcoords.push_back({
            static_cast<float>(packed & 0xFFFFu) *
                kRetailAdtLiquidFixedTexcoordScale,
            static_cast<float>(packed >> 16u) *
                kRetailAdtLiquidFixedTexcoordScale,
        });
      } else {
        heightfield.vertex_texcoords.push_back({
            (heightfield.origin_x +
             static_cast<float>(row) * heightfield.step_x) /
                kRetailLiquidWorldTexcoordPeriod,
            (heightfield.origin_z +
             static_cast<float>(col) * heightfield.step_z) /
                kRetailLiquidWorldTexcoordPeriod,
        });
      }
    }
  }

  const std::size_t cell_count = static_cast<std::size_t>(layer.width) * layer.height;
  heightfield.render_mask.reserve(cell_count);
  if (layer.exists_bitmap.size() == cell_count) {
    for (const bool cell_exists : layer.exists_bitmap) {
      heightfield.render_mask.push_back(cell_exists ? 1u : 0u);
    }
  } else {
    heightfield.render_mask.assign(cell_count, 1u);
  }

  return heightfield;
}

std::vector<WaterHeightfield>
BuildAdtWaterHeightfields(const data::terrain::AdtFile &adt,
                          const std::uint32_t map_id,
                          const std::array<std::uint32_t, 4>& liquid_vertex_formats,
                          const std::array<std::uint32_t, 4>& liquid_material_variants) {
  std::vector<WaterHeightfield> surfaces;
  for (std::size_t chunk_index = 0u; chunk_index < adt.chunks.size(); ++chunk_index) {
    const auto &chunk = adt.chunks[chunk_index];
    const auto &water_chunk = adt.water_chunks[chunk_index];
    if (!water_chunk.layers.empty()) {
      for (const auto &layer : water_chunk.layers) {
        surfaces.push_back(BuildMh2oWaterHeightfield(chunk, layer));
      }
    }

    for (const auto &layer : chunk.legacy_liquid_layers) {
      constexpr std::uint32_t kMclqGridSubdivisions = 8u;
      WaterHeightfield surface{};
      surface.origin_x = chunk.header.position_x;
      surface.origin_z = chunk.header.position_y;
      surface.step_x = -data::terrain::kChunkSize /
                       static_cast<float>(kMclqGridSubdivisions);
      surface.step_z = surface.step_x;
      surface.vertex_rows = kMclqGridSubdivisions + 1u;
      surface.vertex_cols = kMclqGridSubdivisions + 1u;
      if (liquid_vertex_formats[layer.category] < 2u) {
        surface.heights.assign(layer.heights.begin(), layer.heights.end());
      } else {
        surface.heights.assign(layer.heights.size(), 0.0f);
      }
      surface.type = layer.category;
      surface.liquid_type_id = layer.category == 2u && map_id == 0x212u
                                   ? 15u
                                   : static_cast<std::uint32_t>(layer.category) + 1u;
      const std::uint8_t selector = static_cast<std::uint8_t>(
          (surface.liquid_type_id - 1u) & 3u);
      surface.render_mask.reserve(layer.cell_flags.size());
      for (const std::uint8_t flags : layer.cell_flags) {
        surface.render_mask.push_back(
            (flags & 0x0Fu) != 0x0Fu && (flags & 3u) == selector ? 1u : 0u);
      }
      if (liquid_vertex_formats[layer.category] == 0u ||
          liquid_vertex_formats[layer.category] == 2u) {
        surface.vertex_depths.reserve(layer.auxiliary_data.size());
        for (const auto& auxiliary : layer.auxiliary_data) {
          surface.vertex_depths.push_back(RetailLiquidVertexAttenuation(
              liquid_material_variants[layer.category], auxiliary[0]));
        }
      }
      surface.vertex_texcoords.reserve(layer.heights.size());
      for (std::size_t row = 0u; row < 9u; ++row) {
        for (std::size_t col = 0u; col < 9u; ++col) {
          const std::size_t vertex_index = row * 9u + col;
          if (liquid_vertex_formats[layer.category] == 1u) {
            const auto& auxiliary = layer.auxiliary_data[vertex_index];
            const std::uint16_t first = static_cast<std::uint16_t>(
                auxiliary[0] | (static_cast<std::uint16_t>(auxiliary[1]) << 8u));
            const std::uint16_t second = static_cast<std::uint16_t>(
                auxiliary[2] | (static_cast<std::uint16_t>(auxiliary[3]) << 8u));

            surface.vertex_texcoords.push_back({
                static_cast<float>(first) * kRetailAdtLiquidFixedTexcoordScale,
                static_cast<float>(second) * kRetailAdtLiquidFixedTexcoordScale,
            });
          } else {
            surface.vertex_texcoords.push_back({
                (surface.origin_x + static_cast<float>(row) * surface.step_x) /
                    kRetailLiquidWorldTexcoordPeriod,
                (surface.origin_z + static_cast<float>(col) * surface.step_z) /
                    kRetailLiquidWorldTexcoordPeriod,
            });
          }
        }
      }
      surface.depth_level = 2u;
      surfaces.push_back(std::move(surface));
    }
  }
  return surfaces;
}

[[nodiscard]] Vec3 TransformPoint(const Matrix4 &model_mtx,
                                                const Vec3 &point) {
  return openwow::world::TransformPoint(point, model_mtx);
}

}

std::unique_ptr<CachedWmo>
WorldMap::PrepareWmoCpuBundle(const WorldMap::LoadFileCallback &load_file,
                              const std::string &wmo_path,
                              std::string *error) {
  if (!load_file) {
    *error = "no file loader";
    return {};
  }

  auto root_data = load_file(wmo_path);
  if (root_data.empty()) {
    *error = "root not found";
    return {};
  }

  auto root_result = data::wmo::LoadWmoRoot(root_data);
  if (!root_result.ok) {
    *error = "root parse failed: " + root_result.error;
    return {};
  }

  const std::size_t group_count = root_result.root.groupInfos.size();
  auto cached = std::make_unique<CachedWmo>();
  cached->groups.resize(group_count);
  cached->group_presentation_payloads.resize(group_count);
  cached->group_material_indices.resize(group_count);
  cached->collision_vertices.resize(group_count);
  cached->area_triangle_indices.resize(group_count);
  cached->group_residency.assign(group_count,
                                 WmoGroupResidency::kUnrequested);
  cached->group_cpu_retry.resize(group_count);
  cached->group_gpu_queued.assign(group_count, false);
  cached->group_gpu_publication.assign(
      group_count, WmoGroupPublicationStatus::kPending);
  cached->group_gpu_retry.resize(group_count);
  cached->visibility =
      world::WmoVisibilityData::BuildRootMetadata(root_result.root);
  cached->root_wmo_id = root_result.root.header.wmoID;
  cached->root = std::move(root_result.root);
  return cached;
}

WorldMap::WorldMap(audio::SoundRuntime* ambient_audio)
    : ambient_audio_(ambient_audio) {}

WorldMap::~WorldMap() {
  Shutdown();
}

bool WorldMap::Initialize() {
  if (lifecycle_state_ != LifecycleState::kStopped) {
    return lifecycle_state_ == LifecycleState::kRunning &&
           initialization_succeeded_;
  }
  lifecycle_state_ = LifecycleState::kStarting;

  const std::uint32_t hardware_threads = std::thread::hardware_concurrency();
  const std::uint32_t worker_count =
      std::clamp(hardware_threads > 1u ? hardware_threads - 1u : 1u, 1u, 4u);
  InitializeWorldStagingWorkers(worker_count);

  initialization_succeeded_ = true;
  lifecycle_state_ = LifecycleState::kRunning;
  return true;
}

void WorldMap::UnloadMap() {
  const bool had_active_map = !map_name_.empty() || wdt_loaded_ || wdl_loaded_ ||
                              !loaded_tiles_.empty() || !wmo_instances_.empty();

  CancelWorldStaging();

  if (!loaded_tiles_.empty()) {
    std::vector<TileCoord> coords;
    coords.reserve(loaded_tiles_.size());
    for (const auto &[coord, tile] : loaded_tiles_) {
      (void)tile;
      coords.push_back(coord);
    }
    for (const auto &coord : coords) {
      UnloadTile(coord);
    }
  }

  presentation_commands_.emplace_back(ResetPresentationCommand{});
  wmo_instances_.clear();
  wmo_area_spatial_index_.Clear();
  streaming_ownership_.Clear();
  active_wdl_wmo_owners_.clear();
  object_wmo_placements_.clear();
  wmo_cache_.clear();

  InvalidateAreaEnvironmentCache();
  active_weather_type_ = WeatherKind::kNone;
  active_weather_density_ = 0.0f;
  active_weather_row_.reset();
  terrain_streamer_.Reset();
  terrain_streamer_.SetTileExistsCallback({});
  map_id_ = 0u;
  map_name_.clear();
  wdt_ = {};
  wdl_.reset();
  wdt_loaded_ = false;
  wdl_loaded_ = false;
  player_x_ = 0.0f;
  player_y_ = 0.0f;
  player_z_ = 0.0f;
  camera_x_ = 0.0f;
  camera_y_ = 0.0f;
  camera_z_ = 0.0f;
  has_camera_position_ = false;
  player_is_outdoors_ = true;
  has_player_streaming_focus_ = false;
  player_tile_ = {0, 0};
  streaming_x_ = 0.0f;
  streaming_y_ = 0.0f;
  has_camera_streaming_focus_ = false;
  streaming_tile_ = {0, 0};
  openwow::game::DayNight_ClearScreenEffectFogOverride();
  outdoor_fog_ = {};
  fog_density_ = 0.0f;

  if (had_active_map) {
    diagnostics::Log(diagnostics::LogLevel::kInfo,
              "WorldMap: active map unloaded; render runtime retained");
  }
}

void WorldMap::Shutdown() {
  if (lifecycle_state_ == LifecycleState::kStopped ||
      lifecycle_state_ == LifecycleState::kStopping) {
    return;
  }
  lifecycle_state_ = LifecycleState::kStopping;

  CancelWorldStaging();
  world_staging_workers_.Shutdown();
  world_staging_mailbox_ = std::make_shared<WorldStagingMailbox>();

  UnloadMap();
  initialization_succeeded_ = false;
  lifecycle_state_ = LifecycleState::kStopped;
  diagnostics::Log(diagnostics::LogLevel::kInfo, "WorldMap: shutdown complete");
}

void WorldMap::SetMap(uint32_t map_id, const std::string &map_internal_name) {
  UnloadMap();

  terrain_streamer_.OnMapChange(map_id, 0.0f, 0.0f);

  map_id_ = map_id;
  map_name_ = map_internal_name;
  openwow::game::DayNight_ClearScreenEffectFogOverride();
  outdoor_fog_ = {};
  fog_density_ = 0.0f;
  player_tile_ = {0, 0};
  streaming_tile_ = {0, 0};
  wdt_ = {};
  wdl_.reset();
  wdt_loaded_ = false;
  wdl_loaded_ = false;

  const std::string map_path =
      "World\\Maps\\" + map_internal_name + "\\" + map_internal_name;
  if (load_file_) {
    const auto wdt_bytes = load_file_(map_path + ".wdt");
    if (!wdt_bytes.empty()) {
      auto loaded_wdt = data::terrain::LoadWdt(wdt_bytes);
      if (loaded_wdt.ok) {
        wdt_ = std::move(loaded_wdt.wdt);
        wdt_loaded_ = true;
      }
    }

    const auto wdl_bytes = load_file_(map_path + ".wdl");
    if (!wdl_bytes.empty()) {
      auto loaded_wdl = data::terrain::LoadWdl(wdl_bytes);
      if (loaded_wdl.ok) {
        wdl_ = std::make_shared<const data::terrain::WdlFile>(
            std::move(loaded_wdl.wdl));
        wdl_loaded_ = true;
      }
    }
  }
  openwow::game::DayNight_Init(static_cast<int>(map_id));

  diagnostics::Log(diagnostics::LogLevel::kInfo,
            "WorldMap: SetMap id=" + std::to_string(map_id) + " name=" + map_internal_name);

  terrain_streamer_.SetTileExistsCallback([this](int tx, int ty) -> bool {
    if (!wdt_loaded_)
      return true;
    if (tx < 0 || tx > 63 || ty < 0 || ty > 63)
      return false;
    const auto file_tile = ToMapFileTile({tx, ty});
    return wdt_.TileExists(static_cast<uint32_t>(file_tile.x),
                           static_cast<uint32_t>(file_tile.y));
  });

  if (wdt_loaded_) {
    diagnostics::Log(diagnostics::LogLevel::kInfo, "WorldMap: WDT loaded for " + map_internal_name + " (" +
                                         std::to_string(wdt_.CountExistingTiles()) + " tiles)");
  }

  if (wdl_loaded_) {
    presentation_commands_.emplace_back(
        PublishDistantTerrainCommand{.wdl = wdl_});
    diagnostics::Log(diagnostics::LogLevel::kInfo,
                     "WorldMap: WDL loaded for " + map_internal_name);
  }
  if (!wdl_loaded_) {
    diagnostics::Log(diagnostics::LogLevel::kInfo,
                     "WorldMap: WDL not found (no distant terrain): " +
                         map_path + ".wdl");
  }

  RegisterMapWmoPlacementOwners();

  if (dbc_) {
    lighting_.LoadForMap(*dbc_, map_id);

    PublishAdvancedFogInputs();
    RefreshLightingEnvironment();
  }
}

void WorldMap::UpdatePlayerPosition(float x, float y, float z) {
  player_x_ = x;
  player_y_ = y;
  player_z_ = z;
  has_player_streaming_focus_ = true;

  player_tile_ = WorldToTile(x, y);
  player_tile_.x = std::clamp(player_tile_.x, kMinTile, kMaxTile);
  player_tile_.y = std::clamp(player_tile_.y, kMinTile, kMaxTile);

  if (!has_camera_streaming_focus_) {
    UpdateStreamingPosition(x, y);
  }

  RefreshLightingEnvironment();
}

void WorldMap::UpdateStreamingPosition(const float x, const float y) {
  streaming_x_ = x;
  streaming_y_ = y;
  has_camera_streaming_focus_ = true;

  TileCoord new_tile = WorldToTile(x, y);

  new_tile.x = std::max(kMinTile, std::min(kMaxTile, new_tile.x));
  new_tile.y = std::max(kMinTile, std::min(kMaxTile, new_tile.y));

  terrain_streamer_.Update(x, y, map_id_);

  if (new_tile != streaming_tile_ || loaded_tiles_.empty()) {
    streaming_tile_ = new_tile;

    const auto required = GetRequiredTiles();

    std::vector<TileCoord> to_unload;
    for (const auto &[coord, tile_ptr] : loaded_tiles_) {
      if (required.find(coord) == required.end()) {
        to_unload.push_back(coord);
      }
    }
    for (const auto &coord : to_unload) {
      UnloadTile(coord);
    }

    for (auto it = pending_tile_loads_.begin(); it != pending_tile_loads_.end();) {
      if (!required.contains(it->first)) {
        if (it->second.task_id != 0u) {
          static_cast<void>(world_staging_workers_.CancelTask(it->second.task_id));
        }
        ready_tile_loads_.erase(it->first);
        it = pending_tile_loads_.erase(it);
      } else {
        ++it;
      }
    }

    std::vector<TileCoord> to_load;
    to_load.reserve(required.size());
    for (const auto &coord : required) {
      if (wdt_loaded_) {
        const auto file_tile = ToMapFileTile(coord);
        if (!wdt_.TileExists(static_cast<std::uint32_t>(file_tile.x),
                             static_cast<std::uint32_t>(file_tile.y))) {
          continue;
        }
      }
      if (load_file_ && !loaded_tiles_.contains(coord) &&
          !pending_tile_loads_.contains(coord)) {
        to_load.push_back(coord);
      }
    }
    SortTileLoadOrderByVisibleAreaDistance(to_load, x, y);

    if (!to_load.empty() && !active_stream_progress_callback_) {
      active_stream_progress_callback_ = blocking_load_progress_callback_;
    }
    if (stream_progress_total_ != 0u) {
      stream_progress_total_ = stream_progress_completed_ +
                               pending_tile_loads_.size() + to_load.size();
    } else {
      stream_progress_total_ = to_load.size();
    }
    for (const auto &coord : to_load) {
      QueueTileLoad(coord);
    }
    RegisterMapWmoPlacementOwners();

    if (world_staging_workers_.GetThreadCount() == 0u) {
      PumpWorldStaging(std::numeric_limits<std::size_t>::max(),
                       std::numeric_limits<std::size_t>::max());
    }
  }

  RebuildPublicationOrder(x, y);
}

bool WorldMap::AreExistingTerrainTilesLoaded(const float min_x,
                                             const float max_x,
                                             const float min_y,
                                             const float max_y) const {
  if (min_x > max_x || min_y > max_y) {
    return false;
  }
  const TileCoord begin = WorldToTile(max_x, max_y);
  const TileCoord end = WorldToTile(min_x, min_y);
  for (std::int32_t tile_x = begin.x; tile_x <= end.x; ++tile_x) {
    for (std::int32_t tile_y = begin.y; tile_y <= end.y; ++tile_y) {
      if (tile_x < kMinTile || tile_x > kMaxTile || tile_y < kMinTile ||
          tile_y > kMaxTile) {
        continue;
      }
      if (wdt_loaded_) {
        const auto file_tile = ToMapFileTile({tile_x, tile_y});
        if (!wdt_.TileExists(static_cast<std::uint32_t>(file_tile.x),
                             static_cast<std::uint32_t>(file_tile.y))) {

          continue;
        }
      }
      if (!loaded_tiles_.contains(TileCoord{tile_x, tile_y})) {
        return false;
      }
    }
  }
  return true;
}

bool WorldMap::IsAreaResolutionSettledAt(const float x, const float y) const {
  if (map_name_.empty()) {
    return false;
  }
  if (!wdt_.has_global_wmo) {
    const TileCoord tile = WorldToTile(x, y);
    const auto file_tile = ToMapFileTile(tile);
    if (wdt_.TileExists(static_cast<uint32_t>(file_tile.x),
                        static_cast<uint32_t>(file_tile.y)) &&
        !loaded_tiles_.contains(tile)) {
      return false;
    }
  }
  return !streaming_ownership_.HasPendingWithin(x, y,
                                                kCriticalSpawnSurfaceRadius);
}

bool WorldMap::IsWorldEntryStreamingComplete() const {

  if (!IsCriticalSpawnSurfaceReady()) {
    return false;
  }
  if (!pending_tile_loads_.empty() || !pending_wmo_loads_.empty() ||
      !pending_wmo_group_loads_.empty() ||
      streaming_ownership_.HasAnyPending()) {
    return false;
  }
  for (const auto& [placement_key, instance] : wmo_instances_) {
    (void)placement_key;
    const auto cached = wmo_cache_.find(instance.wmo_path);
    if (cached == wmo_cache_.end()) {
      return false;
    }
    const auto& residency = cached->second.group_residency;
    const auto& publication = cached->second.group_gpu_publication;
    const std::size_t count = std::min(residency.size(), publication.size());
    for (std::size_t group_index = 0; group_index < count; ++group_index) {

      if (residency[group_index] == WmoGroupResidency::kUnrequested) {
        continue;
      }
      if (residency[group_index] == WmoGroupResidency::kPending) {
        return false;
      }
      if (publication[group_index] == WmoGroupPublicationStatus::kFailed) {
        continue;
      }
      if (!IsWmoGroupPublicationComplete(publication[group_index])) {
        return false;
      }
    }
  }
  return true;
}

bool WorldMap::IsCriticalSpawnSurfaceReady() const {
  if (map_name_.empty() ||
      (!wdt_.has_global_wmo && !loaded_tiles_.contains(player_tile_))) {
    return false;
  }
  if (streaming_ownership_.HasPendingWithin(
          player_x_, player_y_, kCriticalSpawnSurfaceRadius)) {
    return false;
  }
  for (const auto& [placement_key, instance] : wmo_instances_) {
    (void)placement_key;
    if (BoundsDistanceSquared3D(instance.placement_world_bounds, player_x_,
                                player_y_, player_z_) >
        kCriticalSpawnSurfaceRadius * kCriticalSpawnSurfaceRadius) {
      continue;
    }
    const auto cached = wmo_cache_.find(instance.wmo_path);
    if (cached == wmo_cache_.end()) {
      return false;
    }
    const std::size_t count = std::min(
        instance.critical_spawn_groups.size(),
        cached->second.group_residency.size());
    for (std::size_t group_index = 0; group_index < count; ++group_index) {
      if (instance.critical_spawn_groups[group_index] != 0u &&
          (cached->second.group_residency[group_index] !=
               WmoGroupResidency::kResident ||
            group_index >= cached->second.group_gpu_publication.size() ||
            !IsWmoGroupPublicationComplete(
                cached->second.group_gpu_publication[group_index]))) {
        return false;
      }
    }
  }
  return true;
}

void WorldMap::SetTimeOfDay(float normalized_time) {
  normalized_time = std::fmod(normalized_time, 1.0f);
  if (normalized_time < 0.0f) {
    normalized_time += 1.0f;
  }
  time_of_day_ = normalized_time;
  RefreshLightingEnvironment();
}

void WorldMap::SetScreenEffectLightParamSlotOverride(
    std::optional<std::uint8_t> light_param_slot_override) {
  if (screen_effect_light_param_slot_override_ == light_param_slot_override) {
    return;
  }

  screen_effect_light_param_slot_override_ = light_param_slot_override;
  RefreshLightingEnvironment();
}

void WorldMap::SetScreenEffectFogOverride(
    std::optional<WorldFogOverride> fog_override) {
  if (!fog_override.has_value()) {
    openwow::game::DayNight_ClearScreenEffectFogOverride();
    return;
  }

  openwow::game::DayNight_SetScreenEffectFogOverride(
      fog_override->start_factor, fog_override->end_distance,
      fog_override->packed_argb, fog_override->sky_dome_enabled);
}

std::uint32_t WorldMap::ResolveWmoLiquidVertexFormat(
    const std::uint32_t liquid_type, const std::uint32_t group_flags,
    const std::uint32_t liquid_type_flags) const {
  if (dbc_ == nullptr) {
    return 0u;
  }
  const std::uint32_t render_liquid_type = ResolveWmoGroupRenderLiquidType(
      liquid_type, group_flags, liquid_type_flags);
  const auto *const row =
      dbc_->liquid_type().LookupEntry(render_liquid_type);
  if (row == nullptr) {
    return 0u;
  }
  const auto *const material =
      dbc_->liquid_material().LookupEntry(row->liquid_material_id);
  return material != nullptr ? material->lvf : 0u;
}

std::optional<world::WorldLighting::LiquidDarkeningState>
WorldMap::BuildLiquidDarkeningState() const {
  if (dbc_ == nullptr) {
    return std::nullopt;
  }

  const float liquid_probe_x = has_camera_position_ ? camera_x_ : player_x_;
  const float liquid_probe_y = has_camera_position_ ? camera_y_ : player_y_;
  const float liquid_probe_z = has_camera_position_ ? camera_z_ : player_z_;
  float surface_height = 0.0f;
  std::uint32_t liquid_type_id = 0u;
  if (const auto wmo = QueryAnyWmoLiquidAtPosition(
          liquid_probe_x, liquid_probe_y, liquid_probe_z);
      wmo.has_value()) {
    surface_height = wmo->surface_height;
    liquid_type_id = wmo->liquid_type_id;
  } else {
    const auto liquid = QueryAdtLiquidAtPosition(liquid_probe_x, liquid_probe_y);
    if (!liquid.has_value() || liquid_probe_z >= liquid->height) {
      return std::nullopt;
    }
    surface_height = liquid->height;
    liquid_type_id = liquid->liquid_type_id;
  }
  if (liquid_type_id == 0u) {
    return std::nullopt;
  }

  const auto *const liquid_type = dbc_->liquid_type().LookupEntry(liquid_type_id);
  if (liquid_type == nullptr) {
    return std::nullopt;
  }

  world::WorldLighting::LiquidDarkeningState darkening{};
  darkening.surface_height_delta = liquid_probe_z - surface_height;
  darkening.max_darken_depth = liquid_type->max_darken_depth;
  darkening.fog_darken_intensity = liquid_type->fog_darken_intensity;
  darkening.ambient_darken_intensity = liquid_type->amb_darken_intensity;
  darkening.directional_darken_intensity = liquid_type->dir_darken_intensity;

  darkening.light_params_id = liquid_type->light_id;
  darkening.liquid_type_id = liquid_type_id;
  return darkening;
}

bool WorldMap::MapUsesAdvancedFog() const {

  constexpr std::uint32_t kAdvancedFogLastExcludedMapId = 529u;
  return map_id_ > kAdvancedFogLastExcludedMapId;
}

void WorldMap::PublishAdvancedFogInputs() {
  lighting_.SetAdvancedFogInputs(world::WorldLighting::AdvancedFogInputs{
      .armed = MapUsesAdvancedFog(),
      .camera_far_clip = camera_far_clip_,
  });
}

void WorldMap::RefreshLightingEnvironment() {
  if (lighting_.loaded()) {
    const auto liquid_darkening = BuildLiquidDarkeningState();

    if (liquid_darkening.has_value()) {
      openwow::game::DayNight_SetCameraLiquidState(
          -liquid_darkening->surface_height_delta, liquid_darkening->liquid_type_id);
    } else {
      openwow::game::DayNight_ClearCameraLiquidState();
    }

    const bool use_underwater_light_params = liquid_darkening.has_value();
    const auto resolved = lighting_.ResolveEnvironment(
        player_x_, player_y_, player_z_, time_of_day_,
        screen_effect_light_param_slot_override_,
        active_weather_density_,
        use_underwater_light_params, liquid_darkening,
        suppress_local_lighting_);
    const world::SkyColors &colors = resolved.sky;
    active_sky_colors_ = colors;

    openwow::game::DayNight_SetSceneVisibility(colors.scene_visibility);
    active_primary_skybox_ = resolved.primary_skybox;
    openwow::game::DayNight_ClearSkyModelSlots();
    for (std::size_t slot_index = 0;
         slot_index < resolved.skybox_slots.size(); ++slot_index) {
      const auto &slot = resolved.skybox_slots[slot_index];
      if (slot.active) {
        openwow::game::DayNight_SetSkyModelSlot(slot_index, slot.entry.model_path,
                                                slot.entry.flags, slot.alpha);
      }
    }

    outdoor_fog_ = resolved.fog;
  } else {

    openwow::game::DayNight_SetSceneVisibility(kDefaultSceneVisibility);
    openwow::game::DayNight_ClearSkyModelSlots();

    openwow::game::DayNight_ClearCameraLiquidState();
  }
}

void WorldMap::UpdateDayNightLightEnvironmentForFrame(
    const float camera_x, const float camera_y, const float camera_z,
    const std::array<float, 3> &camera_forward, const float far_clip) {

  openwow::game::DayNight_SetFogModeFromRenderPath(MapUsesAdvancedFog() ? 1 : 0);

  if (camera_far_clip_ != far_clip) {
    camera_far_clip_ = far_clip;
    PublishAdvancedFogInputs();
    RefreshLightingEnvironment();
  }

  std::array<float, 3> forward = camera_forward;
  const float forward_len_sq =
      forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2];
  if (forward_len_sq <= 1.0e-6f) {
    forward = {1.0f, 0.0f, 0.0f};
  } else {
    const float inv_len = 1.0f / std::sqrt(forward_len_sq);
    forward[0] *= inv_len;
    forward[1] *= inv_len;
    forward[2] *= inv_len;
  }

  const double elapsed_ms =
      static_cast<double>(std::max(0.0f, light_env_elapsed_seconds_)) *
      static_cast<double>(kSecondsToMilliseconds);
  const auto tick_ms = static_cast<std::uint32_t>(
      std::fmod(elapsed_ms,
                static_cast<double>(std::numeric_limits<std::uint32_t>::max())));

  const auto total_minutes = static_cast<std::int32_t>(
      std::floor(time_of_day_ * kMinutesPerDay)) % static_cast<std::int32_t>(kMinutesPerDay);
  const std::array<float, 3> camera_position{
      camera_x, camera_y, camera_z};
  const std::array<float, 3> focus_position{
      camera_x + forward[0], camera_y + forward[1], camera_z + forward[2]};

  auto& light_env = *openwow::game::DayNight_GetLightEnv();
  light_env.WriteInt32(0u, total_minutes);
  light_env.WriteFloat(1u, time_of_day_);
  light_env.WriteFloat(2u, 0.0f);
  const auto write_vec3 =
      [&light_env](const std::size_t base,
                   const std::array<float, 3>& value) {
        light_env.WriteFloat(base, value[0]);
        light_env.WriteFloat(base + 1u, value[1]);
        light_env.WriteFloat(base + 2u, value[2]);
      };
  write_vec3(3u, {player_x_, player_y_, player_z_});
  write_vec3(6u, camera_position);
  write_vec3(9u, focus_position);
  write_vec3(12u, forward);
  light_env.WriteFloat(16u, far_clip);
  light_env.WriteFloat(17u, static_cast<float>(tick_ms) * 0.001f);
  light_env.WriteFloat(18u, last_frame_delta_seconds_);
  light_env.WriteFloat(19u, active_weather_density_);
  openwow::game::DayNight_WriteOutdoorFogBand(
      light_env,
      openwow::game::DayNightFogBand{
          .packed_argb = outdoor_fog_.packed_argb,
          .start_distance = outdoor_fog_.start_distance,
          .end_distance = outdoor_fog_.end_distance,
          .density = outdoor_fog_.density,
      });

  if (lighting_.loaded()) {
    light_env.dwords[openwow::game::kLightEnvSunHaloColorIndex] =
        active_sky_colors_.colors[static_cast<std::size_t>(
            openwow::world::SkyColorSlot::kSunHalo)];
  }
  openwow::game::DayNight_ComputeLightHeadingAndGlow();
  openwow::game::DayNight_UpdateFogBands();
  openwow::game::DayNight_UpdateCloudLayerTexture();
  openwow::game::DayNight_UpdateGlobalStarsModelState();

  const openwow::game::DayNightFogBand finalized =
      openwow::game::DayNight_GetCurrentFogBand();
  fog_start_ = finalized.start_distance;
  fog_end_ = finalized.end_distance;
  fog_density_ = finalized.density;
  constexpr float kByteToFloat = 1.0f / 255.0f;
  fog_color_ = {
      static_cast<float>((finalized.packed_argb >> 16u) & 0xFFu) * kByteToFloat,
      static_cast<float>((finalized.packed_argb >> 8u) & 0xFFu) * kByteToFloat,
      static_cast<float>(finalized.packed_argb & 0xFFu) * kByteToFloat,
      static_cast<float>((finalized.packed_argb >> 24u) & 0xFFu) * kByteToFloat,
  };

  openwow::game::DayNight_SetFogModeFromRenderPath(0);
}

void WorldMap::SetSuppressLocalLighting(const bool suppress_local_lighting) {
  if (suppress_local_lighting_ == suppress_local_lighting) {
    return;
  }

  suppress_local_lighting_ = suppress_local_lighting;
  RefreshLightingEnvironment();
}

void WorldMap::SetWeather(const std::uint32_t type, const float intensity,
                          const bool smooth) {
  const openwow::data::dbc::WeatherEntry* weather_row =
      dbc_ != nullptr ? dbc_->weather().LookupEntry(type) : nullptr;

  const std::uint32_t effect_type =
      weather_row != nullptr ? weather_row->effect_type : 0u;
  active_weather_type_ =
      effect_type <= static_cast<std::uint32_t>(WeatherKind::kSandstorm)
          ? static_cast<WeatherKind>(effect_type)
          : WeatherKind::kNone;
  active_weather_density_ = std::clamp(intensity, 0.0f, 1.0f);
  active_weather_row_ =
      weather_row != nullptr
          ? std::optional<data::dbc::WeatherEntry>(*weather_row)
          : std::nullopt;
  presentation_commands_.emplace_back(SetWeatherPresentationCommand{
      .type = active_weather_type_,
      .density = active_weather_density_,
      .row = active_weather_row_,
      .smooth = smooth});
  RefreshLightingEnvironment();
}

void WorldMap::SetFileLoader(LoadFileCallback callback) {
  load_file_ = callback;
}

void WorldMap::BindDbc(const openwow::data::dbc::DbcLoader *dbc) {
  dbc_ = dbc;
  InvalidateAreaEnvironmentCache();
}

std::optional<WmoLiquidPointQueryResult>
WorldMap::QueryWmoLiquidAtPosition(const float x, const float y,
                                       const float z) const {
  const std::array<float, 3> world_position{x, y, z};
  const auto contains = [](const auto &bounds,
                           const std::array<float, 3> &point) {
    return bounds[0] <= point[0] && point[0] <= bounds[3] &&
           bounds[1] <= point[1] && point[1] <= bounds[4] &&
           bounds[2] <= point[2] && point[2] <= bounds[5];
  };

  const auto *const candidates = wmo_area_spatial_index_.Candidates(x, y);
  if (candidates == nullptr) {
    return std::nullopt;
  }

  struct PlacementRun {
    bool valid{false};
    WmoPlacementKey placement{};
    const WmoInstance *instance{nullptr};
    const CachedWmo *cached{nullptr};

    bool admitted{false};
    std::array<float, 3> local_position{};
  } run;
  for (const WmoAreaGroupRef &candidate : *candidates) {
    if (!run.valid || !(run.placement == candidate.placement)) {
      run = PlacementRun{.valid = true, .placement = candidate.placement};
      const auto instance_it = wmo_instances_.find(candidate.placement);
      if (instance_it == wmo_instances_.end()) {
        continue;
      }
      run.instance = &instance_it->second;
      if (!contains(run.instance->placement_world_bounds, world_position)) {
        continue;
      }
      const auto cached_it = wmo_cache_.find(run.instance->wmo_path);
      if (cached_it == wmo_cache_.end()) {
        continue;
      }
      run.cached = &cached_it->second;
      const Vec3 transformed =
          TransformPoint(run.instance->inverse_model_matrix, world_position);
      run.local_position = {transformed[0], transformed[1], transformed[2]};
      const auto &root_header = run.cached->root.header;
      const std::array<float, 6> root_bounds{
          root_header.boundingBox1[0], root_header.boundingBox1[1],
          root_header.boundingBox1[2], root_header.boundingBox2[0],
          root_header.boundingBox2[1], root_header.boundingBox2[2]};
      if (!contains(root_bounds, run.local_position)) {
        continue;
      }
      run.admitted = true;
    }
    if (!run.admitted) {
      continue;
    }
    const WmoInstance &instance = *run.instance;
    const CachedWmo &cached = *run.cached;
    const std::array<float, 3> &local_position = run.local_position;
    const std::size_t group_index = candidate.group_index;
    if (group_index >= cached.root.groupInfos.size() ||
        group_index >= cached.groups.size() ||
        group_index >= cached.group_residency.size() ||
        cached.group_residency[group_index] != WmoGroupResidency::kResident) {
      continue;
    }

    const data::wmo::WmoGroup &group = cached.groups[group_index];

    if ((group.header.flags & data::wmo::kMogpIndoor) != 0u) {
      continue;
    }
    const std::array<float, 6> group_bounds{
        group.header.boundingBox1[0], group.header.boundingBox1[1],
        group.header.boundingBox1[2], group.header.boundingBox2[0],
        group.header.boundingBox2[1], group.header.boundingBox2[2]};
    if (!contains(group_bounds, local_position)) {
      continue;
    }

    const std::uint32_t liquid_type =
        ResolveWmoGroupLiquidType(cached.root, group);
    const auto *const liquid_type_row =
        dbc_ != nullptr ? dbc_->liquid_type().LookupEntry(liquid_type) : nullptr;
    const std::uint32_t liquid_type_flags =
        liquid_type_row != nullptr ? liquid_type_row->flags : 0u;
    const auto local_result = QueryWmoGroupLiquidAtLocalPosition(
        group, liquid_type, local_position, liquid_type_flags);
    if (!local_result.has_value()) {
      continue;
    }

    const Vec3 world_surface = TransformPoint(
        instance.model_matrix,
        {local_position[0], local_position[1], local_result->surface_height});
    return WmoLiquidPointQueryResult{
        .liquid_type_id = local_result->liquid_type_id,
        .surface_height = world_surface[2],
    };
  }

  return std::nullopt;
}

std::optional<WmoLiquidPointQueryResult>
WorldMap::QueryWmoInteriorLiquidAtPosition(const float x, const float y,
                                           const float z) const {

  {
    const auto* const candidates = wmo_area_spatial_index_.Candidates(x, y);
    if (candidates == nullptr) {

      return std::nullopt;
    }
    const float room_segment_min_z = z - kWmoRoomQueryDepth;
    const float room_segment_max_z = z + kWmoRoomQueryDepth;
    const auto group_has_liquid_here =
        [&](const CachedWmo& cached, const std::size_t group_index,
            const std::array<float, 3>& local_position) {
          if (group_index >= cached.groups.size() ||
              group_index >= cached.group_residency.size() ||
              cached.group_residency[group_index] !=
                  WmoGroupResidency::kResident) {
            return false;
          }
          const data::wmo::WmoGroup& group = cached.groups[group_index];
          const std::uint32_t liquid_type =
              ResolveWmoGroupLiquidType(cached.root, group);
          if (liquid_type == 0u) {
            return false;
          }
          const auto* const liquid_type_row =
              dbc_ != nullptr ? dbc_->liquid_type().LookupEntry(liquid_type)
                              : nullptr;
          return QueryWmoGroupLiquidAtLocalPosition(
                     group, liquid_type, local_position,
                     liquid_type_row != nullptr ? liquid_type_row->flags : 0u)
              .has_value();
        };

    bool may_have_interior_liquid = false;

    struct PlacementRun {
      bool valid{false};
      WmoPlacementKey placement{};
      const WmoInstance* instance{nullptr};
      bool cache_resolved{false};
      const CachedWmo* cached{nullptr};
      bool transformed{false};
      std::array<float, 3> local_position{};
    } run;
    for (const WmoAreaGroupRef& candidate : *candidates) {
      if (!run.valid || !(run.placement == candidate.placement)) {
        run = PlacementRun{.valid = true, .placement = candidate.placement};
        const auto instance_it = wmo_instances_.find(candidate.placement);
        if (instance_it != wmo_instances_.end()) {
          run.instance = &instance_it->second;
        }
      }
      if (run.instance == nullptr) {
        continue;
      }
      const WmoInstance& instance = *run.instance;
      if (candidate.group_index >= instance.group_world_bounds.size()) {
        continue;
      }
      const auto& bounds = instance.group_world_bounds[candidate.group_index];
      if (x < bounds[0] || x > bounds[3] || y < bounds[1] || y > bounds[4] ||
          room_segment_max_z < bounds[2] || room_segment_min_z > bounds[5]) {
        continue;
      }
      if (!run.cache_resolved) {
        run.cache_resolved = true;
        const auto cache_it = wmo_cache_.find(instance.wmo_path);
        if (cache_it != wmo_cache_.end()) {
          run.cached = &cache_it->second;
        }
      }
      if (run.cached == nullptr) {
        continue;
      }
      const CachedWmo& cached = *run.cached;
      if (candidate.group_index >= cached.groups.size() ||
          candidate.group_index >= cached.group_residency.size() ||
          cached.group_residency[candidate.group_index] !=
              WmoGroupResidency::kResident) {
        continue;
      }
      const data::wmo::WmoGroup& group = cached.groups[candidate.group_index];
      if ((group.header.flags & kRetailRoomExcludedGroupFlags) != 0u) {
        continue;
      }
      if (!run.transformed) {
        run.transformed = true;
        const Vec3 transformed =
            TransformPoint(instance.inverse_model_matrix, {x, y, z});
        run.local_position = {transformed[0], transformed[1], transformed[2]};
      }
      const std::array<float, 3>& local_position = run.local_position;
      if (group_has_liquid_here(cached, candidate.group_index,
                                local_position)) {
        may_have_interior_liquid = true;
        break;
      }
      if ((group.header.flags & data::wmo::kMogpExterior) != 0u) {
        continue;
      }
      const std::size_t portal_begin = group.header.portalStart;
      const std::size_t portal_end =
          std::min(cached.root.portalRefs.size(),
                   portal_begin + group.header.portalCount);
      for (std::size_t portal_index = portal_begin; portal_index < portal_end;
           ++portal_index) {
        const auto& portal_ref = cached.root.portalRefs[portal_index];
        if (portal_ref.groupIndex >= cached.root.groupInfos.size() ||
            portal_ref.groupIndex == candidate.group_index) {
          continue;
        }
        if (group_has_liquid_here(cached, portal_ref.groupIndex,
                                  local_position)) {
          may_have_interior_liquid = true;
          break;
        }
      }
      if (may_have_interior_liquid) {
        break;
      }
    }
    if (!may_have_interior_liquid) {
      return std::nullopt;
    }
  }
  (void)ResolveAreaEnvironmentAtPosition(x, y, z,
                                         AreaEnvironmentProbe::kUnitSurface);
  if (!last_area_environment_resolution_.containing_group.has_value()) {
    return std::nullopt;
  }
  const WmoAreaGroupRef &ref =
      *last_area_environment_resolution_.containing_group;
  const auto instance_it = wmo_instances_.find(ref.placement);
  if (instance_it == wmo_instances_.end()) {
    return std::nullopt;
  }
  const WmoInstance &instance = instance_it->second;
  const auto cache_it = wmo_cache_.find(instance.wmo_path);
  if (cache_it == wmo_cache_.end()) {
    return std::nullopt;
  }
  const CachedWmo &cached = cache_it->second;
  const std::size_t group_index = ref.group_index;
  if (group_index >= cached.groups.size() ||
      group_index >= cached.group_residency.size() ||
      cached.group_residency[group_index] != WmoGroupResidency::kResident) {
    return std::nullopt;
  }

  const data::wmo::WmoGroup &group = cached.groups[group_index];
  const Vec3 transformed =
      TransformPoint(instance.inverse_model_matrix, {x, y, z});
  const std::array<float, 3> local_position{transformed[0], transformed[1],
                                            transformed[2]};
  const std::uint32_t liquid_type =
      ResolveWmoGroupLiquidType(cached.root, group);
  const auto *const liquid_type_row =
      dbc_ != nullptr ? dbc_->liquid_type().LookupEntry(liquid_type) : nullptr;
  const auto local_result = QueryWmoGroupLiquidAtLocalPosition(
      group, liquid_type, local_position,
      liquid_type_row != nullptr ? liquid_type_row->flags : 0u);
  if (!local_result.has_value()) {
    return std::nullopt;
  }
  const Vec3 world_surface = TransformPoint(
      instance.model_matrix,
      {local_position[0], local_position[1], local_result->surface_height});
  return WmoLiquidPointQueryResult{
      .liquid_type_id = local_result->liquid_type_id,
      .surface_height = world_surface[2],
  };
}

std::optional<WmoLiquidPointQueryResult>
WorldMap::QueryAnyWmoLiquidAtPosition(const float x, const float y,
                                      const float z) const {

  if (const auto interior = QueryWmoInteriorLiquidAtPosition(x, y, z);
      interior.has_value()) {
    return interior;
  }
  return QueryWmoLiquidAtPosition(x, y, z);
}

std::optional<WmoLiquidPointQueryResult> WorldMap::QueryLiquidSurfaceAtPosition(
    const float x, const float y, const float z) const {
  if (const auto wmo = QueryAnyWmoLiquidAtPosition(x, y, z); wmo.has_value()) {
    return wmo;
  }

  const auto liquid = QueryAdtLiquidAtPosition(x, y);
  if (!liquid.has_value() || liquid->height + 0.01f <= z) {
    return std::nullopt;
  }
  return WmoLiquidPointQueryResult{
      .liquid_type_id = liquid->liquid_type_id,
      .surface_height = liquid->height,
  };
}

std::uint32_t WorldMap::GetUnderwaterLiquidTypeId(const float x,
                                                      const float y,
                                                      const float z) const {
  const auto liquid = QueryLiquidSurfaceAtPosition(x, y, z);
  return liquid.has_value() ? liquid->liquid_type_id : 0u;
}

std::optional<float> WorldMap::GetLiquidSurfaceHeightAtPosition(
    const float x, const float y, const float z) const {
  const auto liquid = QueryLiquidSurfaceAtPosition(x, y, z);
  return liquid.has_value() ? std::optional<float>{liquid->surface_height}
                            : std::nullopt;
}

std::vector<WorldLiquidSoundSource> WorldMap::QueryLiquidSoundSources(
    const float x, const float y, const float z, const float radius) const {
  std::vector<WorldLiquidSoundSource> sources;
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
      !std::isfinite(radius) || radius <= 0.0f) {
    return sources;
  }

  const float radius_squared = radius * radius;
  const auto append_nearest_cell = [&](const WaterHeightfield& surface) {
    if (surface.liquid_type_id == 0u || surface.vertex_rows < 2u ||
        surface.vertex_cols < 2u) {
      return;
    }

    const bool has_world_positions =
        surface.positions.size() ==
        static_cast<std::size_t>(surface.vertex_rows) * surface.vertex_cols;
    float best_distance_squared = radius_squared;
    std::optional<std::array<float, 3>> best_position;
    for (std::uint32_t row = 0u; row + 1u < surface.vertex_rows; ++row) {
      for (std::uint32_t column = 0u; column + 1u < surface.vertex_cols;
           ++column) {
        if (!IsWaterHeightfieldCellRendered(surface, row, column)) {
          continue;
        }

        std::array<float, 3> candidate{};
        if (has_world_positions) {
          const auto& p00 = surface.positions[
              WaterHeightfieldVertexIndex(surface, row, column)];
          const auto& p10 = surface.positions[
              WaterHeightfieldVertexIndex(surface, row, column + 1u)];
          const auto& p01 = surface.positions[
              WaterHeightfieldVertexIndex(surface, row + 1u, column)];
          const auto& p11 = surface.positions[
              WaterHeightfieldVertexIndex(surface, row + 1u, column + 1u)];
          candidate = {(p00[0] + p10[0] + p01[0] + p11[0]) * 0.25f,
                       (p00[1] + p10[1] + p01[1] + p11[1]) * 0.25f,
                       (p00[2] + p10[2] + p01[2] + p11[2]) * 0.25f};
        } else {
          const std::size_t i00 =
              WaterHeightfieldVertexIndex(surface, row, column);
          const std::size_t i10 =
              WaterHeightfieldVertexIndex(surface, row, column + 1u);
          const std::size_t i01 =
              WaterHeightfieldVertexIndex(surface, row + 1u, column);
          const std::size_t i11 =
              WaterHeightfieldVertexIndex(surface, row + 1u, column + 1u);
          if (i11 >= surface.heights.size()) {
            continue;
          }
          candidate = {
              surface.origin_x + (static_cast<float>(row) + 0.5f) * surface.step_x,
              surface.origin_z + (static_cast<float>(column) + 0.5f) * surface.step_z,
              (surface.heights[i00] + surface.heights[i10] +
               surface.heights[i01] + surface.heights[i11]) * 0.25f};
        }

        const float dx = candidate[0] - x;
        const float dy = candidate[1] - y;
        const float distance_squared = dx * dx + dy * dy;
        if (distance_squared <= best_distance_squared) {
          best_distance_squared = distance_squared;
          best_position = candidate;
        }
      }
    }

    if (best_position.has_value()) {
      sources.push_back({surface.liquid_type_id, *best_position});
    }
  };

  const auto tile_index = [](const float coordinate) {
    return std::clamp(static_cast<std::int32_t>(
                          std::floor(32.0f - coordinate / kTileSize)),
                      0, 63);
  };
  const std::int32_t tile_x_begin = tile_index(x + radius);
  const std::int32_t tile_x_end = tile_index(x - radius);
  const std::int32_t tile_y_begin = tile_index(y + radius);
  const std::int32_t tile_y_end = tile_index(y - radius);
  for (std::int32_t tile_x = tile_x_begin; tile_x <= tile_x_end; ++tile_x) {
    for (std::int32_t tile_y = tile_y_begin; tile_y <= tile_y_end; ++tile_y) {
      const auto tile = loaded_tiles_.find(TileCoord{tile_x, tile_y});
      if (tile == loaded_tiles_.end() || !tile->second) {
        continue;
      }
      for (const WaterHeightfield& surface : tile->second->water_surfaces) {
        append_nearest_cell(surface);
      }
    }
  }

  const std::array<float, 6> query_bounds{
      x - radius, y - radius, -std::numeric_limits<float>::max(),
      x + radius, y + radius, std::numeric_limits<float>::max()};
  for (const WmoAreaGroupRef& candidate :
       wmo_area_spatial_index_.Candidates(query_bounds)) {
    const auto instance = wmo_instances_.find(candidate.placement);
    if (instance == wmo_instances_.end() ||
        candidate.group_index >= instance->second.group_liquid_surfaces.size()) {
      continue;
    }
    const auto& surface =
        instance->second.group_liquid_surfaces[candidate.group_index];
    if (surface.has_value()) {
      append_nearest_cell(*surface);
    }
  }
  return sources;
}

std::optional<WaterHeightfieldSample>
WorldMap::QueryAdtLiquidAtPosition(const float x, const float y) const {
  const auto tile = loaded_tiles_.find(WorldToTile(x, y));
  if (tile == loaded_tiles_.end() || !tile->second) {
    return std::nullopt;
  }
  for (const WaterHeightfield& surface : tile->second->water_surfaces) {
    if (const auto sample = SampleWaterHeightfield(surface, x, y);
        sample.has_value()) {
      return sample;
    }
  }
  return std::nullopt;
}

bool WorldMap::IsPositionInTerrainHole(const float x, const float y) const {

  const float local_y = -(y - kGroundTypeQueryMapHalfSize);
  const float local_x = -(x - kGroundTypeQueryMapHalfSize);
  if (local_y < 0.0f || local_x < 0.0f || local_y >= kGroundTypeQueryMapFullSize ||
      local_x >= kGroundTypeQueryMapFullSize) {
    return false;
  }

  const TileCoord tile_coord = WorldToTile(x, y);
  const auto tile_it = loaded_tiles_.find(tile_coord);
  if (tile_it == loaded_tiles_.end() || tile_it->second == nullptr) {
    return false;
  }

  const WorldAddress address = openwow::world::WorldToAddress(x, y);
  const std::size_t chunk_index = TerrainChunkStorageIndex(address.chunk);
  if (chunk_index >= tile_it->second->adt.chunks.size()) {
    return false;
  }

  const auto &chunk = tile_it->second->adt.chunks[chunk_index];
  return IsTerrainHoleCell(chunk.holes, address.cell.x, address.cell.y);
}

std::uint32_t WorldMap::ResolveTerrainGroundTypeAtPosition(const float x, const float y) const {
  const float local_y = -(y - kGroundTypeQueryMapHalfSize);
  const float local_x = -(x - kGroundTypeQueryMapHalfSize);
  if (local_y < 0.0f || local_x < 0.0f || local_y >= kGroundTypeQueryMapFullSize ||
      local_x >= kGroundTypeQueryMapFullSize || dbc_ == nullptr) {
    return 0u;
  }

  const TileCoord tile_coord = WorldToTile(x, y);
  const auto tile_it = loaded_tiles_.find(tile_coord);
  if (tile_it == loaded_tiles_.end() || tile_it->second == nullptr) {
    return 0u;
  }

  const WorldAddress address = openwow::world::WorldToAddress(x, y);
  const std::size_t chunk_index = TerrainChunkStorageIndex(address.chunk);
  const std::size_t cell_index = TerrainCellStorageIndex(address.cell);
  if (chunk_index >= tile_it->second->terrain_cell_ground_effect_ids.size() ||
      cell_index >= tile_it->second->terrain_cell_ground_effect_ids[chunk_index].size()) {
    return 0u;
  }

  const std::uint32_t effect_id =
      tile_it->second->terrain_cell_ground_effect_ids[chunk_index][cell_index];
  return ResolveGroundTypeFromEffectId(*dbc_, effect_id);
}

std::uint32_t WorldMap::ResolveAreaIdAtPosition(const float x, const float y,
                                                    const float z) const {
  return ResolveAreaEnvironmentContextAtPosition(x, y, z).area_id;
}

bool WorldMap::IsPositionInSnowArea(const float x, const float y, const float z) const {
  return dbc_ != nullptr && data::dbc::IsAreaFlaggedSnow(
                             *dbc_, ResolveAreaEnvironmentContextAtPosition(x, y, z).area_id);
}

bool WorldMap::IsOutdoorsAtPosition(const float x, const float y, const float z) const {
  return ResolveAreaEnvironmentContextAtPosition(x, y, z).outdoors;
}

AreaEnvironmentContext WorldMap::ResolveAreaEnvironmentContextAtPosition(
    const float x, const float y, const float z) const {
  return ResolveAreaEnvironmentAtPosition(x, y, z,
                                         AreaEnvironmentProbe::kUnitSurface);
}

WorldMap::ResolvedWmoAreaRows WorldMap::ResolveWmoAreaRowsForGroup(
    const WmoAreaGroupRef &ref) const {
  ResolvedWmoAreaRows rows{};
  const auto instance_it = wmo_instances_.find(ref.placement);
  if (instance_it == wmo_instances_.end()) {
    return rows;
  }
  const WmoInstance &instance = instance_it->second;
  const auto cache_it = wmo_cache_.find(instance.wmo_path);
  if (cache_it == wmo_cache_.end()) {
    return rows;
  }
  const CachedWmo &cached = cache_it->second;
  const std::size_t group_index = ref.group_index;
  if (group_index >= cached.groups.size() ||
      group_index >= cached.root.groupInfos.size()) {
    return rows;
  }

  rows.resolved = true;
  rows.group_resident = group_index < cached.group_residency.size() &&
                        cached.group_residency[group_index] ==
                            WmoGroupResidency::kResident;
  rows.group_flags = rows.group_resident
                         ? cached.groups[group_index].header.flags
                         : cached.root.groupInfos[group_index].flags;

  constexpr std::int32_t kWmoAreaTableRootGroupId = -1;
  rows.root = openwow::data::DBClient_FindWmoAreaTable(
      dbc_, cached.root_wmo_id, instance.name_set, kWmoAreaTableRootGroupId);
  if (rows.group_resident) {
    rows.group = openwow::data::DBClient_FindWmoAreaTable(
        dbc_, cached.root_wmo_id, instance.name_set,
        static_cast<std::int32_t>(cached.groups[group_index].header.wmoGroupID));
  }
  return rows;
}

MoverWmoSoundContext WorldMap::ResolveWmoSoundContextAtPosition(
    const float x, const float y, const float z) const {

  (void)ResolveAreaEnvironmentAtPosition(x, y, z,
                                         AreaEnvironmentProbe::kUnitSurface);
  MoverWmoSoundContext context{};
  if (!last_area_environment_resolution_.containing_group.has_value()) {
    return context;
  }
  const ResolvedWmoAreaRows rows = ResolveWmoAreaRowsForGroup(
      *last_area_environment_resolution_.containing_group);
  if (!rows.resolved) {
    return context;
  }

  const auto terms = [](const data::dbc::WMOAreaTableEntry *const row) {
    WmoAreaSoundTerms selection{};
    if (row == nullptr) {
      return selection;
    }
    selection.wmo_area_id = row->id;
    selection.sound_provider_pref = row->sound_pref;
    selection.sound_provider_pref_uw = row->sound_pref_uw;
    selection.sound_ambience_id = row->ambience_id;
    selection.zone_music_id = row->zone_music;
    selection.zone_intro_music_id = row->intro_sound;
    return selection;
  };
  context.group = terms(rows.group);
  context.root = terms(rows.root);

  context.interior_group =
      (rows.group_flags & data::wmo::kMogpExterior) == 0u;
  return context;
}

namespace {

MoverWmoSoundContext g_active_mover_wmo_sound_context{};

}

void PublishActiveMoverWmoSoundContext(
    const MoverWmoSoundContext &context) noexcept {
  g_active_mover_wmo_sound_context = context;
}

const MoverWmoSoundContext &ActiveMoverWmoSoundContext() noexcept {
  return g_active_mover_wmo_sound_context;
}

void WorldMap::InvalidateAreaEnvironmentCache() noexcept {

  ++area_environment_generation_;
  if (area_environment_generation_ == 0u) {
    area_environment_generation_ = 1u;
  }
  last_area_environment_resolution_.valid = false;
}

void WorldMap::InvalidateAreaEnvironmentCacheInBounds(
    const std::array<float, 6>& world_bounds) noexcept {

  const auto inside = [&](const AreaEnvironmentQueryCache& entry) {

    return entry.valid && entry.generation == area_environment_generation_ &&
           entry.x >= world_bounds[0] && entry.x <= world_bounds[3] &&
           entry.y >= world_bounds[1] && entry.y <= world_bounds[4] &&
           entry.z >= world_bounds[2] && entry.z <= world_bounds[5];
  };
  for (auto& entry : area_environment_query_cache_) {
    if (inside(entry)) {
      entry.valid = false;
    }
  }
  if (inside(last_area_environment_resolution_)) {
    last_area_environment_resolution_.valid = false;
  }
}

void WorldMap::AddWmoAreaGroupToIndex(
    const WmoPlacementKey &placement_key, const WmoInstance &instance,
    const std::size_t group_index) {
  if (group_index >= instance.group_world_bounds.size()) {
    return;
  }
  wmo_area_spatial_index_.Add(
      WmoAreaGroupRef{placement_key, static_cast<std::uint32_t>(group_index)},
      instance.group_world_bounds[group_index]);
  InvalidateAreaEnvironmentCache();
}

AreaEnvironmentContext
WorldMap::ResolveAreaEnvironmentAtPosition(
    const float x, const float y, const float z,
    const AreaEnvironmentProbe probe) const {
  const auto query_cell = WmoAreaSpatialIndex::CellFor(x, y);
  const std::size_t query_slot =
      AreaEnvironmentQuerySlot(x, y, z, kAreaEnvironmentQueryCacheSize);
  {
    const AreaEnvironmentQueryCache& entry =
        area_environment_query_cache_[query_slot];
    if (entry.valid && entry.generation == area_environment_generation_ &&
        entry.probe == probe && entry.cell == query_cell && entry.x == x &&
        entry.y == y && entry.z == z) {

      last_area_environment_resolution_ = entry;
      return entry.resolution;
    }
  }

  AreaEnvironmentContext result{};
  result.outdoors = true;
  result.area_id = 0u;

  const LoadedTile *const query_tile = [&]() -> const LoadedTile * {
    const auto tile_it = loaded_tiles_.find(WorldToTile(x, y));
    return tile_it == loaded_tiles_.end() ? nullptr : tile_it->second.get();
  }();
  const WorldAddress query_address = openwow::world::WorldToAddress(x, y);
  const std::size_t query_chunk_index =
      TerrainChunkStorageIndex(query_address.chunk);

  auto get_terrain_height = [&]() -> std::optional<float> {
    const float local_y = -(y - kGroundTypeQueryMapHalfSize);
    const float local_x = -(x - kGroundTypeQueryMapHalfSize);
    if (local_y < 0.0f || local_x < 0.0f ||
        local_y >= kGroundTypeQueryMapFullSize ||
        local_x >= kGroundTypeQueryMapFullSize) {
      return std::nullopt;
    }

    if (query_tile == nullptr) {
      return std::nullopt;
    }

    const WorldAddress &address = query_address;
    if (query_chunk_index >= query_tile->adt.chunks.size()) {
      return std::nullopt;
    }

    const auto &chunk = query_tile->adt.chunks[query_chunk_index];
    if (IsTerrainHoleCell(chunk.holes, address.cell.x, address.cell.y)) {
      return std::nullopt;
    }

    return chunk.header.position_z +
           InterpolateTerrainHeightDelta(chunk.heights,
                                         address.cell.x, address.cell.y,
                                         address.offsetX, address.offsetY);
  };

  auto get_water_height = [&]() -> std::optional<float> {
    if (query_tile == nullptr) {
      return std::nullopt;
    }

    if (query_chunk_index >= query_tile->adt.water_chunks.size()) {
      return std::nullopt;
    }

    const auto &water_chunk = query_tile->adt.water_chunks[query_chunk_index];
    float highest = -std::numeric_limits<float>::infinity();
    bool found = false;
    for (const auto &layer : water_chunk.layers) {
      if (layer.max_height > highest) {
        highest = layer.max_height;
        found = true;
      }
    }
    return found ? std::optional<float>(highest) : std::nullopt;
  };

  const auto terrain_z = get_terrain_height();
  const auto water_z = get_water_height();
  float nearest_surface_z = -std::numeric_limits<float>::infinity();
  std::uint32_t best_area_id = 0u;
  bool best_is_wmo = false;
  bool best_outdoors = true;
  std::optional<WmoAreaGroupRef> best_group;
  std::array<WmoRoomHitLane, 2u> room_lanes{};

  struct PlacementRun {
    bool valid{false};
    WmoPlacementKey placement{};
    const WmoInstance *instance{nullptr};
    bool cache_resolved{false};
    const CachedWmo *cached{nullptr};
  } inspect_run;

  bool room_query_incomplete = false;
  const auto inspect_group = [&](const WmoAreaGroupRef &ref,
                                 const float segment_start_z,
                                 const float segment_end_z) {
    if (!inspect_run.valid || !(inspect_run.placement == ref.placement)) {
      inspect_run = PlacementRun{.valid = true, .placement = ref.placement};
      const auto instance_it = wmo_instances_.find(ref.placement);
      if (instance_it != wmo_instances_.end()) {
        inspect_run.instance = &instance_it->second;
      }
    }
    if (inspect_run.instance == nullptr) {
      return;
    }
    const WmoInstance &instance = *inspect_run.instance;
    const std::size_t group_index = ref.group_index;
    if (group_index >= instance.group_world_bounds.size()) {
      return;
    }
    const auto &world_bounds = instance.group_world_bounds[group_index];
    if (x < world_bounds[0] || x > world_bounds[3] ||
        y < world_bounds[1] || y > world_bounds[4] ||
        std::max(segment_start_z, segment_end_z) < world_bounds[2] ||
        std::min(segment_start_z, segment_end_z) > world_bounds[5]) {
      return;
    }
    if (!inspect_run.cache_resolved) {
      inspect_run.cache_resolved = true;
      const auto cache_it = wmo_cache_.find(instance.wmo_path);
      if (cache_it != wmo_cache_.end()) {
        inspect_run.cached = &cache_it->second;
      }
    }
    if (inspect_run.cached == nullptr) {
      return;
    }
    const CachedWmo &cached = *inspect_run.cached;
    if (group_index >= cached.groups.size() ||
        group_index >= cached.group_residency.size() ||
        group_index >= cached.area_triangle_indices.size() ||
        cached.group_residency[group_index] != WmoGroupResidency::kResident) {

      room_query_incomplete = true;
      return;
    }
    const data::wmo::WmoGroup &group = cached.groups[group_index];
    if ((group.header.flags & kRetailRoomExcludedGroupFlags) != 0u) {
      return;
    }
    const Vec3 local_start =
        TransformPoint(instance.inverse_model_matrix, {x, y, segment_start_z});
    const Vec3 local_end = TransformPoint(instance.inverse_model_matrix,
                                          {x, y, segment_end_z});
    const std::size_t lane_index =
        RetailWmoRoomPlacementSlot(instance.placement_flags);
    WmoRoomHitLane &lane = room_lanes[lane_index];
    WmoRoomHit group_primary;
    WmoRoomHit group_secondary;
    cached.area_triangle_indices[group_index].VisitRoomSegmentFractions(
        group, local_start, local_end,
        [&](const float fraction) {
          AccumulateWmoRoomHit(group_primary, ref, fraction);
        },
        [&](const float fraction) {
          AccumulateWmoRoomHit(group_secondary, ref, fraction);
        });
    std::optional<WmoAreaGroupRef> portal_secondary_room;
    if (const auto correction = CorrectWmoRoomHitThroughPortal(
            cached.root, group, ref, local_start, local_end,
            group_primary.fraction);
        correction.has_value()) {

      group_primary.fraction = correction->fraction;
      group_primary.room = correction->primary;
      portal_secondary_room = correction->secondary;
    }

    const bool primary_accepted =
        group_primary.room.has_value() &&
        AccumulateWmoRoomHit(lane.primary, *group_primary.room,
                             group_primary.fraction);

    if (probe == AreaEnvironmentProbe::kCameraRoom) {

      if (primary_accepted) {
        if (portal_secondary_room.has_value()) {
          lane.secondary.fraction = group_primary.fraction;
          lane.secondary.room = portal_secondary_room;
        } else {
          lane.secondary = WmoRoomHit{};
        }
      }
    } else if (group_secondary.room.has_value()) {
      AccumulateWmoRoomHit(lane.secondary, *group_secondary.room,
                           group_secondary.fraction);
    }
  };

  const auto* candidates = wmo_area_spatial_index_.Candidates(x, y);
  const bool camera_room_probe = probe == AreaEnvironmentProbe::kCameraRoom;

  float room_segment_start_z =
      camera_room_probe ? z : z + kWmoRoomQueryStartLift;
  float room_segment_end_z =
      camera_room_probe ? z - kRetailCameraRoomRayDepth : z - kWmoRoomQueryDepth;
  if (candidates != nullptr) {
    for (const WmoAreaGroupRef &candidate : *candidates) {
      inspect_group(candidate, room_segment_start_z, room_segment_end_z);
    }
  }

  const bool has_downward_wmo_hit = std::any_of(
      room_lanes.begin(), room_lanes.end(), [](const WmoRoomHitLane &lane) {
        return lane.primary.room.has_value() ||
               lane.secondary.room.has_value();
      });
  if (!camera_room_probe && !has_downward_wmo_hit && !terrain_z.has_value() &&
      candidates != nullptr) {
    room_lanes = {};
    room_segment_start_z = z;
    room_segment_end_z = z + kWmoRoomQueryDepth;
    for (const WmoAreaGroupRef& candidate : *candidates) {
      inspect_group(candidate, room_segment_start_z, room_segment_end_z);
    }
  }

  NormalizeRetailWmoRoomHits(room_lanes);
  const WmoRoomHitLane &primary_lane = room_lanes[0];
  const WmoRoomHitLane &secondary_lane = room_lanes[1];
  if (primary_lane.primary.room.has_value()) {
    best_group = primary_lane.primary.room;
    best_is_wmo = true;
    nearest_surface_z =
        room_segment_start_z +
        primary_lane.primary.fraction *
        (room_segment_end_z - room_segment_start_z);
    const ResolvedWmoAreaRows wmo_rows =
        ResolveWmoAreaRowsForGroup(*best_group);
    if (wmo_rows.resolved) {

      const data::dbc::WMOAreaTableEntry *const naming_row =
          wmo_rows.group != nullptr ? wmo_rows.group : wmo_rows.root;
      best_area_id = naming_row != nullptr ? naming_row->area_table_id : 0u;

      best_outdoors = (wmo_rows.group_flags & data::wmo::kMogpExterior) != 0u;
      if (naming_row != nullptr) {
        constexpr std::uint32_t kWmoAreaFlagForceOutdoor = 0x4u;
        constexpr std::uint32_t kWmoAreaFlagForceIndoor = 0x2u;
        if ((naming_row->flags & kWmoAreaFlagForceOutdoor) != 0u) {
          best_outdoors = true;
        } else if ((naming_row->flags & kWmoAreaFlagForceIndoor) != 0u) {
          best_outdoors = false;
        }
      }
    }
  }

  const float query_z = z;

  if (water_z.has_value() && water_z.value() < query_z &&
      water_z.value() > nearest_surface_z) {
    nearest_surface_z = water_z.value();
    best_is_wmo = false;
  }

  if (terrain_z.has_value() && terrain_z.value() < query_z &&
      terrain_z.value() > nearest_surface_z) {
    nearest_surface_z = terrain_z.value();
    best_is_wmo = false;
  }

  result.has_wmo_context = best_is_wmo;
  const auto cache_result = [&](const AreaEnvironmentContext resolved) {

    const auto entry = AreaEnvironmentQueryCache{
        .generation = area_environment_generation_,
        .x = x,
        .y = y,
        .z = z,
        .probe = probe,
        .cell = query_cell,
        .resolution = resolved,
        .containing_group = best_is_wmo ? best_group : std::nullopt,
        .alternate_group =
            best_is_wmo ? primary_lane.secondary.room : std::nullopt,
        .secondary_group =
            best_is_wmo ? secondary_lane.primary.room : std::nullopt,
        .secondary_alternate_group =
            best_is_wmo ? secondary_lane.secondary.room : std::nullopt,
        .valid = true,
    };

    if (probe == AreaEnvironmentProbe::kCameraRoom && room_query_incomplete &&
        !best_is_wmo && last_named_camera_room_.valid) {
      auto retained = entry;

      retained.resolution = last_named_camera_room_.resolution;
      retained.containing_group = last_named_camera_room_.containing_group;
      retained.alternate_group = last_named_camera_room_.alternate_group;
      retained.secondary_group = last_named_camera_room_.secondary_group;
      retained.secondary_alternate_group =
          last_named_camera_room_.secondary_alternate_group;

      auto pending = retained;
      pending.valid = false;
      area_environment_query_cache_[query_slot] = pending;
      last_area_environment_resolution_ = retained;

      return retained.resolution;
    }
    area_environment_query_cache_[query_slot] = entry;
    last_area_environment_resolution_ = entry;
    if (probe == AreaEnvironmentProbe::kCameraRoom && best_is_wmo) {
      last_named_camera_room_ = entry;
    }
    return resolved;
  };

  if (best_is_wmo) {
    result.depth = std::max(0.0f, z - nearest_surface_z);
    if (best_area_id != 0u) {
      result.area_id = best_area_id;
      result.outdoors = best_outdoors;
      return cache_result(result);
    }

    result.outdoors = best_outdoors;
  }

  if (terrain_z.has_value()) {
    if (query_tile != nullptr) {
      if (query_chunk_index < query_tile->adt.chunks.size()) {
        result.area_id =
            query_tile->adt.chunks[query_chunk_index].header.area_id;
        if (!best_is_wmo) {

          constexpr std::uint32_t kAreaFlagIndoors = 0x02000000u;
          constexpr std::uint32_t kAreaFlagOutdoorsOverride = 0x04000000u;
          const auto *const area_row =
              dbc_ != nullptr ? dbc_->area_table().LookupEntry(result.area_id)
                              : nullptr;
          result.outdoors =
              area_row == nullptr ||
              (area_row->flags & kAreaFlagIndoors) == 0u ||
              (area_row->flags & kAreaFlagOutdoorsOverride) != 0u;
        }
        return cache_result(result);
      }
    }
  }

  return cache_result(result);
}

std::optional<WorldMap::CameraWmoFogResolution> WorldMap::ResolveCameraWmoFog(
    const float x, const float y, const float z,
    const float far_clip) const {

  (void)ResolveAreaEnvironmentAtPosition(x, y, z,
                                         AreaEnvironmentProbe::kCameraRoom);
  if (!last_area_environment_resolution_.containing_group.has_value()) {
    return std::nullopt;
  }

  const WmoAreaGroupRef& ref =
      *last_area_environment_resolution_.containing_group;
  const auto instance_it = wmo_instances_.find(ref.placement);
  if (instance_it == wmo_instances_.end()) {
    return std::nullopt;
  }
  const WmoInstance& instance = instance_it->second;
  const auto cache_it = wmo_cache_.find(instance.wmo_path);
  if (cache_it == wmo_cache_.end()) {
    return std::nullopt;
  }
  const CachedWmo& cached = cache_it->second;
  const std::size_t group_index = ref.group_index;
  if (group_index >= cached.groups.size() ||
      group_index >= cached.group_residency.size() ||
      cached.group_residency[group_index] != WmoGroupResidency::kResident) {
    return std::nullopt;
  }

  if ((cached.groups[group_index].header.flags & data::wmo::kMogpExterior) !=
      0u) {
    return std::nullopt;
  }

  const Vec3 local_camera =
      TransformPoint(instance.inverse_model_matrix, {x, y, z});
  const std::uint32_t liquid_type_id = GetUnderwaterLiquidTypeId(x, y, z);
  const auto* const liquid_type =
      liquid_type_id != 0u && dbc_ != nullptr
          ? dbc_->liquid_type().LookupEntry(liquid_type_id)
          : nullptr;
  const std::optional<FogState> resolved =
      ResolveRetailWmoFog(cached.root, cached.groups[group_index].header,
                         local_camera, far_clip,
                         WmoFogLiquidState{
                             .liquid_type_id = liquid_type_id,
                             .liquid_type_flags =
                                 liquid_type != nullptr ? liquid_type->flags
                                                        : 0u,
                         });
  if (!resolved.has_value()) {
    return std::nullopt;
  }

  std::array<std::size_t, 2> lane_storage{};
  std::size_t lane_count = 0u;
  lane_storage[lane_count++] = ref.group_index;
  const auto& alternate = last_area_environment_resolution_.alternate_group;
  if (alternate.has_value() && alternate->placement == ref.placement &&
      alternate->group_index < cached.groups.size() &&
      alternate->group_index < cached.group_residency.size() &&
      cached.group_residency[alternate->group_index] ==
          WmoGroupResidency::kResident &&
      (cached.groups[alternate->group_index].header.flags &
       data::wmo::kMogpExterior) == 0u) {
    lane_storage[lane_count++] = alternate->group_index;
  }
  return CameraWmoFogResolution{
      .state = *resolved,
      .portal_blend_progress = ResolveWmoFogPortalBlendProgress(
          cached.visibility,
          std::span<const std::size_t>{lane_storage.data(), lane_count},
          local_camera),
  };
}

MovementWmoCollisionCompleteness WorldMap::VisitMovementCollisionFacets(
    const std::array<float, 6>& world_bounds,
    const CollisionFacetVisitor& visitor,
    const bool include_object_placements) {
  return VisitWmoFacets(world_bounds, visitor, include_object_placements,
                        WmoFacetSurfaceSet::kMovementCollision);
}

MovementWmoCollisionCompleteness WorldMap::VisitRenderSurfaceFacets(
    const std::array<float, 6>& world_bounds,
    const CollisionFacetVisitor& visitor) {

  return VisitWmoFacets(world_bounds, visitor,
                        true,
                        WmoFacetSurfaceSet::kRenderSurface);
}

MovementWmoCollisionCompleteness WorldMap::VisitWmoFacets(
    const std::array<float, 6>& world_bounds,
    const CollisionFacetVisitor& visitor,
    const bool include_object_placements,
    const WmoFacetSurfaceSet surface_set) {
  if (!visitor ||
      std::any_of(world_bounds.begin(), world_bounds.end(),
                  [](const float value) { return !std::isfinite(value); }) ||
      world_bounds[0] > world_bounds[3] ||
      world_bounds[1] > world_bounds[4] ||
      world_bounds[2] > world_bounds[5]) {
    return MovementWmoCollisionCompleteness::kPending;
  }

  bool all_overlapping_groups_resident = true;
  std::optional<WmoPlacementKey> transformed_placement;
  std::array<float, 6> placement_local_bounds{};

  std::uint64_t emitting_object_guid = 0u;
  const CollisionFacetVisitor stamping_visitor =
      [&visitor, &emitting_object_guid](const CollisionFacetView& facet) {
        if (emitting_object_guid == 0u) {
          visitor(facet);
          return;
        }
        CollisionFacetView stamped = facet;
        stamped.owner_guid = emitting_object_guid;
        visitor(stamped);
      };

  for (const WmoAreaGroupRef& candidate :
       wmo_area_spatial_index_.Candidates(world_bounds)) {
    const auto instance_it = wmo_instances_.find(candidate.placement);
    if (instance_it == wmo_instances_.end()) {
      all_overlapping_groups_resident = false;
      continue;
    }

    if (!include_object_placements &&
        object_wmo_placements_.contains(candidate.placement.owner)) {
      continue;
    }
    const WmoInstance& instance = instance_it->second;
    emitting_object_guid = instance.object_guid;
    const auto cached_it = wmo_cache_.find(instance.wmo_path);
    if (cached_it == wmo_cache_.end()) {
      all_overlapping_groups_resident = false;
      continue;
    }
    CachedWmo& cached = cached_it->second;
    const std::size_t group_count = std::min(
        cached.groups.size(),
        std::min(cached.group_residency.size(),
                 instance.group_world_bounds.size()));
    const std::size_t group_index = candidate.group_index;
    if (group_index >= group_count) {
      all_overlapping_groups_resident = false;
      continue;
    }

    if (!BoundsOverlap(instance.group_world_bounds[group_index], world_bounds)) {
      continue;
    }
    if (!transformed_placement.has_value() ||
        *transformed_placement != candidate.placement) {
      placement_local_bounds = {
          std::numeric_limits<float>::infinity(),
          std::numeric_limits<float>::infinity(),
          std::numeric_limits<float>::infinity(),
          -std::numeric_limits<float>::infinity(),
          -std::numeric_limits<float>::infinity(),
          -std::numeric_limits<float>::infinity()};
      for (std::uint32_t corner = 0; corner < 8u; ++corner) {
        const Vec3 world_corner{
            (corner & 1u) != 0u ? world_bounds[3] : world_bounds[0],
            (corner & 2u) != 0u ? world_bounds[4] : world_bounds[1],
            (corner & 4u) != 0u ? world_bounds[5] : world_bounds[2]};
        const Vec3 local_corner =
            TransformPoint(instance.inverse_model_matrix, world_corner);
        for (std::size_t axis = 0; axis < 3u; ++axis) {
          placement_local_bounds[axis] =
              std::min(placement_local_bounds[axis], local_corner[axis]);
          placement_local_bounds[axis + 3u] =
              std::max(placement_local_bounds[axis + 3u], local_corner[axis]);
        }
      }
      transformed_placement = candidate.placement;
    }
    const auto &local_bounds = placement_local_bounds;
    if (cached.group_residency[group_index] ==
        WmoGroupResidency::kUnrequested) {
      const float query_x = (world_bounds[0] + world_bounds[3]) * 0.5f;
      const float query_y = (world_bounds[1] + world_bounds[4]) * 0.5f;
      const float query_z = (world_bounds[2] + world_bounds[5]) * 0.5f;
      QueueWmoGroupLoad(
          instance.wmo_path, static_cast<std::uint32_t>(group_index),
          BoundsDistanceSquared3D(instance.group_world_bounds[group_index],
                                  query_x, query_y, query_z));
    }
    if (cached.group_residency[group_index] !=
        WmoGroupResidency::kResident) {
      all_overlapping_groups_resident = false;
      continue;
    }
    const data::wmo::WmoGroup& group = cached.groups[group_index];
    const std::size_t triangle_count = std::min(
        group.triangleMaterials.size(), group.indices.size() / 3u);

      const auto emit_triangle = [&](const std::uint32_t triangle_index) {
        if (triangle_index >= triangle_count) {
          return;
        }
        const std::uint8_t mopy_flags =
            group.triangleMaterials[triangle_index].flags;

        if (surface_set == WmoFacetSurfaceSet::kMovementCollision) {
          const bool collidable =
              (mopy_flags &
               (data::wmo::kMopyFDetail | data::wmo::kMopyFCollideHit)) == 0u;
          if (!collidable) {
            return;
          }
        }
        const std::size_t offset =
            static_cast<std::size_t>(triangle_index) * 3u;
        const std::uint16_t i0 = group.indices[offset];
        const std::uint16_t i1 = group.indices[offset + 1u];
        const std::uint16_t i2 = group.indices[offset + 2u];
        if (i0 >= group.vertices.size() || i1 >= group.vertices.size() ||
            i2 >= group.vertices.size()) {
          return;
        }
        const auto to_world = [&instance](const data::wmo::Vec3f& vertex) {
          return TransformPoint(instance.model_matrix,
                                {vertex.x, vertex.y, vertex.z});
        };
        const Vec3 a = to_world(group.vertices[i0]);
        const Vec3 b = to_world(group.vertices[i1]);
        const Vec3 c = to_world(group.vertices[i2]);
        const std::uint64_t facet_id =
            (static_cast<std::uint64_t>(group_index) << 32u) |
            triangle_index;
        VisitMovementTriangle(a, b, c, world_bounds,
                              instance.collision_owner_id, facet_id,
                              mopy_flags, false, stamping_visitor);
      };

      if (surface_set == WmoFacetSurfaceSet::kRenderSurface) {

        constexpr float kBatchBoundsQuantizationSlop = 1.0f;
        for (const data::wmo::WmoRenderBatch& batch : group.renderBatches) {
          const std::array<float, 6> batch_local_bounds{
              static_cast<float>(batch.bboxMin[0]) - kBatchBoundsQuantizationSlop,
              static_cast<float>(batch.bboxMin[1]) - kBatchBoundsQuantizationSlop,
              static_cast<float>(batch.bboxMin[2]) - kBatchBoundsQuantizationSlop,
              static_cast<float>(batch.bboxMax[0]) + kBatchBoundsQuantizationSlop,
              static_cast<float>(batch.bboxMax[1]) + kBatchBoundsQuantizationSlop,
              static_cast<float>(batch.bboxMax[2]) + kBatchBoundsQuantizationSlop};
          if (!BoundsOverlap(batch_local_bounds, local_bounds)) {
            continue;
          }
          const std::uint32_t first_triangle = batch.startIndex / 3u;
          const std::uint32_t batch_triangles = batch.nIndices / 3u;
          for (std::uint32_t offset = 0; offset < batch_triangles; ++offset) {
            emit_triangle(first_triangle + offset);
          }
        }
        continue;
      }

      if (group.bspNodes.empty()) {
        for (std::uint32_t triangle = 0;
             triangle < triangle_count; ++triangle) {
          emit_triangle(triangle);
        }
        continue;
      }

      const auto visit_node = [&](const auto& self,
                                  const std::uint32_t node_index,
                                  const std::size_t depth) -> void {
        if (node_index >= group.bspNodes.size() ||
            depth > group.bspNodes.size()) {
          return;
        }
        const data::wmo::WmoBspNode& node = group.bspNodes[node_index];
        if ((node.flags & 0x04u) != 0u) {
          const std::size_t face_begin = node.faceStart;
          const std::size_t face_end = std::min(
              group.bspFaceIndices.size(),
              face_begin + static_cast<std::size_t>(node.nFaces));
          for (std::size_t face = face_begin; face < face_end; ++face) {
            emit_triangle(group.bspFaceIndices[face]);
          }
          return;
        }
        const std::uint32_t axis = node.flags & 0x03u;
        if (axis > 2u) {
          return;
        }
        if (node.negChild >= 0 && local_bounds[axis] <= node.planeDist) {
          self(self, static_cast<std::uint32_t>(node.negChild), depth + 1u);
        }
        if (node.posChild >= 0 &&
            local_bounds[axis + 3u] >= node.planeDist) {
          self(self, static_cast<std::uint32_t>(node.posChild), depth + 1u);
        }
      };
      visit_node(visit_node, 0u, 0u);
  }
  return all_overlapping_groups_resident
             ? MovementWmoCollisionCompleteness::kComplete
             : MovementWmoCollisionCompleteness::kPending;
}

void WorldMap::VisitMovementLiquidFacets(
    const std::array<float, 6>& world_bounds,
    const std::uint32_t collision_mask,
    const CollisionFacetVisitor& visitor) const {
  if (!visitor || (collision_mask & 0x30000u) == 0u ||
      std::any_of(world_bounds.begin(), world_bounds.end(),
                  [](const float value) { return !std::isfinite(value); }) ||
      world_bounds[0] > world_bounds[3] ||
      world_bounds[1] > world_bounds[4] ||
      world_bounds[2] > world_bounds[5]) {
    return;
  }

  const auto liquid_type_admitted = [this, collision_mask](
                                        const std::uint32_t liquid_type_id) {
    if ((collision_mask & 0x20000u) != 0u) {
      return true;
    }
    if ((collision_mask & 0x10000u) == 0u) {
      return false;
    }

    if (liquid_type_id == 0u) {
      return true;
    }
    const auto* const liquid_type =
        dbc_ != nullptr ? dbc_->liquid_type().LookupEntry(liquid_type_id)
                        : nullptr;
    return liquid_type != nullptr && (liquid_type->flags & 4u) != 0u;
  };

  const auto tile_index = [](const float coordinate) {
    return static_cast<std::int32_t>(
        std::floor(32.0f - coordinate / kTileSize));
  };
  const std::int32_t tile_x_begin =
      std::clamp(tile_index(world_bounds[3]), 0, 63);
  const std::int32_t tile_x_end =
      std::clamp(tile_index(world_bounds[0]), 0, 63);
  const std::int32_t tile_y_begin =
      std::clamp(tile_index(world_bounds[4]), 0, 63);
  const std::int32_t tile_y_end =
      std::clamp(tile_index(world_bounds[1]), 0, 63);
  for (std::int32_t tile_x = tile_x_begin; tile_x <= tile_x_end; ++tile_x) {
    for (std::int32_t tile_y = tile_y_begin; tile_y <= tile_y_end; ++tile_y) {
      const auto tile = loaded_tiles_.find(TileCoord{tile_x, tile_y});
      if (tile == loaded_tiles_.end() || !tile->second) {
        continue;
      }
      const auto& surfaces = tile->second->water_surfaces;
      for (std::size_t surface_index = 0u; surface_index < surfaces.size();
           ++surface_index) {
        const WaterHeightfield& surface = surfaces[surface_index];
        if (!liquid_type_admitted(surface.liquid_type_id) ||
            surface_index > 0xffffu) {
          continue;
        }
        const std::uint64_t owner_id =
            0x0400000000000000ull |
            (static_cast<std::uint64_t>(tile_x & 0xffff) << 32u) |
            (static_cast<std::uint64_t>(tile_y & 0xffff) << 16u) |
            static_cast<std::uint64_t>(surface_index);
        VisitWaterHeightfieldFacets(surface, world_bounds, owner_id, visitor);
      }
    }
  }

  for (const WmoAreaGroupRef& candidate :
       wmo_area_spatial_index_.Candidates(world_bounds)) {
    const auto instance_it = wmo_instances_.find(candidate.placement);
    if (instance_it == wmo_instances_.end()) {
      continue;
    }
    const WmoInstance& instance = instance_it->second;
    const std::size_t group_count = std::min(
        instance.group_liquid_surfaces.size(),
        instance.group_world_bounds.size());
    const std::size_t group_index = candidate.group_index;
    if (group_index < group_count) {
      const auto& surface = instance.group_liquid_surfaces[group_index];
      if (!surface.has_value() ||
          !BoundsOverlap(instance.group_world_bounds[group_index],
                         world_bounds) ||
          !liquid_type_admitted(surface->liquid_type_id)) {
        continue;
      }
      std::uint64_t identity = candidate.placement.owner;
      identity ^= static_cast<std::uint64_t>(candidate.placement.ordinal) +
                  0x9e3779b97f4a7c15ull + (identity << 6u) +
                  (identity >> 2u);
      identity ^= static_cast<std::uint64_t>(group_index) +
                  0x517cc1b727220a95ull + (identity << 6u) +
                  (identity >> 2u);
      const std::uint64_t owner_id =
          0x0500000000000000ull | (identity & 0x00ffffffffffffffull);
      VisitWaterHeightfieldFacets(*surface, world_bounds, owner_id, visitor);
    }
  }
}

void WorldMap::Update(float dt) {
  PumpWorldStaging(kTilePublicationBudgetPerFrame,
                   kWmoPublicationBudgetPerFrame);

  last_frame_delta_seconds_ = std::max(0.0f, dt);
  light_env_elapsed_seconds_ += last_frame_delta_seconds_;
}

WorldPresentationSnapshot WorldMap::PublishPresentationSnapshot(
    CameraSnapshot camera, const float far_clip) {
  const float camera_x = camera.position[0];
  const float camera_y = camera.position[1];
  const float camera_z = camera.position[2];

  camera_x_ = camera_x;
  camera_y_ = camera_y;
  camera_z_ = camera_z;
  has_camera_position_ = true;
  QueueCameraRoomWmoGroups(camera_x, camera_y, camera_z);
  const AreaEnvironmentContext area_environment =
      ResolveAreaEnvironmentAtPosition(camera_x, camera_y, camera_z,
                                       AreaEnvironmentProbe::kCameraRoom);
  const bool room_changed =
      last_presentation_outdoors_ != area_environment.outdoors ||
      last_presentation_room_ != last_area_environment_resolution_.containing_group;

  UpdateDayNightLightEnvironmentForFrame(camera_x, camera_y, camera_z,
                                         camera.forward, far_clip);
  const FogState outdoor_fog{
      .params = {fog_start_, fog_end_, fog_density_, 0.0f},
      .color = fog_color_};

  const std::optional<CameraWmoFogResolution> wmo_fog =
      ResolveCameraWmoFog(camera_x, camera_y, camera_z, far_clip);
  const FogState fog =
      wmo_fog.has_value()
          ? BlendWmoFogPortalTransition(outdoor_fog, wmo_fog->state,
                                        wmo_fog->portal_blend_progress)
          : outdoor_fog;
  const auto* const light_env = openwow::game::DayNight_GetLightEnv();
  const Vec3 direction{
      light_env->ReadFloat(
          openwow::game::kDayNightDerivedDirectionCurrentBaseIndex),
      light_env->ReadFloat(
          openwow::game::kDayNightDerivedDirectionCurrentBaseIndex + 1u),
      light_env->ReadFloat(
          openwow::game::kDayNightDerivedDirectionCurrentBaseIndex + 2u)};
  const Vec3 model_direction = Normalize(direction);
  const auto ambient_slot =
      static_cast<std::size_t>(SkyColorSlot::kGlobalAmbient);
  const auto diffuse_slot =
      static_cast<std::size_t>(SkyColorSlot::kGlobalDiffuse);
  environment_.model_light_direction = model_direction;
  environment_.light_direction = {-model_direction[0], -model_direction[1],
                                  -model_direction[2]};
  environment_.model_ambient =
      lighting_.loaded() ? UnitRgb(active_sky_colors_.colors[ambient_slot])
                              : Vec3{1.0f, 1.0f, 1.0f};
  environment_.model_diffuse =
      lighting_.loaded() ? UnitRgb(active_sky_colors_.colors[diffuse_slot])
                              : Vec3{};
  environment_.ambient = environment_.model_ambient;
  environment_.diffuse = environment_.model_diffuse;
  environment_.wmo_outdoor_diffuse = UnitRgb(light_env->dwords[
      openwow::game::kDayNightDerivedColorCurrentDiffuseIndex]);
  environment_.wmo_outdoor_ambient = UnitRgb(light_env->dwords[
      openwow::game::kDayNightDerivedColorCurrentAmbientIndex]);
  environment_.window_diffuse = UnitRgb(light_env->dwords[
      openwow::game::kDayNightDerivedColorCurrentMidpointIndex]);
  environment_.window_ambient = UnitRgb(light_env->dwords[
      openwow::game::kDayNightDerivedColorCurrentBrightenedMidpointIndex]);
  environment_.wmo_material_ambient_argb = light_env->dwords[
      openwow::game::kDayNightDerivedColorCurrentAmbientIndex];
  environment_.fog_start = fog.params[0];
  environment_.fog_end = fog.params[1];
  environment_.fog_density = std::clamp(fog.params[2], 0.0f, 1.0f);
  environment_.fog_color = fog.color;
  environment_.weather = active_weather_type_;
  environment_.weather_density = active_weather_density_;
  environment_.indoors = !area_environment.outdoors;
  std::optional<ZoneSkyboxEntry> camera_wmo_skybox;
  const auto view_projection = Multiply(camera.view, camera.projection);
  frustum_.ExtractFromViewProj(std::span<const float, 16>{view_projection});
  camera.map_generation = world_staging_generation_;
  for (std::size_t plane = 0; plane < frustum_.planes.size(); ++plane) {
    std::copy_n(frustum_.planes[plane].begin(), 4u,
                camera.frustum_planes.begin() + plane * 4u);
  }
  WorldPresentationSnapshot snapshot = BuildPresentationSnapshot(camera);

  const std::optional<WmoAreaGroupRef>& camera_room_ref =
      last_area_environment_resolution_.containing_group;
  const WmoInstance* camera_room_instance = nullptr;
  if (camera_room_ref.has_value()) {
    const auto instance_it = wmo_instances_.find(camera_room_ref->placement);
    const auto cache_it =
        instance_it != wmo_instances_.end()
            ? wmo_cache_.find(instance_it->second.wmo_path)
            : wmo_cache_.end();
    if (cache_it != wmo_cache_.end()) {
      const CachedWmo& cached = cache_it->second;
      const std::size_t group_index = camera_room_ref->group_index;
      const bool group_resident =
          group_index < cached.groups.size() &&
          group_index < cached.group_residency.size() &&
          cached.group_residency[group_index] == WmoGroupResidency::kResident;
      const std::uint32_t group_flags =
          group_resident
              ? cached.groups[group_index].header.flags
              : (group_index < cached.root.groupInfos.size()
                     ? cached.root.groupInfos[group_index].flags
                     : 0u);
      if ((group_flags & data::wmo::kMogpExterior) == 0u) {
        camera_room_instance = &instance_it->second;
      }
    }
  }

  environment_.sky_visible = camera_room_instance == nullptr;
  environment_.sky_clip_rect = {};
  environment_.terrain_visible = camera_room_instance == nullptr;
  environment_.terrain_clip_rect = {};
  if (camera_room_instance != nullptr) {
    const auto item = std::find_if(
        snapshot.world_models.begin(), snapshot.world_models.end(),
        [&](const WorldPresentationItem& candidate) {
          return candidate.stable_id ==
                 camera_room_instance->placement_stable_id;
        });
    if (item != snapshot.world_models.end() && !item->wmo_seed_groups.empty()) {
      const WmoSkyVisibility& apertures = item->wmo_sky_visibility;
      environment_.sky_visible = apertures.visible;
      environment_.sky_clip_rect = apertures.clip_rect;
      environment_.terrain_visible = apertures.terrain_visible;
      environment_.terrain_clip_rect = apertures.terrain_clip_rect;
      if (apertures.visible && apertures.show_local_skybox) {
        const auto cached = wmo_cache_.find(item->resource_key);
        if (cached != wmo_cache_.end() &&
            !cached->second.root.skyboxName.empty()) {
          camera_wmo_skybox = ZoneSkyboxEntry{
              .model_path = cached->second.root.skyboxName};
        }
      }
    }
  }

  const bool camera_in_liquid =
      openwow::game::DayNight_GetCameraLiquidState().liquid_type_id != 0u;
  const bool sky_dome_enabled = openwow::game::DayNight_IsSkyDomeEnabled();
  const bool fog_override_active =
      openwow::game::DayNight_HasScreenEffectFogOverride();
  environment_.sky_pass_enabled =
      environment_.sky_visible && !camera_in_liquid && sky_dome_enabled;
  const auto pack_argb = [](const std::array<float, 4>& color) {
    const auto channel = [](const float value) {
      return static_cast<std::uint32_t>(
          std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    return (channel(color[3]) << 24u) | (channel(color[0]) << 16u) |
           (channel(color[1]) << 8u) | channel(color[2]);
  };
  if (!environment_.sky_visible) {
    environment_.scene_clear_argb = pack_argb(environment_.fog_color);
  } else if (!camera_in_liquid && (sky_dome_enabled || !fog_override_active)) {
    environment_.scene_clear_argb = 0u;
  } else {
    environment_.scene_clear_argb = pack_argb(fog_color_);
  }
  snapshot.environment.sky_visible = environment_.sky_visible;
  snapshot.environment.sky_clip_rect = environment_.sky_clip_rect;
  snapshot.environment.terrain_visible = environment_.terrain_visible;
  snapshot.environment.terrain_clip_rect = environment_.terrain_clip_rect;
  snapshot.environment.sky_pass_enabled = environment_.sky_pass_enabled;
  snapshot.environment.scene_clear_argb = environment_.scene_clear_argb;

  snapshot.environment.portal_fills.clear();
  snapshot.environment.portal_fill_argb = pack_argb(fog_color_);
  for (const WorldPresentationItem& item : snapshot.world_models) {
    if (item.wmo_portal_fills.empty()) {
      continue;
    }
    auto& into = snapshot.environment.portal_fills;
    const auto rebase = static_cast<std::uint32_t>(into.vertices.size());
    into.vertices.insert(into.vertices.end(),
                         item.wmo_portal_fills.vertices.begin(),
                         item.wmo_portal_fills.vertices.end());
    for (const WmoExteriorPortalFill& fill : item.wmo_portal_fills.fills) {
      into.fills.push_back(
          WmoExteriorPortalFill{.viewport_rect = fill.viewport_rect,
                                .first_vertex = fill.first_vertex + rebase,
                                .vertex_count = fill.vertex_count});
    }
  }
  if (room_changed) {
    snapshot.wmo_camera_room_transition = ++wmo_camera_room_transition_;
    std::string message = "WMO camera room: outdoors=" +
                          std::to_string(area_environment.outdoors ? 1 : 0);
    if (last_area_environment_resolution_.containing_group.has_value()) {
      const WmoAreaGroupRef& ref =
          *last_area_environment_resolution_.containing_group;
      const auto instance = wmo_instances_.find(ref.placement);
      if (instance != wmo_instances_.end()) {
        snapshot.wmo_camera_room_placement =
            instance->second.placement_stable_id;
        snapshot.wmo_camera_room_group = ref.group_index;
        const auto cached = wmo_cache_.find(instance->second.wmo_path);
        message += " path=" + instance->second.wmo_path +
                   " placement=" +
                   std::to_string(instance->second.placement_stable_id) +
                   " group=" + std::to_string(ref.group_index);
        if (cached != wmo_cache_.end() &&
            ref.group_index < cached->second.group_residency.size()) {
          message += " cpu_residency=" + std::to_string(static_cast<int>(
              cached->second.group_residency[ref.group_index]));
          if (ref.group_index < cached->second.group_gpu_publication.size()) {
            message += " gpu_published=" + std::to_string(
                cached->second.group_gpu_publication[ref.group_index] ==
                        WmoGroupPublicationStatus::kDrawableReady
                    ? 1
                    : 0);
            message += " gpu_publication=" + std::string(
                WmoGroupPublicationStatusName(
                    cached->second.group_gpu_publication[ref.group_index]));
          }
        }
        const auto presentation = std::find_if(
            snapshot.world_models.begin(), snapshot.world_models.end(),
            [&](const WorldPresentationItem& item) {
              return item.stable_id == instance->second.placement_stable_id;
            });
        if (presentation != snapshot.world_models.end()) {
          message += " visible_groups=" + std::to_string(std::count_if(
              presentation->visible_subresources.begin(),
              presentation->visible_subresources.end(),
              [](const std::uint8_t value) { return value != 0u; }));
          message += " visible_group_paths=" +
                     std::to_string(
                         presentation->wmo_visible_group_paths.size());
        }
      }
    }
    diagnostics::Log(diagnostics::LogLevel::kInfo, message);
    last_presentation_outdoors_ = area_environment.outdoors;
    last_presentation_room_ = last_area_environment_resolution_.containing_group;
  }
  snapshot.sky.colors = active_sky_colors_.colors;
  snapshot.sky.fog_distance = active_sky_colors_.fog_distance;
  snapshot.sky.fog_multiplier = active_sky_colors_.fog_multiplier;
  snapshot.sky.highlight = active_sky_colors_.highlight_sky;
  snapshot.sky.scene_visibility = active_sky_colors_.scene_visibility;
  snapshot.sky.water_shallow_alpha = active_sky_colors_.water_shallow_alpha;
  snapshot.sky.water_deep_alpha = active_sky_colors_.water_deep_alpha;
  snapshot.sky.ocean_shallow_alpha = active_sky_colors_.ocean_shallow_alpha;
  snapshot.sky.ocean_deep_alpha = active_sky_colors_.ocean_deep_alpha;
  snapshot.primary_skybox = active_primary_skybox_;
  if (camera_wmo_skybox.has_value()) {
    snapshot.primary_skybox = std::move(camera_wmo_skybox);
  }
  const auto& tint = openwow::game::DayNight_GetSpellVisualLightingTint();
  snapshot.spell_visual_tint_argb = tint.packed_argb;
  snapshot.spell_visual_tint_blend = tint.blend_factor;
  snapshot.time_of_day_hours = time_of_day_ * 24.0f;
  snapshot.wmo_night_glow = light_env->ReadFloat(119u);
  snapshot.distant_terrain_enabled =
      wdl_loaded_;
  for (const auto& [placement, instance] : wmo_instances_) {
    (void)placement;
    QueueVisibleWmoGroups(instance.wmo_path, instance, frustum_, camera_x,
                          camera_y, camera_z);
  }
  return snapshot;
}

WorldPresentationCommandBatch WorldMap::DrainPresentationCommands() {
  ++presentation_drain_sequence_;
  for (auto& [path, cached] : wmo_cache_) {
    const std::size_t count = std::min(
        {cached.groups.size(), cached.group_residency.size(),
         cached.group_gpu_queued.size(),
         cached.group_material_indices.size(),
          cached.group_gpu_publication.size(), cached.group_gpu_retry.size()});
    for (std::size_t group_index = 0u; group_index < count; ++group_index) {
      if (cached.group_residency[group_index] != WmoGroupResidency::kResident ||
          cached.group_gpu_queued[group_index] ||
          !ShouldQueueWmoGroupPublication(
              cached.group_gpu_publication[group_index]) ||
          !cached.group_gpu_retry[group_index].CanQueue(
              presentation_drain_sequence_)) {
        continue;
      }
      if (cached.group_presentation_payloads.size() < cached.groups.size()) {
        cached.group_presentation_payloads.resize(cached.groups.size());
      }
      if (!cached.group_presentation_payloads[group_index]) {
        cached.group_presentation_payloads[group_index] =
            std::make_shared<const data::wmo::WmoGroup>(
                cached.groups[group_index]);
      }
      presentation_commands_.emplace_back(PublishWorldModelGroupCommand{
          .resource_key = path,
          .group_index = static_cast<std::uint32_t>(group_index),
          .group = cached.group_presentation_payloads[group_index],
          .material_indices = cached.group_material_indices[group_index]});
      cached.group_gpu_queued[group_index] = true;
    }
  }
  WorldPresentationCommandBatch batch{
      .generation = world_staging_generation_,
      .commands = std::move(presentation_commands_)};
  presentation_commands_.clear();
  return batch;
}

void WorldMap::QueueWaterRipplePresentation(
    SpawnWaterRippleCommand command) {
  if (!world_staging_generation_.IsValid() || command.duration_seconds <= 0.0f ||
      command.initial_extent < 0.0f || command.extent_rate < 0.0f) {
    return;
  }

  const float final_extent =
      command.initial_extent + command.extent_rate * command.duration_seconds;
  const std::array<float, 6> bounds{
      command.position[0] - final_extent,
      command.position[1] - final_extent,
      command.position[2] - 1.0f,
      command.position[0] + final_extent,
      command.position[1] + final_extent,
      command.position[2] + 1.0f,
  };

  const float radius_sq = final_extent * final_extent;
  VisitMovementLiquidFacets(
      bounds, 0x20000u,
      [&command, radius_sq](const CollisionFacetView& facet) {
        for (const auto& vertex : facet.vertices) {
          const float dx = vertex[0] - command.position[0];
          const float dy = vertex[1] - command.position[1];
          const float dz = vertex[2] - command.position[2];
          if (dx * dx + dy * dy + dz * dz > radius_sq) {
            return;
          }
        }
        command.control_points.insert(command.control_points.end(),
                                      facet.vertices.begin(),
                                      facet.vertices.end());
      });
  if (!command.control_points.empty()) {
    presentation_commands_.emplace_back(std::move(command));
  }
}

void WorldMap::AcknowledgePresentation(
    const WorldPresentationAcknowledgment& acknowledgment) {
  if (acknowledgment.generation != world_staging_generation_) {
    return;
  }
  for (const auto& result : acknowledgment.wmo_groups) {
    const auto cached = wmo_cache_.find(result.resource_key);
    if (cached == wmo_cache_.end() ||
        result.group_index >= cached->second.group_gpu_queued.size()) {
      continue;
    }

    cached->second.group_gpu_queued[result.group_index] = false;
    if (result.group_index < cached->second.group_gpu_publication.size() &&
        result.group_index < cached->second.group_residency.size() &&
        cached->second.group_residency[result.group_index] ==
            WmoGroupResidency::kResident) {
      auto& status =
          cached->second.group_gpu_publication[result.group_index];
      if (result.status == WmoGroupPublicationStatus::kRetryableFailure &&
          result.group_index < cached->second.group_gpu_retry.size()) {
        cached->second.group_gpu_retry[result.group_index]
            .RecordRetryableFailure(presentation_drain_sequence_);
        status = WmoGroupPublicationStatus::kPending;
      } else {
        status = result.status;
        if (result.group_index < cached->second.group_gpu_retry.size()) {
          cached->second.group_gpu_retry[result.group_index].Reset();
        }
      }
    }
  }
}

void WorldMap::QueueFullPresentationReplay() {
  presentation_commands_.clear();
  presentation_commands_.emplace_back(ResetPresentationCommand{});

  if (wdl_loaded_) {
    presentation_commands_.emplace_back(
        PublishDistantTerrainCommand{.wdl = wdl_});
  }

  for (const auto& [coord, loaded] : loaded_tiles_) {
    if (!loaded) {
      continue;
    }
    presentation_commands_.emplace_back(PublishTerrainTileCommand{
        .owner = TileOwnerId(coord),
        .tile_x = coord.x,
        .tile_y = coord.y,
        .adt = std::make_shared<const data::terrain::AdtFile>(loaded->adt),
        .liquids = std::make_shared<const std::vector<WaterHeightfield>>(
            loaded->water_surfaces),
        .big_alpha =
            (wdt_.flags & data::terrain::WdtFlags::kBigAlpha) != 0u});
  }

  for (const auto& [path, cached] : wmo_cache_) {
    presentation_commands_.emplace_back(BeginWorldModelCommand{
        .resource_key = path,
        .root =
            std::make_shared<const data::wmo::WmoRoot>(cached.root),
        .group_count = static_cast<std::uint32_t>(cached.groups.size())});
  }
  for (auto& [path, cached] : wmo_cache_) {
    (void)path;
    std::fill(cached.group_gpu_queued.begin(),
              cached.group_gpu_queued.end(), false);
    std::fill(cached.group_gpu_publication.begin(),
              cached.group_gpu_publication.end(),
              WmoGroupPublicationStatus::kPending);
    std::fill(cached.group_gpu_retry.begin(), cached.group_gpu_retry.end(),
              WmoGroupPublicationRetryState{});
  }

  for (const auto& [key, instance] : wmo_instances_) {
    (void)key;
    presentation_commands_.emplace_back(PublishWorldModelInstanceCommand{
        .stable_id = instance.placement_stable_id,
        .doodad_owner = instance.doodad_owner_id,
        .object_guid = instance.object_guid,
        .resource_key = instance.wmo_path,
        .transform = instance.model_matrix,
        .doodad_set = instance.active_wmo_doodad_set_id,
        .additional_doodad_sets = instance.additional_active_wmo_doodad_sets,
        .doodad_animation_controls = instance.doodad_animation_controls,
        .visible = instance.visible,
        .group_count =
            static_cast<std::uint32_t>(instance.group_liquid_surfaces.size())});
    for (std::size_t group_index = 0;
         group_index < instance.group_liquid_surfaces.size(); ++group_index) {
      if (!instance.group_liquid_surfaces[group_index].has_value()) {
        continue;
      }
      presentation_commands_.emplace_back(PublishWorldModelLiquidCommand{
          .stable_id = instance.placement_stable_id,
          .group_index = static_cast<std::uint32_t>(group_index),
          .liquid = std::make_shared<const WaterHeightfield>(
              *instance.group_liquid_surfaces[group_index])});
    }
  }

  presentation_commands_.emplace_back(SetWeatherPresentationCommand{
      .type = active_weather_type_,
      .density = active_weather_density_,
      .row = active_weather_row_});
}

TileCoord WorldMap::WorldToTile(float x, float y) {

  return {static_cast<int32_t>(std::floor(kMapMidPoint - x / kTileSize)),
          static_cast<int32_t>(std::floor(kMapMidPoint - y / kTileSize))};
}

std::unordered_set<TileCoord, TileCoordHash> WorldMap::GetRequiredTiles() const {
  std::unordered_set<TileCoord, TileCoordHash> result;
  for (int32_t dx = -view_distance_; dx <= view_distance_; ++dx) {
    for (int32_t dy = -view_distance_; dy <= view_distance_; ++dy) {
      const int32_t tx = streaming_tile_.x + dx;
      const int32_t ty = streaming_tile_.y + dy;
      if (tx >= kMinTile && tx <= kMaxTile && ty >= kMinTile && ty <= kMaxTile) {
        result.insert({tx, ty});
      }
    }
  }
  return result;
}

std::string WorldMap::GetAdtPath(const TileCoord &coord) const {

  const auto file_tile = ToMapFileTile(coord);
  std::ostringstream oss;
  oss << "World\\Maps\\" << map_name_ << "\\" << map_name_ << "_"
      << file_tile.x << "_" << file_tile.y << ".adt";
  return oss.str();
}

void WorldMap::InitializeWorldStagingWorkers(const std::uint32_t worker_count) {
  if (world_staging_workers_.IsInitialized()) {
    if (world_staging_workers_.GetThreadCount() == worker_count) {
      return;
    }
    CancelWorldStaging();
    world_staging_workers_.Shutdown();
  }
  world_staging_workers_.Initialize(worker_count);
}

void WorldMap::CancelWorldStaging() {
  ++world_staging_generation_;
  for (const auto &[coord, request] : pending_tile_loads_) {
    (void)coord;
    if (request.task_id != 0u) {
      static_cast<void>(world_staging_workers_.CancelTask(request.task_id));
    }
  }
  for (const auto &[path, request] : pending_wmo_loads_) {
    (void)path;
    if (request.task_id != 0u) {
      static_cast<void>(world_staging_workers_.CancelTask(request.task_id));
    }
  }
  for (const auto& [key, request] : pending_wmo_group_loads_) {
    (void)key;
    if (request.task_id != 0u) {
      static_cast<void>(world_staging_workers_.CancelTask(request.task_id));
    }
  }

  pending_tile_loads_.clear();
  ready_tile_loads_.clear();
  ready_tile_publications_ = {};
  pending_wmo_loads_.clear();
  ready_wmo_loads_.clear();
  ready_wmo_publications_ = {};
  wmo_cpu_retry_.clear();
  pending_wmo_group_loads_.clear();
  ready_wmo_group_loads_.clear();
  ready_wmo_group_publications_ = {};
  stream_progress_total_ = 0u;
  stream_progress_completed_ = 0u;
  active_stream_progress_callback_ = {};
  world_staging_pump_sequence_ = 0u;
  if (world_staging_mailbox_) {
    std::lock_guard lock(world_staging_mailbox_->mutex);
    world_staging_mailbox_->tiles.clear();
    world_staging_mailbox_->wmos.clear();
    world_staging_mailbox_->wmo_groups.clear();
  }
}

void WorldMap::CancelWmoGroupLoadsForPath(const std::string& path) {
  for (auto it = pending_wmo_group_loads_.begin();
       it != pending_wmo_group_loads_.end();) {
    if (it->first.path != path) {
      ++it;
      continue;
    }
    if (it->second.task_id != 0u) {
      static_cast<void>(world_staging_workers_.CancelTask(it->second.task_id));
    }
    ready_wmo_group_loads_.erase(it->first);
    it = pending_wmo_group_loads_.erase(it);
  }
}

void WorldMap::QueueTileLoad(const TileCoord &coord) {

  if (wdt_loaded_ && coord.x >= 0 && coord.x <= 63 && coord.y >= 0 && coord.y <= 63) {
    const auto file_tile = ToMapFileTile(coord);
    if (!wdt_.TileExists(static_cast<std::uint32_t>(file_tile.x),
                         static_cast<std::uint32_t>(file_tile.y))) {
      return;
    }
  }
  if (!load_file_ || pending_tile_loads_.contains(coord) || loaded_tiles_.contains(coord)) {
    return;
  }
  if (!world_staging_workers_.IsInitialized()) {

    InitializeWorldStagingWorkers(0u);
  }

  const MapGeneration generation = world_staging_generation_;
  const std::uint64_t request_id = next_world_staging_request_id_++;
  const StreamOwnerHandle owner = MakeStreamOwnerHandle(request_id);
  const bool big_alpha = (wdt_.flags & data::terrain::WdtFlags::kBigAlpha) != 0u;
  const std::uint32_t map_id = map_id_;
  std::array<std::uint32_t, 4> liquid_vertex_formats{0u, 1u, 2u, 2u};
  std::array<std::uint32_t, 4> liquid_material_variants{2u, 2u, 2u, 2u};
  if (dbc_ != nullptr) {
    for (std::size_t category = 0u;
         category < liquid_vertex_formats.size(); ++category) {
      const std::uint32_t liquid_type_id =
          category == 2u && map_id == 0x212u
              ? 15u
              : static_cast<std::uint32_t>(category) + 1u;
      const auto* liquid_type = dbc_->liquid_type().LookupEntry(liquid_type_id);
      const auto* material = liquid_type != nullptr
          ? dbc_->liquid_material().LookupEntry(liquid_type->liquid_material_id)
          : nullptr;
      if (material != nullptr) {
        liquid_vertex_formats[category] = material->lvf;
      }
      if (liquid_type != nullptr) {
        liquid_material_variants[category] = liquid_type->int_params[0];
      }
    }
  }
  const std::string path = GetAdtPath(coord);
  const auto load_file = load_file_;
  const auto mailbox = world_staging_mailbox_;
  const float publication_priority =
      TileBoundsDistanceSq2D(streaming_x_, streaming_y_, coord);

  StagingRequest request{.generation = generation,
                         .owner = owner,
                         .request_id = request_id,
                         .publication_priority = publication_priority};
  pending_tile_loads_.emplace(coord, request);
  try {

    const std::uint32_t task_id = world_staging_workers_.Submit(
        "world-adt:" + path, WmoTaskPriority(publication_priority),
        [coord, generation, owner, request_id, big_alpha, map_id,
         liquid_vertex_formats, liquid_material_variants, path, load_file,
         mailbox]() mutable {
          StagedWorldTile completion{
              .coord = coord,
              .generation = generation,
              .owner = owner,
              .request_id = request_id,
          };
          try {
            auto file_data = load_file(path);
            if (!file_data.empty()) {
              auto result = data::terrain::LoadAdt(file_data);
              if (!result.ok) {
                completion.error = "ADT parse failed: " + path + " — " + result.error;
              } else {
                auto tile = std::make_unique<LoadedTile>();
                tile->coord = coord;
                tile->adt = std::move(result.adt);
                for (std::size_t chunk_index = 0;
                     chunk_index < tile->adt.chunks.size(); ++chunk_index) {
                  tile->terrain_cell_ground_effect_ids[chunk_index] =
                      BuildChunkGroundEffectIdGrid(tile->adt.chunks[chunk_index], big_alpha);
                }
                tile->water_surfaces =
                    BuildAdtWaterHeightfields(
                        tile->adt, map_id, liquid_vertex_formats,
                        liquid_material_variants);
                completion.tile = std::move(tile);
              }
            }
          } catch (const std::exception &exception) {
            completion.error = "ADT preparation failed: " + path + " — " + exception.what();
          } catch (...) {
            completion.error = "ADT preparation failed: " + path + " — unknown exception";
          }
          std::lock_guard lock(mailbox->mutex);
          mailbox->tiles.push_back(std::move(completion));
        });
    pending_tile_loads_.at(coord).task_id = task_id;
  } catch (const std::exception &exception) {
    ready_tile_loads_.insert_or_assign(
        coord, StagedWorldTile{
                   .coord = coord,
                   .generation = generation,
                   .owner = owner,
                   .request_id = request_id,
                   .error = "failed to queue ADT " + path + " — " + exception.what(),
               });
    ready_tile_publications_.push(TilePublicationCandidate{
        .priority = publication_priority,
        .request_id = request_id,
        .coord = coord,
    });
  }
}

void WorldMap::QueueWmoLoad(const std::string &wmo_path,
                                const float publication_priority) {
  if (wmo_path.empty() || wmo_cache_.contains(wmo_path) ||
      !load_file_) {
    return;
  }
  if (auto retry = wmo_cpu_retry_.find(wmo_path);
      retry != wmo_cpu_retry_.end()) {
    retry->second.priority =
        std::min(retry->second.priority, publication_priority);
    if (!retry->second.CanQueue(world_staging_pump_sequence_)) {
      return;
    }
  }
  if (auto pending = pending_wmo_loads_.find(wmo_path);
      pending != pending_wmo_loads_.end()) {
    if (publication_priority < pending->second.publication_priority) {
      pending->second.publication_priority = publication_priority;
      if (ready_wmo_loads_.contains(wmo_path)) {
        ready_wmo_publications_.push(WmoPublicationCandidate{
            .priority = publication_priority,
            .request_id = pending->second.request_id,
            .path = wmo_path,
        });
      }
    }
    return;
  }
  if (!world_staging_workers_.IsInitialized()) {
    InitializeWorldStagingWorkers(0u);
  }

  const MapGeneration generation = world_staging_generation_;
  const std::uint64_t request_id = next_world_staging_request_id_++;
  const StreamOwnerHandle owner = MakeStreamOwnerHandle(request_id);
  const auto load_file = load_file_;
  const auto mailbox = world_staging_mailbox_;
  pending_wmo_loads_.emplace(
      wmo_path, StagingRequest{.generation = generation,
                               .owner = owner,
                               .request_id = request_id,
                               .publication_priority = publication_priority});
  try {
    const core::TaskPriority task_priority =
        streaming_ownership_.HasPendingForPath(wmo_path)
            ? WmoTaskPriority(publication_priority)
            : core::TaskPriority::High;
    const std::uint32_t task_id = world_staging_workers_.Submit(
        "world-wmo-root:" + wmo_path, task_priority,
        [wmo_path, generation, owner, request_id, load_file, mailbox]() mutable {
          StagedWorldWmo completion{
              .path = wmo_path,
              .generation = generation,
              .owner = owner,
              .request_id = request_id,
          };
          try {
            completion.cached =
                PrepareWmoCpuBundle(load_file, wmo_path, &completion.error);
          } catch (const std::exception &exception) {
            completion.error = exception.what();
          } catch (...) {
            completion.error = "unknown WMO preparation exception";
          }
          std::lock_guard lock(mailbox->mutex);
          mailbox->wmos.push_back(std::move(completion));
        });
    pending_wmo_loads_.at(wmo_path).task_id = task_id;
  } catch (const std::exception &exception) {
    ready_wmo_loads_.insert_or_assign(
        wmo_path, StagedWorldWmo{
                      .path = wmo_path,
                      .generation = generation,
                      .owner = owner,
                      .request_id = request_id,
                      .error = "failed to queue: " + std::string(exception.what()),
                  });
    ready_wmo_publications_.push(WmoPublicationCandidate{
        .priority = publication_priority,
        .request_id = request_id,
        .path = wmo_path,
    });
  }
}

void WorldMap::QueueWmoGroupLoad(const std::string& wmo_path,
                                     const std::uint32_t group_index,
                                     const float priority) {
  const auto cached = wmo_cache_.find(wmo_path);
  if (cached == wmo_cache_.end() || !load_file_ ||
      group_index >= cached->second.groups.size()) {
    return;
  }
  auto& residency = cached->second.group_residency[group_index];
  if (residency != WmoGroupResidency::kUnrequested) {
    return;
  }
  if (cached->second.group_cpu_retry.size() < cached->second.groups.size()) {
    cached->second.group_cpu_retry.resize(cached->second.groups.size());
  }
  auto& retry = cached->second.group_cpu_retry[group_index];
  if (retry.HasFailure()) {
    retry.priority = std::min(retry.priority, priority);
    if (!retry.CanQueue(world_staging_pump_sequence_)) {
      return;
    }
  }
  if (!world_staging_workers_.IsInitialized()) {
    InitializeWorldStagingWorkers(0u);
  }

  const WmoGroupRequestKey key{.path = wmo_path,
                               .group_index = group_index};
  const MapGeneration generation = world_staging_generation_;
  const std::uint64_t request_id = next_world_staging_request_id_++;
  const StreamOwnerHandle owner = MakeStreamOwnerHandle(request_id);
  const auto load_file = load_file_;
  const auto mailbox = world_staging_mailbox_;
  const std::size_t material_count = cached->second.root.materials.size();
  const std::string group_path = BuildWmoGroupPath(wmo_path, group_index);
  pending_wmo_group_loads_.emplace(
      key, StagingRequest{.generation = generation,
                          .owner = owner,
                          .request_id = request_id,
                          .publication_priority = priority});
  residency = WmoGroupResidency::kPending;

  try {
    const std::uint32_t task_id = world_staging_workers_.Submit(
        "world-wmo-group:" + group_path, WmoTaskPriority(priority),
        [key, group_path, generation, owner, request_id, load_file, mailbox,
         material_count]() mutable {
          StagedWorldWmoGroup completion{
              .key = key,
              .generation = generation,
              .owner = owner,
              .request_id = request_id,
          };
          try {
            auto group_data = load_file(group_path);
            completion.publication_bytes = group_data.size();
            if (group_data.empty()) {
              completion.error = "group not found: " + group_path;
            } else {
              auto result = data::wmo::LoadWmoGroup(group_data);
              if (!result.ok) {
                completion.error =
                    "group parse failed: " + group_path + " — " + result.error;
              } else {
                completion.group = std::move(result.group);
                completion.material_indices = CollectWmoGroupMaterialIndices(
                    completion.group, material_count);
                completion.area_triangle_index =
                    WmoAreaTriangleIndex::Build(completion.group);
                completion.collision_vertices.reserve(
                    completion.group.vertices.size());
                for (const auto& vertex : completion.group.vertices) {
                  completion.collision_vertices.push_back(
                      {vertex.x, vertex.y, vertex.z});
                }
              }
            }
          } catch (const std::exception& exception) {
            completion.error = exception.what();
          } catch (...) {
            completion.error = "unknown WMO group preparation exception";
          }
          std::lock_guard lock(mailbox->mutex);
          mailbox->wmo_groups.push_back(std::move(completion));
        });
    pending_wmo_group_loads_.at(key).task_id = task_id;
  } catch (const std::exception& exception) {
    ready_wmo_group_loads_.insert_or_assign(
        key, StagedWorldWmoGroup{
                 .key = key,
                 .generation = generation,
                 .owner = owner,
                 .request_id = request_id,
                 .error = "failed to queue: " + std::string(exception.what()),
             });
    ready_wmo_group_publications_.push(WmoGroupPublicationCandidate{
        .priority = priority,
        .request_id = request_id,
        .key = key,
    });
  }
}

void WorldMap::QueueInitialWmoGroups(const std::string& wmo_path) {
  auto cached = wmo_cache_.find(wmo_path);
  if (cached == wmo_cache_.end()) {
    return;
  }
  const std::size_t group_count = cached->second.groups.size();
  auto& candidates = wmo_group_priority_scratch_;
  candidates.clear();
  candidates.reserve(group_count);
  for (std::size_t group_index = 0; group_index < group_count; ++group_index) {
    float best = std::numeric_limits<float>::max();
    for (auto& [key, instance] : wmo_instances_) {
      (void)key;
      if (instance.wmo_path != wmo_path ||
          group_index >= instance.group_world_bounds.size()) {
        continue;
      }
      const float distance = BoundsDistanceSquared3D(
          instance.group_world_bounds[group_index], player_x_, player_y_,
          player_z_);
      best = std::min(best, distance);
    }
    if (best != std::numeric_limits<float>::max()) {
      candidates.emplace_back(best, static_cast<std::uint32_t>(group_index));
    }
  }
  std::stable_sort(candidates.begin(), candidates.end());
  struct CriticalCandidate {
    float distance_squared{0.0f};
    WmoInstance* instance{nullptr};
    std::size_t group_index{0u};
  };
  std::vector<CriticalCandidate> critical_candidates;
  for (auto& [key, instance] : wmo_instances_) {
    (void)key;
    if (instance.wmo_path != wmo_path ||
        BoundsDistanceSquared3D(instance.placement_world_bounds, player_x_,
                                player_y_, player_z_) >
            kCriticalSpawnSurfaceRadius * kCriticalSpawnSurfaceRadius) {
      continue;
    }
    instance.critical_spawn_groups.assign(group_count, 0u);
    std::optional<CriticalCandidate> nearest;
    for (std::size_t group_index = 0;
         group_index < instance.group_world_bounds.size(); ++group_index) {
      const float distance = BoundsDistanceSquared3D(
          instance.group_world_bounds[group_index], player_x_, player_y_,
          player_z_);
      const CriticalCandidate candidate{.distance_squared = distance,
                                        .instance = &instance,
                                        .group_index = group_index};
      if (!nearest.has_value() ||
          distance < nearest->distance_squared) {
        nearest = candidate;
      }
      if (distance <=
          kCriticalSpawnSurfaceRadius * kCriticalSpawnSurfaceRadius) {
        critical_candidates.push_back(candidate);
      }
    }
    if (nearest.has_value() &&
        std::none_of(critical_candidates.begin(), critical_candidates.end(),
                     [&](const CriticalCandidate& candidate) {
                       return candidate.instance == &instance;
                     })) {
      critical_candidates.push_back(*nearest);
    }
  }
  std::stable_sort(
      critical_candidates.begin(), critical_candidates.end(),
      [](const CriticalCandidate& lhs, const CriticalCandidate& rhs) {
        return lhs.distance_squared < rhs.distance_squared;
      });
  for (const CriticalCandidate& candidate : critical_candidates) {
    candidate.instance->critical_spawn_groups[candidate.group_index] = 1u;
  }
  std::unordered_set<std::uint32_t> queued_groups;
  for (const CriticalCandidate& candidate : critical_candidates) {
    const auto group_index =
        static_cast<std::uint32_t>(candidate.group_index);
    if (queued_groups.insert(group_index).second) {
      QueueWmoGroupLoad(wmo_path, group_index,
                        candidate.distance_squared);
    }
  }
  for (const auto& [distance_squared, group_index] : candidates) {

    if (!world_entry_streaming_mode_ &&
        distance_squared > kWmoHighPriorityRadius * kWmoHighPriorityRadius) {
      break;
    }
    if (queued_groups.insert(group_index).second) {
      QueueWmoGroupLoad(wmo_path, group_index, distance_squared);
    }
  }
}

void WorldMap::QueueCameraRoomWmoGroups(const float camera_x,
                                        const float camera_y,
                                        const float camera_z) {
  const auto* const candidates =
      wmo_area_spatial_index_.Candidates(camera_x, camera_y);
  if (candidates == nullptr) {
    return;
  }

  const float segment_min_z = camera_z - kRetailCameraRoomRayDepth;
  const float segment_max_z = camera_z;
  for (const WmoAreaGroupRef& candidate : *candidates) {
    const auto instance = wmo_instances_.find(candidate.placement);
    if (instance == wmo_instances_.end() ||
        candidate.group_index >= instance->second.group_world_bounds.size()) {
      continue;
    }
    const auto& bounds =
        instance->second.group_world_bounds[candidate.group_index];
    if (camera_x < bounds[0] || camera_x > bounds[3] ||
        camera_y < bounds[1] || camera_y > bounds[4] ||
        segment_max_z < bounds[2] || segment_min_z > bounds[5]) {
      continue;
    }
    QueueWmoGroupLoad(instance->second.wmo_path, candidate.group_index, 0.0f);
  }
}

void WorldMap::QueueVisibleWmoGroups(
    const std::string& wmo_path, const WmoInstance& instance,
    const world::Frustum& frustum, const float camera_x,
    const float camera_y, const float camera_z) {
  auto cached = wmo_cache_.find(wmo_path);
  if (cached == wmo_cache_.end()) {
    return;
  }
  auto& candidates = wmo_group_priority_scratch_;
  candidates.clear();
  const std::size_t group_count = std::min(
      cached->second.group_residency.size(), instance.group_world_bounds.size());
  candidates.reserve(group_count);
  for (std::size_t group_index = 0; group_index < group_count; ++group_index) {
    if (cached->second.group_residency[group_index] !=
        WmoGroupResidency::kUnrequested) {
      continue;
    }
    const auto& bounds = instance.group_world_bounds[group_index];
    const float distance = BoundsDistanceSquared3D(
        bounds, camera_x, camera_y, camera_z);
    if (!frustum.TestAABB(bounds[0], bounds[1], bounds[2], bounds[3],
                          bounds[4], bounds[5]) &&
        distance > kWmoHighPriorityRadius * kWmoHighPriorityRadius) {
      continue;
    }
    candidates.emplace_back(distance,
                            static_cast<std::uint32_t>(group_index));
  }
  std::stable_sort(candidates.begin(), candidates.end());
  for (const auto& [distance_squared, group_index] : candidates) {
    QueueWmoGroupLoad(wmo_path, group_index, distance_squared);
  }
}

void WorldMap::RebuildPublicationOrder(const float focus_x, const float focus_y) {
  ready_tile_publications_ = {};
  for (auto &[coord, request] : pending_tile_loads_) {
    request.publication_priority =
        TileBoundsDistanceSq2D(focus_x, focus_y, coord);
    if (ready_tile_loads_.contains(coord)) {
      ready_tile_publications_.push(TilePublicationCandidate{
          .priority = request.publication_priority,
          .request_id = request.request_id,
          .coord = coord,
      });
    }
  }

  ready_wmo_publications_ = {};
  for (auto &[path, request] : pending_wmo_loads_) {
    if (const auto priority = streaming_ownership_.PendingPriorityForPath(
            path, focus_x, focus_y)) {
      request.publication_priority = *priority;
    }
    if (ready_wmo_loads_.contains(path)) {
      ready_wmo_publications_.push(WmoPublicationCandidate{
          .priority = request.publication_priority,
          .request_id = request.request_id,
          .path = path,
      });
    }
  }

  ready_wmo_group_publications_ = {};
  for (auto& [key, request] : pending_wmo_group_loads_) {
    float best = request.publication_priority;
    for (const auto& [placement_key, instance] : wmo_instances_) {
      (void)placement_key;
      if (instance.wmo_path == key.path &&
          key.group_index < instance.group_world_bounds.size()) {
        best = std::min(
            best, BoundsDistanceSquared3D(
                      instance.group_world_bounds[key.group_index], focus_x,
                      focus_y, player_z_));
      }
    }
    request.publication_priority = best;
    if (ready_wmo_group_loads_.contains(key)) {
      ready_wmo_group_publications_.push(WmoGroupPublicationCandidate{
          .priority = best,
          .request_id = request.request_id,
          .key = key,
      });
    }
  }
}

void WorldMap::DrainWorldStagingMailbox() {
  if (!world_staging_mailbox_) {
    return;
  }
  std::vector<StagedWorldTile> tiles;
  std::vector<StagedWorldWmo> wmos;
  std::vector<StagedWorldWmoGroup> wmo_groups;
  {
    std::lock_guard lock(world_staging_mailbox_->mutex);
    tiles.swap(world_staging_mailbox_->tiles);
    wmos.swap(world_staging_mailbox_->wmos);
    wmo_groups.swap(world_staging_mailbox_->wmo_groups);
  }

  for (auto &completion : tiles) {
    const auto request = pending_tile_loads_.find(completion.coord);
    if (completion.generation != world_staging_generation_ ||
        request == pending_tile_loads_.end() ||
        request->second.generation != completion.generation ||
        request->second.owner != completion.owner ||
        request->second.request_id != completion.request_id) {
      continue;
    }
    ready_tile_loads_.insert_or_assign(completion.coord, std::move(completion));
    ready_tile_publications_.push(TilePublicationCandidate{
        .priority = request->second.publication_priority,
        .request_id = request->second.request_id,
        .coord = request->first,
    });
  }
  for (auto &completion : wmos) {
    const auto request = pending_wmo_loads_.find(completion.path);
    if (completion.generation != world_staging_generation_ ||
        request == pending_wmo_loads_.end() ||
        request->second.generation != completion.generation ||
        request->second.owner != completion.owner ||
        request->second.request_id != completion.request_id) {
      continue;
    }
    ready_wmo_loads_.insert_or_assign(completion.path, std::move(completion));
    ready_wmo_publications_.push(WmoPublicationCandidate{
        .priority = request->second.publication_priority,
        .request_id = request->second.request_id,
        .path = request->first,
    });
  }
  for (auto& completion : wmo_groups) {
    const auto request = pending_wmo_group_loads_.find(completion.key);
    const auto cached = wmo_cache_.find(completion.key.path);
    if (completion.generation != world_staging_generation_ ||
        request == pending_wmo_group_loads_.end() ||
        cached == wmo_cache_.end() ||
        request->second.generation != completion.generation ||
        request->second.owner != completion.owner ||
        request->second.request_id != completion.request_id ||
        completion.key.group_index >= cached->second.group_residency.size()) {
      continue;
    }
    cached->second.group_residency[completion.key.group_index] =
        completion.error.empty() ? WmoGroupResidency::kReady
                                 : WmoGroupResidency::kUnrequested;
    ready_wmo_group_loads_.insert_or_assign(completion.key,
                                             std::move(completion));
    ready_wmo_group_publications_.push(WmoGroupPublicationCandidate{
        .priority = request->second.publication_priority,
        .request_id = request->second.request_id,
        .key = request->first,
    });
  }
}

void WorldMap::QueueDueWmoRetries() {
  std::vector<std::pair<std::string, float>> root_retries;
  std::vector<std::string> abandoned_roots;
  root_retries.reserve(wmo_cpu_retry_.size());
  for (const auto& [path, retry] : wmo_cpu_retry_) {
    if (!retry.HasFailure() ||
        !retry.CanQueue(world_staging_pump_sequence_) ||
        pending_wmo_loads_.contains(path)) {
      continue;
    }
    if (!streaming_ownership_.HasPendingForPath(path)) {
      abandoned_roots.push_back(path);
      continue;
    }
    root_retries.emplace_back(path, retry.priority);
  }
  for (const std::string& path : abandoned_roots) {
    wmo_cpu_retry_.erase(path);
  }
  for (const auto& [path, priority] : root_retries) {
    QueueWmoLoad(path, priority);
  }

  struct GroupRetry {
    std::string path;
    std::uint32_t group_index{};
    float priority{};
  };
  std::vector<GroupRetry> group_retries;
  for (const auto& [path, cached] : wmo_cache_) {
    const std::size_t count = std::min(
        cached.group_residency.size(), cached.group_cpu_retry.size());
    for (std::size_t group_index = 0u; group_index < count; ++group_index) {
      const auto& retry = cached.group_cpu_retry[group_index];
      if (cached.group_residency[group_index] ==
              WmoGroupResidency::kUnrequested &&
          retry.HasFailure() && retry.CanQueue(world_staging_pump_sequence_)) {
        group_retries.push_back(
            {.path = path,
             .group_index = static_cast<std::uint32_t>(group_index),
             .priority = retry.priority});
      }
    }
  }
  for (const GroupRetry& retry : group_retries) {
    QueueWmoGroupLoad(retry.path, retry.group_index, retry.priority);
  }
}

void WorldMap::PublishWmoGroup(StagedWorldWmoGroup completion) {
  auto cached = wmo_cache_.find(completion.key.path);
  if (cached == wmo_cache_.end() ||
      completion.key.group_index >= cached->second.groups.size()) {
    return;
  }

  const std::size_t group_index = completion.key.group_index;
  cached->second.groups[group_index] = std::move(completion.group);
  if (cached->second.group_presentation_payloads.size() <
      cached->second.groups.size()) {
    cached->second.group_presentation_payloads.resize(
        cached->second.groups.size());
  }
  cached->second.group_presentation_payloads[group_index] =
      std::make_shared<const data::wmo::WmoGroup>(
          cached->second.groups[group_index]);
  cached->second.collision_vertices[group_index] =
      std::move(completion.collision_vertices);
  cached->second.area_triangle_indices[group_index] =
      std::move(completion.area_triangle_index);
  cached->second.group_material_indices[group_index] =
      std::move(completion.material_indices);
  cached->second.visibility.PublishGroup(cached->second.root, group_index,
                                         cached->second.groups[group_index]);
  cached->second.group_residency[group_index] = WmoGroupResidency::kResident;

  InvalidateAreaEnvironmentCache();
  if (group_index < cached->second.group_cpu_retry.size()) {
    cached->second.group_cpu_retry[group_index].Reset();
  }
  if (cached->second.group_gpu_queued.size() < cached->second.groups.size()) {
    cached->second.group_gpu_queued.resize(cached->second.groups.size(), false);
  }
  cached->second.group_gpu_queued[group_index] = false;
  if (cached->second.group_gpu_publication.size() < cached->second.groups.size()) {
    cached->second.group_gpu_publication.resize(
        cached->second.groups.size(), WmoGroupPublicationStatus::kPending);
  }
  cached->second.group_gpu_publication[group_index] =
      WmoGroupPublicationStatus::kPending;
  if (cached->second.group_gpu_retry.size() < cached->second.groups.size()) {
    cached->second.group_gpu_retry.resize(cached->second.groups.size());
  }
  cached->second.group_gpu_retry[group_index].Reset();

  for (auto& [placement_key, instance] : wmo_instances_) {
    if (instance.wmo_path != completion.key.path) {
      continue;
    }
    if (instance.visible) {
      AddWmoAreaGroupToIndex(placement_key, instance, group_index);
    }
    if (instance.group_liquid_surfaces.size() < cached->second.groups.size()) {
      instance.group_liquid_surfaces.resize(cached->second.groups.size());
    }
    const auto& group = cached->second.groups[group_index];
    const std::uint32_t query_liquid_type =
        ResolveWmoGroupLiquidType(cached->second.root, group);
    const auto* liquid_type =
        dbc_ != nullptr
            ? dbc_->liquid_type().LookupEntry(
                  query_liquid_type != 0u ? query_liquid_type : 1u)
            : nullptr;
    const std::uint32_t liquid_type_flags =
        liquid_type != nullptr ? liquid_type->flags : 0u;

    const std::uint32_t render_liquid_type = ResolveWmoGroupRenderLiquidType(
        query_liquid_type, group.header.flags, liquid_type_flags);
    const auto* render_type =
        dbc_ != nullptr
            ? dbc_->liquid_type().LookupEntry(
                  render_liquid_type != 0u ? render_liquid_type : 1u)
            : nullptr;
    const std::uint32_t depth_attenuation_table =
        render_type != nullptr ? render_type->int_params[0] : 0u;
    instance.group_liquid_surfaces[group_index] =
        BuildWmoGroupLiquidHeightfield(
            cached->second.root, group, instance.model_matrix,
            liquid_type_flags,
            ResolveWmoLiquidVertexFormat(query_liquid_type, group.header.flags,
                                         liquid_type_flags),
            depth_attenuation_table,

            [&cached](const std::uint32_t sibling_index)
                -> const data::wmo::WmoGroup * {
              const CachedWmo &wmo = cached->second;
              if (sibling_index >= wmo.groups.size() ||
                  sibling_index >= wmo.group_residency.size() ||
                  wmo.group_residency[sibling_index] !=
                      WmoGroupResidency::kResident) {
                return nullptr;
              }
              return &wmo.groups[sibling_index];
            });
    if (instance.group_liquid_surfaces[group_index]) {
      presentation_commands_.emplace_back(PublishWorldModelLiquidCommand{
          .stable_id = instance.placement_stable_id,
          .group_index = static_cast<std::uint32_t>(group_index),
          .liquid = std::make_shared<const WaterHeightfield>(
              *instance.group_liquid_surfaces[group_index])});
    }
  }

}

std::size_t WorldMap::PumpReadyWmoGroups(
    const std::size_t group_budget, std::uint64_t* const byte_budget,
    const std::chrono::steady_clock::time_point deadline) {
  if (byte_budget == nullptr) {
    return 0u;
  }
  std::size_t published = 0u;
  while (published < group_budget &&
         !ready_wmo_group_publications_.empty()) {
    if (std::chrono::steady_clock::now() >= deadline) {
      break;
    }
    const WmoGroupPublicationCandidate candidate =
        ready_wmo_group_publications_.top();
    const auto request = pending_wmo_group_loads_.find(candidate.key);
    auto ready = ready_wmo_group_loads_.find(candidate.key);
    if (request == pending_wmo_group_loads_.end() ||
        ready == ready_wmo_group_loads_.end() ||
        request->second.request_id != candidate.request_id ||
        ready->second.request_id != candidate.request_id ||
        ready->second.generation != world_staging_generation_ ||
        request->second.owner != ready->second.owner) {
      ready_wmo_group_publications_.pop();
      continue;
    }

    auto& completion = ready->second;
    std::uint64_t remaining_after_publication = *byte_budget;
    if (completion.error.empty() &&
        !TryConsumePublicationByteBudget(
            completion.publication_bytes,
            kWmoPublicationByteBudgetPerFrame,
            remaining_after_publication)) {
      break;
    }
    ready_wmo_group_publications_.pop();
    if (!completion.error.empty()) {
      diagnostics::Log(diagnostics::LogLevel::kWarn,
                "WorldMap: WMO group " + completion.key.path + "[" +
                    std::to_string(completion.key.group_index) + "] — " +
                    completion.error);
      if (auto cached = wmo_cache_.find(completion.key.path);
          cached != wmo_cache_.end() &&
          completion.key.group_index < cached->second.group_residency.size()) {
        cached->second.group_residency[completion.key.group_index] =
            WmoGroupResidency::kUnrequested;
        if (completion.key.group_index <
            cached->second.group_cpu_retry.size()) {
          cached->second.group_cpu_retry[completion.key.group_index]
              .RecordFailure(world_staging_pump_sequence_,
                             request->second.publication_priority);
        }
      }
      ready_wmo_group_loads_.erase(ready);
      pending_wmo_group_loads_.erase(request);
      ++published;
      continue;
    }

    *byte_budget = remaining_after_publication;
    StagedWorldWmoGroup published_group = std::move(completion);
    ready_wmo_group_loads_.erase(ready);
    pending_wmo_group_loads_.erase(request);
    PublishWmoGroup(std::move(published_group));
    ++published;
  }
  return published;
}

void WorldMap::PumpWorldStaging(const std::size_t tile_budget,
                                    const std::size_t wmo_budget) {
  ++world_staging_pump_sequence_;
  QueueDueWmoRetries();
  const auto pump_started = std::chrono::steady_clock::now();
  DrainWorldStagingMailbox();

  std::size_t published_tiles = 0u;
  while (published_tiles < tile_budget && !ready_tile_publications_.empty()) {
    const TilePublicationCandidate candidate = ready_tile_publications_.top();
    ready_tile_publications_.pop();
    const auto request = pending_tile_loads_.find(candidate.coord);
    const auto ready = ready_tile_loads_.find(candidate.coord);
    if (request == pending_tile_loads_.end() ||
        ready == ready_tile_loads_.end() ||
        request->second.request_id != candidate.request_id ||
        ready->second.request_id != candidate.request_id ||
        ready->second.generation != world_staging_generation_ ||
        request->second.owner != ready->second.owner) {
      continue;
    }

    StagedWorldTile completion = std::move(ready->second);
    ready_tile_loads_.erase(ready);
    pending_tile_loads_.erase(request);
    ++published_tiles;
    ++stream_progress_completed_;

    if (!completion.error.empty()) {
      diagnostics::Log(diagnostics::LogLevel::kWarn, "WorldMap: " + completion.error);
    }
    if (completion.tile) {
      const TileCoord coord = candidate.coord;
      loaded_tiles_.insert_or_assign(coord, std::move(completion.tile));
      InvalidateAreaEnvironmentCache();
      auto &loaded = loaded_tiles_.at(coord);

      if (ambient_audio_ != nullptr) {
        for (std::size_t index = 0; index < loaded->adt.chunks.size(); ++index) {
          CreateChunkAmbientSounds(
              loaded->adt.chunks[index].sound_emitters, *ambient_audio_,
              loaded->ambient_sounds[index]);
        }
      }

      if (tile_loaded_callback_) {
        tile_loaded_callback_(coord, loaded->adt);
      }
      presentation_commands_.emplace_back(PublishTerrainTileCommand{
          .owner = TileOwnerId(coord),
          .tile_x = coord.x,
          .tile_y = coord.y,
          .adt = std::make_shared<const data::terrain::AdtFile>(loaded->adt),
          .liquids =
              std::make_shared<const std::vector<WaterHeightfield>>(
                  loaded->water_surfaces),
          .big_alpha =
              (wdt_.flags & data::terrain::WdtFlags::kBigAlpha) != 0u});
      RegisterTileWmoPlacementOwner(*loaded);
    }

    if (active_stream_progress_callback_ && stream_progress_total_ != 0u) {
      active_stream_progress_callback_(std::min(
          1.0f, static_cast<float>(stream_progress_completed_) /
                    static_cast<float>(stream_progress_total_)));
    }
  }

  DrainWorldStagingMailbox();
  std::size_t published_wmos = 0u;
  while (published_wmos < wmo_budget && !ready_wmo_publications_.empty()) {
    WmoPublicationCandidate candidate = ready_wmo_publications_.top();
    ready_wmo_publications_.pop();
    const auto request = pending_wmo_loads_.find(candidate.path);
    const auto ready = ready_wmo_loads_.find(candidate.path);
    if (request == pending_wmo_loads_.end() ||
        ready == ready_wmo_loads_.end() ||
        request->second.request_id != candidate.request_id ||
        ready->second.request_id != candidate.request_id ||
        ready->second.generation != world_staging_generation_ ||
        request->second.owner != ready->second.owner) {
      continue;
    }

    StagedWorldWmo completion = std::move(ready->second);
    const float publication_priority = request->second.publication_priority;
    ready_wmo_loads_.erase(ready);
    pending_wmo_loads_.erase(request);
    ++published_wmos;
    if (completion.cached) {
      const std::string path = std::move(candidate.path);
      wmo_cpu_retry_.erase(path);
      if (streaming_ownership_.HasPendingForPath(path)) {
        auto [cached, inserted] =
            wmo_cache_.insert_or_assign(path, std::move(*completion.cached));
        (void)inserted;

        InvalidateAreaEnvironmentCache();
        presentation_commands_.emplace_back(BeginWorldModelCommand{
            .resource_key = path,
            .root = std::make_shared<const data::wmo::WmoRoot>(
                cached->second.root),
            .group_count =
                static_cast<std::uint32_t>(cached->second.groups.size())});
        PublishWmoPlacementsForPath(path);
        QueueInitialWmoGroups(path);
      }
    } else {
      wmo_cpu_retry_[candidate.path].RecordFailure(
          world_staging_pump_sequence_, publication_priority);
      if (!completion.error.empty()) {
        diagnostics::Log(diagnostics::LogLevel::kWarn,
                  "WorldMap: WMO " + candidate.path + " — " + completion.error);
      }
    }
  }

  DrainWorldStagingMailbox();
  std::uint64_t wmo_byte_budget = kWmoPublicationByteBudgetPerFrame;
  const auto wmo_deadline = pump_started + kWmoPublicationTimeBudgetPerFrame;
  PumpReadyWmoGroups(
      wmo_budget > published_wmos ? wmo_budget - published_wmos : 0u,
      &wmo_byte_budget, wmo_deadline);

  if (stream_progress_total_ != 0u &&
      stream_progress_completed_ >= stream_progress_total_ &&
      pending_tile_loads_.empty()) {
    active_stream_progress_callback_ = {};
    stream_progress_total_ = 0u;
    stream_progress_completed_ = 0u;
  }
}

void WorldMap::UnloadTile(const TileCoord &coord) {
  const std::uint64_t owner = TileOwnerId(coord);
  RemoveWmoPlacementOwner(owner);
  presentation_commands_.emplace_back(RemoveTerrainTileCommand{
      .owner = owner, .tile_x = coord.x, .tile_y = coord.y});
  if (tile_unloaded_callback_) {
    tile_unloaded_callback_(coord);
  }
  loaded_tiles_.erase(coord);
  InvalidateAreaEnvironmentCache();
}

void WorldMap::ComputeWmoModelMatrix(const data::terrain::WmoPlacement &placement,
                                         float out_mtx[16]) {
  const auto model_matrix = BuildWmoModelMatrix(placement);
  std::copy(model_matrix.begin(), model_matrix.end(), out_mtx);
}

bool WorldMap::SetScreenSize(uint16_t width, uint16_t height) {
  if (width == screen_width_ && height == screen_height_) {
    return false;
  }

  screen_width_ = width;
  screen_height_ = height;
  return true;
}

}

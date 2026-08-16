#include "openwow/world/collision/collision.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>

namespace openwow::world {

using namespace data::terrain;

void TerrainCollision::SetTileData(int32_t tile_x, int32_t tile_y,
                                   const AdtFile& adt) {
  TileKey key{tile_x, tile_y};
  TileData& td = tiles_[key];
  td.tile_x = tile_x;
  td.tile_y = tile_y;

  for (int i = 0; i < kTotalChunks; ++i) {
    const TerrainChunk& src = adt.chunks[static_cast<size_t>(i)];
    ChunkData& dst = td.chunks[static_cast<size_t>(i)];
    dst.base_x  = src.header.position_x;
    dst.base_y  = src.header.position_y;
    dst.base_z  = src.header.position_z;
    dst.heights = src.heights;
    dst.holes   = src.holes;
  }
  ++revision_;
}

void TerrainCollision::RemoveTile(int32_t tile_x, int32_t tile_y) {
  if (tiles_.erase(TileKey{tile_x, tile_y}) != 0u) {
    ++revision_;
  }
}

void TerrainCollision::Clear() {
  if (!tiles_.empty()) {
    tiles_.clear();
    ++revision_;
  }
}

std::optional<float> TerrainCollision::GetHeightAt(float x, float y) const {
  return GetHeight(x, y);
}

bool TerrainCollision::IsLoaded(float x, float y) const {
  const int32_t tx =
      static_cast<int32_t>(std::floor(kMapMidPoint - x / kTileSize));
  const int32_t ty =
      static_cast<int32_t>(std::floor(kMapMidPoint - y / kTileSize));
  return tiles_.find(TileKey{tx, ty}) != tiles_.end();
}

bool TerrainCollision::AreTilesLoaded(const float min_x, const float max_x,
                                      const float min_y, const float max_y) const {
  if (min_x > max_x || min_y > max_y) {
    return false;
  }
  const auto tile_index = [](const float coordinate) {
    return static_cast<std::int32_t>(
        std::floor(kMapMidPoint - coordinate / kTileSize));
  };
  const std::int32_t tile_x_begin = tile_index(max_x);
  const std::int32_t tile_x_end = tile_index(min_x);
  const std::int32_t tile_y_begin = tile_index(max_y);
  const std::int32_t tile_y_end = tile_index(min_y);
  if (tile_x_begin < 0 || tile_x_end > 63 || tile_y_begin < 0 ||
      tile_y_end > 63) {
    return false;
  }
  for (std::int32_t tile_x = tile_x_begin; tile_x <= tile_x_end; ++tile_x) {
    for (std::int32_t tile_y = tile_y_begin; tile_y <= tile_y_end; ++tile_y) {
      if (tiles_.find(TileKey{tile_x, tile_y}) == tiles_.end()) {
        return false;
      }
    }
  }
  return true;
}

std::optional<float> TerrainCollision::GetHeight(float x, float y) const {
  int   cell_row, cell_col;
  float fx, fy;
  const ChunkData* chunk = ResolveChunk(x, y, cell_row, cell_col, fx, fy);
  if (!chunk) return std::nullopt;
  if (IsTerrainHoleCell(chunk->holes, cell_row, cell_col)) return std::nullopt;

  return chunk->base_z +
         InterpolateTerrainHeightDelta(chunk->heights, cell_row, cell_col, fx, fy);
}

std::optional<std::array<float, 3>> TerrainCollision::GetNormal(
    float x, float y) const {
  int   cell_row, cell_col;
  float fx, fy;
  const ChunkData* chunk = ResolveChunk(x, y, cell_row, cell_col, fx, fy);
  if (!chunk) return std::nullopt;
  if (IsTerrainHoleCell(chunk->holes, cell_row, cell_col)) return std::nullopt;

  return ComputeTriangleNormal(*chunk, cell_row, cell_col, fx, fy);
}

bool TerrainCollision::IsHole(float x, float y) const {
  int   cell_row, cell_col;
  float fx, fy;
  const ChunkData* chunk = ResolveChunk(x, y, cell_row, cell_col, fx, fy);
  if (!chunk) return false;
  return IsTerrainHoleCell(chunk->holes, cell_row, cell_col);
}

void TerrainCollision::VisitChunksOverlappingBounds(
    const float min_x, const float max_x,
    const float min_y, const float max_y,
    const ChunkVisitor& visitor) const {
  if (!visitor || min_x > max_x || min_y > max_y) {
    return;
  }

  for (const auto& [tile_key, tile] : tiles_) {
    (void)tile_key;
    for (const ChunkData& chunk : tile.chunks) {
      const float chunk_min_x = chunk.base_x - kChunkSize;
      const float chunk_max_x = chunk.base_x;
      const float chunk_min_y = chunk.base_y - kChunkSize;
      const float chunk_max_y = chunk.base_y;
      if (max_x < chunk_min_x || min_x > chunk_max_x ||
          max_y < chunk_min_y || min_y > chunk_max_y) {
        continue;
      }

      visitor(ChunkView{
          .base_x = chunk.base_x,
          .base_y = chunk.base_y,
          .base_z = chunk.base_z,
          .heights = chunk.heights.data(),
          .holes = chunk.holes,
      });
    }
  }
}

void TerrainCollision::VisitFacets(
    const float min_x, const float max_x,
    const float min_y, const float max_y,
    const float min_z, const float max_z,
    const CollisionFacetVisitor& visitor) const {
  if (!visitor || min_x > max_x || min_y > max_y || min_z > max_z) {
    return;
  }

  const auto tile_index = [](const float coordinate) {
    return static_cast<std::int32_t>(
        std::floor(kMapMidPoint - coordinate / kTileSize));
  };
  const std::int32_t tile_x_begin =
      std::clamp(tile_index(max_x), 0, 63);
  const std::int32_t tile_x_end =
      std::clamp(tile_index(min_x), 0, 63);
  const std::int32_t tile_y_begin =
      std::clamp(tile_index(max_y), 0, 63);
  const std::int32_t tile_y_end =
      std::clamp(tile_index(min_y), 0, 63);

  for (std::int32_t tile_x = tile_x_begin; tile_x <= tile_x_end;
       ++tile_x) {
    for (std::int32_t tile_y = tile_y_begin; tile_y <= tile_y_end;
         ++tile_y) {
      const auto found = tiles_.find(TileKey{tile_x, tile_y});
      if (found == tiles_.end()) {
        continue;
      }
      const TileData& tile = found->second;
      const float tile_max_x =
          static_cast<float>(32 - tile_x) * kTileSize;
      const float tile_max_y =
          static_cast<float>(32 - tile_y) * kTileSize;
      const int chunk_row_begin = std::clamp(
          static_cast<int>(std::floor((tile_max_x - max_x) / kChunkSize)),
          0, kChunksPerSide - 1);
      const int chunk_row_end = std::clamp(
          static_cast<int>(std::floor((tile_max_x - min_x) / kChunkSize)),
          0, kChunksPerSide - 1);
      const int chunk_column_begin = std::clamp(
          static_cast<int>(std::floor((tile_max_y - max_y) / kChunkSize)),
          0, kChunksPerSide - 1);
      const int chunk_column_end = std::clamp(
          static_cast<int>(std::floor((tile_max_y - min_y) / kChunkSize)),
          0, kChunksPerSide - 1);

      for (int chunk_row = chunk_row_begin; chunk_row <= chunk_row_end;
           ++chunk_row) {
        for (int chunk_column = chunk_column_begin;
             chunk_column <= chunk_column_end; ++chunk_column) {
          const std::size_t chunk_index =
              TerrainChunkStorageIndex({chunk_row, chunk_column});
      const ChunkData& chunk = tile.chunks[chunk_index];
      const float chunk_min_x = chunk.base_x - kChunkSize;
      const float chunk_max_x = chunk.base_x;
      const float chunk_min_y = chunk.base_y - kChunkSize;
      const float chunk_max_y = chunk.base_y;
      if (max_x < chunk_min_x || min_x > chunk_max_x ||
          max_y < chunk_min_y || min_y > chunk_max_y) {
        continue;
      }

      const auto outer = [&chunk](const int row, const int column) {
        return std::array<float, 3>{
            chunk.base_x - static_cast<float>(row) * kUnitSize,
            chunk.base_y - static_cast<float>(column) * kUnitSize,
            chunk.base_z +
                chunk.heights[static_cast<std::size_t>(row * 17 + column)]};
      };
      const auto center = [&chunk](const int row, const int column) {
        return std::array<float, 3>{
            chunk.base_x - (static_cast<float>(row) + 0.5f) * kUnitSize,
            chunk.base_y - (static_cast<float>(column) + 0.5f) * kUnitSize,
            chunk.base_z + chunk.heights[static_cast<std::size_t>(
                               row * 17 + 9 + column)]};
      };
      const std::uint64_t owner_id =
          0x0100000000000000ull |
          (static_cast<std::uint64_t>(
               static_cast<std::uint32_t>(tile.tile_x) & 0xffffu)
           << 32u) |
          (static_cast<std::uint64_t>(
               static_cast<std::uint32_t>(tile.tile_y) & 0xffffu)
           << 16u) |
          static_cast<std::uint64_t>(chunk_index);

      const auto emit = [&](std::array<float, 3> a,
                            std::array<float, 3> b,
                            std::array<float, 3> c,
                            const std::uint64_t facet_id) {
        const float triangle_min_x = std::min({a[0], b[0], c[0]});
        const float triangle_max_x = std::max({a[0], b[0], c[0]});
        const float triangle_min_y = std::min({a[1], b[1], c[1]});
        const float triangle_max_y = std::max({a[1], b[1], c[1]});
        const float triangle_min_z = std::min({a[2], b[2], c[2]});
        const float triangle_max_z = std::max({a[2], b[2], c[2]});
        if (triangle_min_x > max_x || triangle_max_x < min_x ||
            triangle_min_y > max_y || triangle_max_y < min_y ||
            triangle_min_z > max_z || triangle_max_z < min_z) {
          return;
        }

        const float e1x = b[0] - a[0];
        const float e1y = b[1] - a[1];
        const float e1z = b[2] - a[2];
        const float e2x = c[0] - a[0];
        const float e2y = c[1] - a[1];
        const float e2z = c[2] - a[2];
        std::array<float, 3> normal{
            e1y * e2z - e1z * e2y,
            e1z * e2x - e1x * e2z,
            e1x * e2y - e1y * e2x};
        if (normal[2] < 0.0f) {
          normal[0] = -normal[0];
          normal[1] = -normal[1];
          normal[2] = -normal[2];
        }
        const float length = std::sqrt(normal[0] * normal[0] +
                                       normal[1] * normal[1] +
                                       normal[2] * normal[2]);
        if (length < 1.0e-8f) {
          return;
        }
        normal[0] /= length;
        normal[1] /= length;
        normal[2] /= length;
        CollisionFacetView facet;
        facet.vertices[0] = a;
        facet.vertices[1] = b;
        facet.vertices[2] = c;
        facet.normal = normal;
        facet.owner_id = owner_id;
        facet.facet_id = facet_id;
        visitor(facet);
      };

      for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 8; ++column) {
          if (IsTerrainHoleCell(chunk.holes, row, column)) {
            continue;
          }
          const auto tl = outer(row, column);
          const auto tr = outer(row, column + 1);
          const auto bl = outer(row + 1, column);
          const auto br = outer(row + 1, column + 1);
          const auto middle = center(row, column);
          const std::uint32_t base_id =
              static_cast<std::uint32_t>((row * 8 + column) * 4);
          emit(bl, middle, tl, base_id);
          emit(middle, tr, tl, base_id + 1u);
          emit(middle, bl, br, base_id + 2u);
          emit(middle, br, tr, base_id + 3u);
        }
      }
        }
      }
    }
  }
}

const TerrainCollision::ChunkData* TerrainCollision::ResolveChunk(
    float x, float y, int& cell_row, int& cell_col, float& fx,
    float& fy) const {

  const int32_t tx =
      static_cast<int32_t>(std::floor(kMapMidPoint - x / kTileSize));
  const int32_t ty =
      static_cast<int32_t>(std::floor(kMapMidPoint - y / kTileSize));

  auto it = tiles_.find(TileKey{tx, ty});
  if (it == tiles_.end()) return nullptr;

  const TileData& tile = it->second;

  const float tile_max_x = static_cast<float>(32 - tx) * kTileSize;
  const float tile_max_y = static_cast<float>(32 - ty) * kTileSize;

  const int chunk_iy =
      static_cast<int>(std::floor((tile_max_x - x) / kChunkSize));
  const int chunk_ix =
      static_cast<int>(std::floor((tile_max_y - y) / kChunkSize));

  if (chunk_iy < 0 || chunk_iy > 15 || chunk_ix < 0 || chunk_ix > 15)
    return nullptr;

  const ChunkData& chunk =
      tile.chunks[TerrainChunkStorageIndex({chunk_iy, chunk_ix})];

  const float rel_x = (chunk.base_x - x) / kUnitSize;
  const float rel_y = (chunk.base_y - y) / kUnitSize;

  if (rel_x < 0.0f || rel_x > 8.0f || rel_y < 0.0f || rel_y > 8.0f)
    return nullptr;

  cell_row = std::min(static_cast<int>(rel_x), 7);
  cell_col = std::min(static_cast<int>(rel_y), 7);

  fx = rel_x - static_cast<float>(cell_row);
  fy = rel_y - static_cast<float>(cell_col);

  return &chunk;
}

float InterpolateTerrainHeightDelta(
    const std::array<float, data::terrain::kVerticesPerChunk>& heights,
    const int cell_row, const int cell_col, const float fx, const float fy) {
  const auto& h = heights;
  const int r = cell_row;
  const int c = cell_col;

  const float h_tl = h[static_cast<size_t>(r * 17 + c)];
  const float h_tr = h[static_cast<size_t>(r * 17 + c + 1)];
  const float h_bl = h[static_cast<size_t>((r + 1) * 17 + c)];
  const float h_br = h[static_cast<size_t>((r + 1) * 17 + c + 1)];
  const float h_c  = h[static_cast<size_t>(r * 17 + 9 + c)];

  if (fx + fy < 1.0f) {
    if (fx < fy) {

      return (1.0f - fy - fx) * h_tl + (fy - fx) * h_tr + 2.0f * fx * h_c;
    }

    return (1.0f - fx - fy) * h_tl + (fx - fy) * h_bl + 2.0f * fy * h_c;
  }

  if (fx < fy) {

    return (fy - fx) * h_tr + (fx + fy - 1.0f) * h_br +
           2.0f * (1.0f - fy) * h_c;
  }

  return (fx - fy) * h_bl + (fx + fy - 1.0f) * h_br +
         2.0f * (1.0f - fx) * h_c;
}

std::array<float, 3> TerrainCollision::ComputeTriangleNormal(
    const ChunkData& chunk, int cell_row, int cell_col, float fx, float fy) {

  const float bx = chunk.base_x;
  const float by = chunk.base_y;
  const float bz = chunk.base_z;
  const float u  = kUnitSize;
  const int   r  = cell_row;
  const int   c  = cell_col;

  auto Outer = [&](int rr, int cc) -> std::array<float, 3> {
    return {bx - static_cast<float>(rr) * u,
            by - static_cast<float>(cc) * u,
            bz + chunk.heights[static_cast<size_t>(rr * 17 + cc)]};
  };
  auto Center = [&](int rr, int cc) -> std::array<float, 3> {
    return {bx - (static_cast<float>(rr) + 0.5f) * u,
            by - (static_cast<float>(cc) + 0.5f) * u,
            bz + chunk.heights[static_cast<size_t>(rr * 17 + 9 + cc)]};
  };

  std::array<float, 3> v0{}, v1{}, v2{};

  if (fx + fy < 1.0f) {
    if (fx < fy) {

      v0 = Outer(r, c);
      v1 = Outer(r, c + 1);
      v2 = Center(r, c);
    } else {

      v0 = Outer(r, c);
      v1 = Outer(r + 1, c);
      v2 = Center(r, c);
    }
  } else {
    if (fx < fy) {

      v0 = Outer(r, c + 1);
      v1 = Outer(r + 1, c + 1);
      v2 = Center(r, c);
    } else {

      v0 = Outer(r + 1, c);
      v1 = Outer(r + 1, c + 1);
      v2 = Center(r, c);
    }
  }

  const float e1x = v1[0] - v0[0], e1y = v1[1] - v0[1], e1z = v1[2] - v0[2];
  const float e2x = v2[0] - v0[0], e2y = v2[1] - v0[1], e2z = v2[2] - v0[2];

  float nx = e1y * e2z - e1z * e2y;
  float ny = e1z * e2x - e1x * e2z;
  float nz = e1x * e2y - e1y * e2x;

  if (nz < 0.0f) {
    nx = -nx;
    ny = -ny;
    nz = -nz;
  }

  const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
  if (len > 1e-8f) {
    nx /= len;
    ny /= len;
    nz /= len;
  } else {
    nx = 0.0f;
    ny = 0.0f;
    nz = 1.0f;
  }

  return {nx, ny, nz};
}

std::optional<RayHit> TerrainCollision::RaycastTerrain(
    float ox, float oy, float oz,
    float dx, float dy, float dz,
    float max_dist) const {
  if (tiles_.empty()) return std::nullopt;

  const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
  if (len < 1e-8f) return std::nullopt;
  const float inv_len = 1.0f / len;
  dx *= inv_len;
  dy *= inv_len;
  dz *= inv_len;

  constexpr float kCoarseStep = 0.5f;
  constexpr int kMaxCoarseSteps = 4000;
  constexpr int kBinaryRefinements = 12;

  float prev_t = 0.0f;
  auto prev_terrain_z = GetHeight(ox, oy);
  bool prev_below = prev_terrain_z.has_value() && oz < *prev_terrain_z;

  const int max_steps = std::min(
      kMaxCoarseSteps,
      static_cast<int>(max_dist / kCoarseStep) + 1);

  for (int step = 1; step <= max_steps; ++step) {
    const float t = static_cast<float>(step) * kCoarseStep;
    if (t > max_dist) break;

    const float rx = ox + dx * t;
    const float ry = oy + dy * t;
    const float rz = oz + dz * t;

    auto terrain_z = GetHeight(rx, ry);
    if (!terrain_z) {

      prev_t = t;
      prev_terrain_z = std::nullopt;
      prev_below = false;
      continue;
    }

    const bool cur_below = rz < *terrain_z;

    if (!prev_below && cur_below && prev_terrain_z.has_value()) {

      float lo = prev_t;
      float hi = t;
      float mid_x = rx, mid_y = ry, mid_z = rz;
      float mid_terrain_z = *terrain_z;

      for (int ref = 0; ref < kBinaryRefinements; ++ref) {
        const float mid_t = (lo + hi) * 0.5f;
        mid_x = ox + dx * mid_t;
        mid_y = oy + dy * mid_t;
        mid_z = oz + dz * mid_t;

        auto h = GetHeight(mid_x, mid_y);
        if (!h) {
          lo = mid_t;
          continue;
        }
        mid_terrain_z = *h;

        if (mid_z < mid_terrain_z) {
          hi = mid_t;
        } else {
          lo = mid_t;
        }
      }

      const float final_t = (lo + hi) * 0.5f;
      const float fx = ox + dx * final_t;
      const float fy = oy + dy * final_t;
      auto fh = GetHeight(fx, fy);
      const float fz = fh.value_or(mid_terrain_z);

      auto normal = GetNormal(fx, fy);

      RayHit hit;
      hit.x = fx;
      hit.y = fy;
      hit.z = fz;
      hit.distance = final_t;
      if (normal) {
        hit.normal[0] = (*normal)[0];
        hit.normal[1] = (*normal)[1];
        hit.normal[2] = (*normal)[2];
      } else {
        hit.normal[0] = 0.0f;
        hit.normal[1] = 0.0f;
        hit.normal[2] = 1.0f;
      }
      return hit;
    }

    prev_t = t;
    prev_terrain_z = terrain_z;
    prev_below = cur_below;
  }

  return std::nullopt;
}

namespace {

bool RayIntersectsTriangle(
    const float ox, const float oy, const float oz,
    const float dx, const float dy, const float dz,
    const float* v0, const float* v1, const float* v2,
    float& t, float out_normal[3]) {
  constexpr float kEpsilon = 1e-7f;

  const float e1x = v1[0] - v0[0], e1y = v1[1] - v0[1], e1z = v1[2] - v0[2];
  const float e2x = v2[0] - v0[0], e2y = v2[1] - v0[1], e2z = v2[2] - v0[2];

  const float px = dy * e2z - dz * e2y;
  const float py = dz * e2x - dx * e2z;
  const float pz = dx * e2y - dy * e2x;

  const float det = e1x * px + e1y * py + e1z * pz;
  if (std::fabs(det) < kEpsilon) return false;

  const float inv_det = 1.0f / det;
  const float tx = ox - v0[0], ty = oy - v0[1], tz = oz - v0[2];
  const float u = (tx * px + ty * py + tz * pz) * inv_det;
  if (u < 0.0f || u > 1.0f) return false;

  const float qx = ty * e1z - tz * e1y;
  const float qy = tz * e1x - tx * e1z;
  const float qz = tx * e1y - ty * e1x;
  const float v = (dx * qx + dy * qy + dz * qz) * inv_det;
  if (v < 0.0f || u + v > 1.0f) return false;

  t = (e2x * qx + e2y * qy + e2z * qz) * inv_det;
  if (t < 0.0f) return false;

  float nx = e1y * e2z - e1z * e2y;
  float ny = e1z * e2x - e1x * e2z;
  float nz = e1x * e2y - e1y * e2x;
  const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
  if (len > 1e-12f) {
    nx /= len; ny /= len; nz /= len;
    if (nx * dx + ny * dy + nz * dz > 0.0f) {
      nx = -nx; ny = -ny; nz = -nz;
    }
  } else {
    nx = 0.0f; ny = 0.0f; nz = 1.0f;
  }
  out_normal[0] = nx; out_normal[1] = ny; out_normal[2] = nz;
  return true;
}

}

std::optional<RayHit> CollisionManager::RaycastWmoFacets(
    const float ox, const float oy, const float oz,
    const float dx, const float dy, const float dz,
    const float max_dist) const {
  if (!wmo_facet_gather_ || max_dist <= 0.0f) {
    return std::nullopt;
  }
  const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
  if (len < 1e-8f) {
    return std::nullopt;
  }
  const float ux = dx / len, uy = dy / len, uz = dz / len;
  const float ex = ox + ux * max_dist;
  const float ey = oy + uy * max_dist;
  const float ez = oz + uz * max_dist;
  const std::array<float, 6> bounds{
      std::min(ox, ex), std::min(oy, ey), std::min(oz, ez),
      std::max(ox, ex), std::max(oy, ey), std::max(oz, ez)};

  std::optional<RayHit> best;
  wmo_facet_gather_(bounds, [&](const CollisionFacetView& facet) {
    float t = 0.0f;
    float normal[3];
    if (!RayIntersectsTriangle(ox, oy, oz, ux, uy, uz,
                               facet.vertices[0].data(),
                               facet.vertices[1].data(),
                               facet.vertices[2].data(), t, normal)) {
      return;
    }
    if (t > max_dist || (best && t >= best->distance)) {
      return;
    }
    RayHit hit;
    hit.distance = t;
    hit.x = ox + ux * t;
    hit.y = oy + uy * t;
    hit.z = oz + uz * t;
    hit.normal[0] = normal[0];
    hit.normal[1] = normal[1];
    hit.normal[2] = normal[2];
    best = hit;
  });
  return best;
}

std::optional<float> CollisionManager::GetGroundHeight(float x, float y,
                                                       float z) const {

  constexpr float kSurfaceEpsilon = 0.01f;
  std::optional<float> terrain_z = terrain_.GetHeight(x, y);
  if (terrain_z.has_value() && *terrain_z > z + kSurfaceEpsilon) {
    terrain_z.reset();
  }

  constexpr float kWmoGroundProbeDepth = 200.0f;
  std::optional<float> wmo_z;
  if (const auto hit =
          RaycastWmoFacets(x, y, z, 0.0f, 0.0f, -1.0f, kWmoGroundProbeDepth);
      hit.has_value()) {
    wmo_z = hit->z;
  }

  if (terrain_z && wmo_z) return std::max(*terrain_z, *wmo_z);
  if (terrain_z) return terrain_z;
  return wmo_z;
}

std::optional<RayHit> CollisionManager::Raycast(float ox, float oy, float oz,
                                                float dx, float dy, float dz,
                                                float max_dist) const {
  std::optional<RayHit> best;

  auto terrain_hit = terrain_.RaycastTerrain(ox, oy, oz, dx, dy, dz, max_dist);
  if (terrain_hit) {
    best = terrain_hit;
  }

  if (auto wmo_hit = RaycastWmoFacets(ox, oy, oz, dx, dy, dz, max_dist);
      wmo_hit.has_value()) {
    if (!best || wmo_hit->distance < best->distance) {
      best = wmo_hit;
    }
  }

  return best;
}

bool CollisionManager::RaycastWorld(float ox, float oy, float oz,
                                    float dx, float dy, float dz,
                                    float max_dist,
                                    float& hit_x, float& hit_y,
                                    float& hit_z) const {
  auto hit = Raycast(ox, oy, oz, dx, dy, dz, max_dist);
  if (!hit) return false;
  hit_x = hit->x;
  hit_y = hit->y;
  hit_z = hit->z;
  return true;
}

void CollisionManager::VisitFacets(
    const float min_x, const float max_x,
    const float min_y, const float max_y,
    const float min_z, const float max_z,
    const CollisionFacetVisitor& visitor) const {
  if (!visitor) {
    return;
  }
  terrain_.VisitFacets(min_x, max_x, min_y, max_y, min_z, max_z, visitor);
  if (wmo_facet_gather_) {
    wmo_facet_gather_({min_x, min_y, min_z, max_x, max_y, max_z}, visitor);
  }
}

std::uint64_t CollisionManager::FacetRevision() const {
  const std::uint64_t terrain_revision = terrain_.FacetRevision();
  const std::uint64_t wmo_revision =
      wmo_facet_revision_ ? wmo_facet_revision_() : 0u;
  return terrain_revision * 0x9e3779b97f4a7c15ull ^
         (wmo_revision + 0x517cc1b727220a95ull +
          (terrain_revision << 6u) + (terrain_revision >> 2u));
}

}

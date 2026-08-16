#include "openwow/data/map/heightmap_query.h"

#include <algorithm>
#include <cmath>

namespace openwow::data::map {

void HeightmapQuery::WorldToTile(float worldX, float worldY,
                                 uint32_t& tileX, uint32_t& tileY) {

  float fx = kMapMid - worldX / kTileSize;
  float fy = kMapMid - worldY / kTileSize;
  tileX = static_cast<uint32_t>(std::max(0.0f, std::floor(fx)));
  tileY = static_cast<uint32_t>(std::max(0.0f, std::floor(fy)));
  if (tileX > 63) tileX = 63;
  if (tileY > 63) tileY = 63;
}

void HeightmapQuery::WorldToChunk(float worldX, float worldY,
                                  uint32_t& tileX, uint32_t& tileY,
                                  uint32_t& chunkX, uint32_t& chunkY) {
  WorldToTile(worldX, worldY, tileX, tileY);

  float tileOriginX = (kMapMid - static_cast<float>(tileX)) * kTileSize;
  float tileOriginY = (kMapMid - static_cast<float>(tileY)) * kTileSize;

  float offX = tileOriginX - worldX;
  float offY = tileOriginY - worldY;

  chunkX = static_cast<uint32_t>(std::clamp(offX / kChunkSize, 0.0f, 15.0f));
  chunkY = static_cast<uint32_t>(std::clamp(offY / kChunkSize, 0.0f, 15.0f));
}

void HeightmapQuery::SetHeightData(uint32_t tileX, uint32_t tileY,
                                   uint32_t chunkX, uint32_t chunkY,
                                   const std::array<float, 145>& heights) {
  if (chunkX >= 16 || chunkY >= 16) return;

  TileKey key{tileX, tileY};
  TileData& td = tiles_[key];
  ChunkData& cd = td.chunks[chunkY * 16 + chunkX];
  cd.heights = heights;
  cd.loaded = true;

  hot_tile_ptr_ = nullptr;
}

void HeightmapQuery::SetTileHeights(
    uint32_t tileX, uint32_t tileY,
    const std::array<std::array<float, 145>, 256>& allHeights) {
  TileKey key{tileX, tileY};
  TileData& td = tiles_[key];
  for (uint32_t i = 0; i < 256; ++i) {
    td.chunks[i].heights = allHeights[i];
    td.chunks[i].loaded = true;
  }

  hot_tile_ptr_ = nullptr;
}

void HeightmapQuery::ClearTile(uint32_t tileX, uint32_t tileY) {
  tiles_.erase(TileKey{tileX, tileY});

  hot_tile_ptr_ = nullptr;
}

void HeightmapQuery::Clear() {
  tiles_.clear();
  hot_tile_ptr_ = nullptr;
}

const HeightmapQuery::ChunkData* HeightmapQuery::ResolveChunk(
    float worldX, float worldY, float& localX, float& localY) const {
  uint32_t tx, ty, cx, cy;
  WorldToChunk(worldX, worldY, tx, ty, cx, cy);

  const TileData* td = nullptr;
  if (hot_tile_ptr_ != nullptr &&
      hot_tile_key_.x == tx && hot_tile_key_.y == ty) {
    td = hot_tile_ptr_;
  } else {
    auto it = tiles_.find(TileKey{tx, ty});
    if (it == tiles_.end()) return nullptr;
    td = &it->second;
    hot_tile_key_ = TileKey{tx, ty};
    hot_tile_ptr_ = td;
  }

  const ChunkData& cd = td->chunks[cy * 16 + cx];
  if (!cd.loaded) return nullptr;

  float tileOriginX = (kMapMid - static_cast<float>(tx)) * kTileSize;
  float tileOriginY = (kMapMid - static_cast<float>(ty)) * kTileSize;
  float chunkOriginX = tileOriginX - static_cast<float>(cx) * kChunkSize;
  float chunkOriginY = tileOriginY - static_cast<float>(cy) * kChunkSize;

  localX = chunkOriginX - worldX;
  localY = chunkOriginY - worldY;

  localX = std::clamp(localX, 0.0f, kChunkSize - 0.001f);
  localY = std::clamp(localY, 0.0f, kChunkSize - 0.001f);

  return &cd;
}

float HeightmapQuery::InterpolateInChunk(const ChunkData& chunk,
                                         float localX, float localY) {

  float cellX = localX / kUnitSize;
  float cellY = localY / kUnitSize;

  int col = static_cast<int>(cellX);
  int row = static_cast<int>(cellY);
  col = std::clamp(col, 0, 7);
  row = std::clamp(row, 0, 7);

  float fx = cellX - static_cast<float>(col);
  float fy = cellY - static_cast<float>(row);

  auto outerIdx = [](int r, int c) -> int { return r * 9 + c; };
  auto innerIdx = [](int r, int c) -> int { return 81 + r * 8 + c; };

  float h_tl = chunk.heights[outerIdx(row, col)];
  float h_tr = chunk.heights[outerIdx(row, col + 1)];
  float h_bl = chunk.heights[outerIdx(row + 1, col)];
  float h_br = chunk.heights[outerIdx(row + 1, col + 1)];
  float h_c  = chunk.heights[innerIdx(row, col)];

  if (fx + fy < 1.0f) {
    if (fx < fy) {

      return h_tl + (h_c - h_tl) * fx + (h_bl - h_tl) * fy;
    } else {

      return h_tl + (h_tr - h_tl) * fx + (h_c - h_tl) * fy;
    }
  } else {
    if (fx < fy) {

      float rfx = 1.0f - fx;
      float rfy = 1.0f - fy;
      return h_br + (h_bl - h_br) * rfx + (h_c - h_br) * rfy;
    } else {

      float rfx = 1.0f - fx;
      float rfy = 1.0f - fy;
      return h_br + (h_c - h_br) * rfx + (h_tr - h_br) * rfy;
    }
  }
}

std::optional<float> HeightmapQuery::GetHeight(float worldX, float worldY) const {
  float lx, ly;
  const ChunkData* chunk = ResolveChunk(worldX, worldY, lx, ly);
  if (!chunk) return std::nullopt;
  return InterpolateInChunk(*chunk, lx, ly);
}

std::optional<HeightmapNormal> HeightmapQuery::GetNormal(float worldX, float worldY) const {

  constexpr float kEps = 0.5f;

  auto hc = GetHeight(worldX, worldY);
  if (!hc) return std::nullopt;

  float hpx = GetHeight(worldX + kEps, worldY).value_or(*hc);
  float hmx = GetHeight(worldX - kEps, worldY).value_or(*hc);
  float hpy = GetHeight(worldX, worldY + kEps).value_or(*hc);
  float hmy = GetHeight(worldX, worldY - kEps).value_or(*hc);

  float dzdx = (hpx - hmx) / (2.0f * kEps);
  float dzdy = (hpy - hmy) / (2.0f * kEps);

  float nx = -dzdx;
  float ny = -dzdy;
  float nz = 1.0f;
  float len = std::sqrt(nx * nx + ny * ny + nz * nz);
  if (len > 0.0001f) {
    nx /= len;
    ny /= len;
    nz /= len;
  }
  return HeightmapNormal{nx, ny, nz};
}

bool HeightmapQuery::HasData(float worldX, float worldY) const {
  float lx, ly;
  return ResolveChunk(worldX, worldY, lx, ly) != nullptr;
}

std::optional<float> HeightmapQuery::GetSlope(float worldX, float worldY) const {
  auto normal = GetNormal(worldX, worldY);
  if (!normal) return std::nullopt;

  float cosAngle = std::clamp(normal->z, -1.0f, 1.0f);
  float angleRad = std::acos(cosAngle);
  return angleRad * (180.0f / 3.14159265f);
}

uint32_t HeightmapQuery::GetLoadedTileCount() const {
  return static_cast<uint32_t>(tiles_.size());
}

std::optional<float> HeightmapQuery::GetHeightAt(float x, float y) const {
  return GetHeight(x, y);
}

bool HeightmapQuery::IsLoaded(float x, float y) const {
  return HasData(x, y);
}

}

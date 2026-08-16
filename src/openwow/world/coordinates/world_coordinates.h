#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace openwow::world {

inline constexpr float kMapHalfSize = 17066.666f;

inline constexpr float kWorldGridCenter = 17066.666f;

inline constexpr float kMapFullSize  = 34133.332f;

inline constexpr float kTileSize     = 533.33333f;

inline constexpr int32_t kTilesPerSide = 64;

inline constexpr float kTileGridCenter = 32.0f;

inline constexpr int32_t kChunksPerTileSide = 16;

inline constexpr int32_t kChunksPerTile = kChunksPerTileSide * kChunksPerTileSide;

inline constexpr float kChunkSize = 33.333332f;

inline constexpr int32_t kCellsPerChunkSide = 8;

inline constexpr float kCellSize = kChunkSize / static_cast<float>(kCellsPerChunkSide);

inline constexpr int32_t kOuterGridSize = 9;

inline constexpr int32_t kInnerGridSize = 8;

inline constexpr int32_t kVerticesPerChunk = kOuterGridSize * kOuterGridSize
                                           + kInnerGridSize * kInnerGridSize;

inline constexpr float kInvTileSize  = 1.0f / kTileSize;

inline constexpr float kWorldToChunkScale = 0.03f;
inline constexpr float kInvChunkSize = kWorldToChunkScale;

[[nodiscard]] inline float WorldToContinuousChunkCoordinate(
    const float coordinate) noexcept {
  return -(coordinate - kWorldGridCenter) * kWorldToChunkScale;
}

inline constexpr float kInvCellSize  = 1.0f / kCellSize;

struct TileCoord {
  int32_t x = 0;
  int32_t y = 0;

  constexpr bool operator==(const TileCoord& o) const { return x == o.x && y == o.y; }
  constexpr bool operator!=(const TileCoord& o) const { return !(*this == o); }
};

struct MapFileTileCoord {
  int32_t x = 0;
  int32_t y = 0;
};

[[nodiscard]] constexpr MapFileTileCoord ToMapFileTile(const TileCoord world_tile) {
  return {world_tile.y, world_tile.x};
}

[[nodiscard]] constexpr TileCoord FromMapFileTile(const MapFileTileCoord file_tile) {
  return {file_tile.y, file_tile.x};
}

struct TileCoordHash {
  std::size_t operator()(const TileCoord& t) const {
    auto h = (static_cast<int64_t>(t.x) << 32) | static_cast<uint32_t>(t.y);
    return std::hash<int64_t>{}(h);
  }
};

struct ChunkCoord {
  int32_t x = 0;
  int32_t y = 0;
};

struct CellCoord {
  int32_t x = 0;
  int32_t y = 0;
};

struct WorldAddress {
  TileCoord  tile;
  ChunkCoord chunk;
  CellCoord  cell;
  float      offsetX = 0.0f;
  float      offsetY = 0.0f;
};

[[nodiscard]] constexpr std::size_t TerrainChunkStorageIndex(
    const ChunkCoord chunk) noexcept {
  return static_cast<std::size_t>(chunk.x * kChunksPerTileSide + chunk.y);
}

[[nodiscard]] constexpr std::size_t TerrainCellStorageIndex(
    const CellCoord cell) noexcept {
  return static_cast<std::size_t>(cell.x * kCellsPerChunkSide + cell.y);
}

[[nodiscard]] constexpr bool IsTerrainHoleCell(
    const std::uint32_t holes, const int world_x_cell,
    const int world_y_cell) noexcept {
  if (world_x_cell < 0 || world_x_cell >= kCellsPerChunkSide ||
      world_y_cell < 0 || world_y_cell >= kCellsPerChunkSide) {
    return false;
  }
  const int hole_x = world_x_cell >> 1;
  const int hole_y = world_y_cell >> 1;
  return (holes & (1u << (hole_x * 4 + hole_y))) != 0u;
}

inline TileCoord WorldToTile(float worldX, float worldY) {
  return {
    static_cast<int32_t>(std::floor(kTileGridCenter - worldX / kTileSize)),
    static_cast<int32_t>(std::floor(kTileGridCenter - worldY / kTileSize))
  };
}

inline std::pair<float, float> TileToWorld(TileCoord tile) {
  return {
    (kTileGridCenter - static_cast<float>(tile.x)) * kTileSize,
    (kTileGridCenter - static_cast<float>(tile.y)) * kTileSize
  };
}

inline std::pair<float, float> TileCenterToWorld(TileCoord tile) {
  return {
    (kTileGridCenter - (static_cast<float>(tile.x) + 0.5f)) * kTileSize,
    (kTileGridCenter - (static_cast<float>(tile.y) + 0.5f)) * kTileSize
  };
}

inline void WorldToChunk(float worldX, float worldY,
                         TileCoord& tile, ChunkCoord& chunk) {
  tile = WorldToTile(worldX, worldY);

  const float localX = -(worldX - kMapHalfSize);
  const float localY = -(worldY - kMapHalfSize);

  const float tileOriginX = static_cast<float>(tile.x) * kTileSize;
  const float tileOriginY = static_cast<float>(tile.y) * kTileSize;

  chunk.x = static_cast<int32_t>((localX - tileOriginX) / kChunkSize);
  chunk.y = static_cast<int32_t>((localY - tileOriginY) / kChunkSize);

  chunk.x = std::max(0, std::min(kChunksPerTileSide - 1, chunk.x));
  chunk.y = std::max(0, std::min(kChunksPerTileSide - 1, chunk.y));
}

inline WorldAddress WorldToAddress(float worldX, float worldY) {
  WorldAddress addr;
  WorldToChunk(worldX, worldY, addr.tile, addr.chunk);

  const float localX = -(worldX - kMapHalfSize);
  const float localY = -(worldY - kMapHalfSize);

  const float tileOriginX = static_cast<float>(addr.tile.x) * kTileSize;
  const float tileOriginY = static_cast<float>(addr.tile.y) * kTileSize;

  const float chunkLocalX = localX - tileOriginX
                           - static_cast<float>(addr.chunk.x) * kChunkSize;
  const float chunkLocalY = localY - tileOriginY
                           - static_cast<float>(addr.chunk.y) * kChunkSize;

  addr.cell.x = static_cast<int32_t>(chunkLocalX / kCellSize);
  addr.cell.y = static_cast<int32_t>(chunkLocalY / kCellSize);
  addr.cell.x = std::max(0, std::min(kCellsPerChunkSide - 1, addr.cell.x));
  addr.cell.y = std::max(0, std::min(kCellsPerChunkSide - 1, addr.cell.y));

  addr.offsetX = (chunkLocalX - static_cast<float>(addr.cell.x) * kCellSize)
                 / kCellSize;
  addr.offsetY = (chunkLocalY - static_cast<float>(addr.cell.y) * kCellSize)
                 / kCellSize;
  addr.offsetX = std::max(0.0f, std::min(1.0f, addr.offsetX));
  addr.offsetY = std::max(0.0f, std::min(1.0f, addr.offsetY));

  return addr;
}

inline bool IsValidTile(TileCoord tile) {
  return tile.x >= 0 && tile.x < kTilesPerSide
      && tile.y >= 0 && tile.y < kTilesPerSide;
}

inline bool IsValidMapCoord(float worldX, float worldY, float worldZ,
                            float edgeMargin = 0.0f) {
  if (!std::isfinite(worldX) || !std::isfinite(worldY) || !std::isfinite(worldZ)) {
    return false;
  }

  const float yFromUpperEdge = kMapHalfSize - worldY;
  const float xFromUpperEdge = kMapHalfSize - worldX;
  return edgeMargin <= yFromUpperEdge
      && kMapFullSize - edgeMargin > yFromUpperEdge
      && edgeMargin <= xFromUpperEdge
      && xFromUpperEdge < kMapFullSize - edgeMargin;
}

inline bool IsValidMapCoord(const float* worldPos, float edgeMargin = 0.0f) {
  return IsValidMapCoord(worldPos[0], worldPos[1], worldPos[2], edgeMargin);
}

inline bool IsInWorldBounds(float worldX, float worldY) {
  return IsValidMapCoord(worldX, worldY, 0.0f, 0.0f);
}

inline float WorldDistance2D(float x1, float y1, float x2, float y2) {
  const float dx = x2 - x1;
  const float dy = y2 - y1;
  return std::sqrt(dx * dx + dy * dy);
}

inline float WorldDistanceSq2D(float x1, float y1, float x2, float y2) {
  const float dx = x2 - x1;
  const float dy = y2 - y1;
  return dx * dx + dy * dy;
}

inline float TileBoundsDistanceSq2D(float worldX, float worldY, TileCoord tile) {
  const auto [tileMaxX, tileMaxY] = TileToWorld(tile);
  const float tileMinX = tileMaxX - kTileSize;
  const float tileMinY = tileMaxY - kTileSize;

  float nearestX = worldX;
  if (nearestX < tileMinX) {
    nearestX = tileMinX;
  } else if (nearestX > tileMaxX) {
    nearestX = tileMaxX;
  }

  float nearestY = worldY;
  if (nearestY < tileMinY) {
    nearestY = tileMinY;
  } else if (nearestY > tileMaxY) {
    nearestY = tileMaxY;
  }

  return WorldDistanceSq2D(worldX, worldY, nearestX, nearestY);
}

inline std::pair<float, float> ChunkCenterToWorld(TileCoord tile,
                                                   ChunkCoord chunk) {

  const float localX = static_cast<float>(tile.x) * kTileSize
                      + (static_cast<float>(chunk.x) + 0.5f) * kChunkSize;
  const float localY = static_cast<float>(tile.y) * kTileSize
                      + (static_cast<float>(chunk.y) + 0.5f) * kChunkSize;
  return {
    -(localX - kMapHalfSize),
    -(localY - kMapHalfSize)
  };
}

std::string MakeWdtFilename(const std::string& map_name);

std::string FormatWorldAddress(const WorldAddress& addr);

std::string FormatWorldPosition(float x, float y, float z);

std::pair<float, float> WorldToMinimapNormalized(float worldX, float worldY,
                                                   float mapBoundsMinX,
                                                   float mapBoundsMinY,
                                                   float mapBoundsMaxX,
                                                   float mapBoundsMaxY);

std::vector<TileCoord> GetNeighborTiles(TileCoord center, int radius = 1);

int CountTilesInRect(float minX, float minY, float maxX, float maxY);

}

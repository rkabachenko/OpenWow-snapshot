
#include "openwow/world/coordinates/world_coordinates.h"

#include <cstdio>
#include <string>

static_assert(openwow::world::kTileSize > 530.0f && openwow::world::kTileSize < 534.0f,
              "kTileSize must be ~533.333");
static_assert(openwow::world::kChunkSize > 33.0f && openwow::world::kChunkSize < 34.0f,
              "kChunkSize must be ~33.333");
static_assert(openwow::world::kCellSize > 4.1f && openwow::world::kCellSize < 4.2f,
              "kCellSize must be ~4.167");
static_assert(openwow::world::kVerticesPerChunk == 145,
              "kVerticesPerChunk must be 145");
static_assert(openwow::world::kChunksPerTile == 256,
              "kChunksPerTile must be 256");

namespace openwow::world {

std::string MakeWdtFilename(const std::string& map_name) {
  char buf[256];
  std::snprintf(buf, sizeof(buf), "World\\Maps\\%s\\%s.wdt",
                map_name.c_str(), map_name.c_str());
  return buf;
}

std::string FormatWorldAddress(const WorldAddress& addr) {
  char buf[128];
  std::snprintf(buf, sizeof(buf),
                "Tile(%d,%d) Chunk(%d,%d) Cell(%d,%d) Offset(%.2f,%.2f)",
                addr.tile.x, addr.tile.y,
                addr.chunk.x, addr.chunk.y,
                addr.cell.x, addr.cell.y,
                addr.offsetX, addr.offsetY);
  return buf;
}

std::string FormatWorldPosition(float x, float y, float z) {
  char buf[96];
  std::snprintf(buf, sizeof(buf), "(%.1f, %.1f, %.1f)", x, y, z);
  return buf;
}

std::pair<float, float> WorldToMinimapNormalized(float worldX, float worldY,
                                                   float mapBoundsMinX,
                                                   float mapBoundsMinY,
                                                   float mapBoundsMaxX,
                                                   float mapBoundsMaxY) {
  float rangeX = mapBoundsMaxX - mapBoundsMinX;
  float rangeY = mapBoundsMaxY - mapBoundsMinY;
  if (rangeX <= 0.0f) rangeX = 1.0f;
  if (rangeY <= 0.0f) rangeY = 1.0f;
  float nx = (worldX - mapBoundsMinX) / rangeX;
  float ny = (worldY - mapBoundsMinY) / rangeY;
  return {nx, ny};
}

std::vector<TileCoord> GetNeighborTiles(TileCoord center, int radius) {
  std::vector<TileCoord> result;
  for (int dy = -radius; dy <= radius; ++dy) {
    for (int dx = -radius; dx <= radius; ++dx) {
      TileCoord t{center.x + dx, center.y + dy};
      if (IsValidTile(t)) {
        result.push_back(t);
      }
    }
  }
  return result;
}

int CountTilesInRect(float minX, float minY, float maxX, float maxY) {
  TileCoord tMin = WorldToTile(maxX, maxY);
  TileCoord tMax = WorldToTile(minX, minY);
  tMin.x = std::max(0, tMin.x);
  tMin.y = std::max(0, tMin.y);
  tMax.x = std::min(kTilesPerSide - 1, tMax.x);
  tMax.y = std::min(kTilesPerSide - 1, tMax.y);
  if (tMin.x > tMax.x || tMin.y > tMax.y) return 0;
  return (tMax.x - tMin.x + 1) * (tMax.y - tMin.y + 1);
}

}

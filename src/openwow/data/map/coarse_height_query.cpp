#include "openwow/data/map/coarse_height_query.h"

#include <cmath>

#include "openwow/data/terrain/wdl_file.h"
#include "openwow/world/coordinates/world_coordinates.h"

namespace openwow::data::map {

using openwow::world::kMapHalfSize;
using openwow::world::kTileSize;

static constexpr float kHalfTile = kTileSize / 2.0f;

static constexpr float kVtxPos[9][2] = {
    { 0.0f,       0.0f      },
    { 0.0f,      -kHalfTile },
    { 0.0f,      -kTileSize },
    {-kHalfTile,  0.0f      },
    {-kHalfTile, -kHalfTile },
    {-kHalfTile, -kTileSize },
    {-kTileSize,  0.0f      },
    {-kTileSize, -kHalfTile },
    {-kTileSize, -kTileSize },
};

static constexpr int kTri[8][3] = {
    {3, 0, 4},
    {0, 1, 4},
    {1, 2, 4},
    {2, 5, 4},
    {5, 8, 4},
    {8, 7, 4},
    {7, 6, 4},
    {6, 3, 4},
};

static int SelectTriangle(int halfY, int halfX, float dx, float dy) {
  if (halfY) {
    if (halfX) {
      return (dy >= dx) ? 5 : 4;
    }
    return (dy >= -kTileSize - dx) ? 2 : 3;
  }
  if (halfX) {
    return (dy >= -kTileSize - dx) ? 7 : 6;
  }
  return (dy < dx) ? 1 : 0;
}

void CoarseHeightQuery::SetTileData(int tileX, int tileY,
                                     const std::int16_t layer0[9],
                                     const std::int16_t layer1[9]) {
  if (tileX < 0 || tileX >= kTilesPerSide ||
      tileY < 0 || tileY >= kTilesPerSide)
    return;

  auto& slot = tiles_[tileY][tileX];
  for (int i = 0; i < 9; ++i) {
    slot.heights.layer0[i] = layer0[i];
    slot.heights.layer1[i] = layer1[i];
  }
  slot.loaded = true;
}

void CoarseHeightQuery::ClearTile(int tileX, int tileY) {
  if (tileX < 0 || tileX >= kTilesPerSide ||
      tileY < 0 || tileY >= kTilesPerSide)
    return;
  tiles_[tileY][tileX] = {};
}

void CoarseHeightQuery::Clear() {
  for (auto& row : tiles_)
    for (auto& slot : row)
      slot = {};
  disabled_ = false;
}

void CoarseHeightQuery::PopulateFromWdl(
    const openwow::data::terrain::WdlFile& wdl) {

  static constexpr int kSample[3] = {0, 8, 16};

  for (int ty = 0; ty < kTilesPerSide; ++ty) {
    for (int tx = 0; tx < kTilesPerSide; ++tx) {
      const auto* th = wdl.tile_heights[ty][tx];
      if (!th) continue;

      auto& slot = tiles_[ty][tx];
      for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
          const std::int16_t h = th->outer[kSample[r]][kSample[c]];
          slot.heights.layer0[r * 3 + c] = h;
          slot.heights.layer1[r * 3 + c] = h;
        }
      }
      slot.loaded = true;
    }
  }
}

void CoarseHeightQuery::SetDisabled(bool disabled) { disabled_ = disabled; }
bool CoarseHeightQuery::IsDisabled() const { return disabled_; }

bool CoarseHeightQuery::QueryLayer(const float* pos, float* outHeight,
                                    int layer) const {
  if (disabled_) return false;
  if (layer < 0 || layer > 1) return false;

  const float localX = -(pos[0] - kMapHalfSize);
  const float localY = -(pos[1] - kMapHalfSize);
  const float gridX = localX / kHalfTile;
  const float gridY = localY / kHalfTile;

  const int halfTileX = static_cast<int>(std::floor(gridX));
  const int halfTileY = static_cast<int>(std::floor(gridY));

  const int tileX = halfTileX >> 1;
  const int tileY = halfTileY >> 1;

  if (tileX < 0 || tileX >= kTilesPerSide ||
      tileY < 0 || tileY >= kTilesPerSide)
    return false;

  const auto& slot = tiles_[tileY][tileX];
  if (!slot.loaded) return false;

  const float originX =
      kMapHalfSize - static_cast<float>(tileX) * kTileSize;
  const float originY =
      kMapHalfSize - static_cast<float>(tileY) * kTileSize;
  const float dx = pos[0] - originX;
  const float dy = pos[1] - originY;

  const int halfX = halfTileX & 1;
  const int halfY = halfTileY & 1;
  const int triIdx = SelectTriangle(halfY, halfX, dx, dy);

  const int i0 = kTri[triIdx][0];
  const int i1 = kTri[triIdx][1];
  const int i2 = kTri[triIdx][2];

  const auto& ld =
      (layer == 0) ? slot.heights.layer0 : slot.heights.layer1;

  const float p0x = kVtxPos[i0][0], p0y = kVtxPos[i0][1];
  const float h0  = static_cast<float>(ld[i0]);
  const float p1x = kVtxPos[i1][0], p1y = kVtxPos[i1][1];
  const float h1  = static_cast<float>(ld[i1]);
  const float p2x = kVtxPos[i2][0], p2y = kVtxPos[i2][1];
  const float h2  = static_cast<float>(ld[i2]);

  const float e1x = p1x - p0x, e1y = p1y - p0y, e1h = h1 - h0;
  const float e2x = p2x - p0x, e2y = p2y - p0y, e2h = h2 - h0;

  float nx = e1y * e2h - e1h * e2y;
  float ny = e1h * e2x - e2h * e1x;
  float nz = e1x * e2y - e1y * e2x;

  const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
  if (len == 0.0f) return false;
  nx /= len;
  ny /= len;
  nz /= len;

  *outHeight =
      (ny * dy - (p0x * nx + p0y * ny + h0 * nz) + nx * dx) * (-1.0f / nz);

  return true;
}

bool CoarseHeightQuery::QueryLayer0(const float* pos,
                                     float* outHeight) const {
  return QueryLayer(pos, outHeight, 0);
}

bool CoarseHeightQuery::QueryLayer1(const float* pos,
                                     float* outHeight) const {
  return QueryLayer(pos, outHeight, 1);
}

bool CoarseHeightQuery::HasTileData(int tileX, int tileY) const {
  if (tileX < 0 || tileX >= kTilesPerSide ||
      tileY < 0 || tileY >= kTilesPerSide)
    return false;
  return tiles_[tileY][tileX].loaded;
}

int CoarseHeightQuery::GetLoadedTileCount() const {
  int count = 0;
  for (const auto& row : tiles_)
    for (const auto& slot : row)
      if (slot.loaded) ++count;
  return count;
}

}

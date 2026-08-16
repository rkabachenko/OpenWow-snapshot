#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <unordered_map>

#include "openwow/world/terrain/terrain_height_provider.h"

namespace openwow::data::map {

struct HeightmapNormal {
  float x{0.0f};
  float y{0.0f};
  float z{0.0f};
};

class HeightmapQuery : public openwow::world::TerrainHeightProvider {
 public:

  static constexpr float kTileSize   = 533.33333f;
  static constexpr float kChunkSize  = 33.33333f;
  static constexpr float kUnitSize   = kChunkSize / 8.0f;
  static constexpr float kMapMid     = 32.0f;
  static constexpr int   kChunksPerSide = 16;
  static constexpr int   kOuterGrid  = 9;
  static constexpr int   kInnerGrid  = 8;
  static constexpr int   kVertsPerChunk = kOuterGrid * kOuterGrid + kInnerGrid * kInnerGrid;

  void SetHeightData(uint32_t tileX, uint32_t tileY,
                     uint32_t chunkX, uint32_t chunkY,
                     const std::array<float, 145>& heights);

  void SetTileHeights(uint32_t tileX, uint32_t tileY,
                      const std::array<std::array<float, 145>, 256>& allHeights);

  void ClearTile(uint32_t tileX, uint32_t tileY);

  void Clear();

  [[nodiscard]] std::optional<float> GetHeight(float worldX, float worldY) const;

  [[nodiscard]] std::optional<HeightmapNormal> GetNormal(float worldX, float worldY) const;

  [[nodiscard]] bool HasData(float worldX, float worldY) const;

  [[nodiscard]] std::optional<float> GetSlope(float worldX, float worldY) const;

  [[nodiscard]] uint32_t GetLoadedTileCount() const;

  [[nodiscard]] std::optional<float> GetHeightAt(float x,
                                                  float y) const override;
  [[nodiscard]] bool IsLoaded(float x, float y) const override;

  static void WorldToTile(float worldX, float worldY,
                          uint32_t& tileX, uint32_t& tileY);

  static void WorldToChunk(float worldX, float worldY,
                           uint32_t& tileX, uint32_t& tileY,
                           uint32_t& chunkX, uint32_t& chunkY);

 private:

  struct ChunkData {
    std::array<float, 145> heights{};
    bool loaded{false};
  };

  struct TileData {
    std::array<ChunkData, 256> chunks{};
  };

  struct TileKey {
    uint32_t x, y;
    bool operator==(const TileKey& o) const { return x == o.x && y == o.y; }
  };
  struct TileKeyHash {
    std::size_t operator()(const TileKey& k) const {
      return std::hash<uint64_t>{}(
          (static_cast<uint64_t>(k.x) << 32) | k.y);
    }
  };

  std::unordered_map<TileKey, TileData, TileKeyHash> tiles_;

  mutable TileKey hot_tile_key_{0, 0};
  mutable const TileData* hot_tile_ptr_{nullptr};

  const ChunkData* ResolveChunk(float worldX, float worldY,
                                float& localX, float& localY) const;

  static float InterpolateInChunk(const ChunkData& chunk,
                                  float localX, float localY);
};

}

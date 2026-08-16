#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

#include "openwow/data/terrain/adt_file.h"
#include "openwow/world/terrain/terrain_height_provider.h"
#include "openwow/world/coordinates/world_coordinates.h"

namespace openwow::world {

struct RayHit {
  float x, y, z;
  float distance;
  float normal[3];
};

struct CollisionFacetView {
  std::array<std::array<float, 3>, 3> vertices{};
  std::array<float, 3> normal{0.0f, 0.0f, 1.0f};
  std::uint64_t owner_id{0};
  std::uint64_t facet_id{0};

  std::uint64_t owner_guid{0};

  std::uint8_t source_flags{0};
};

using CollisionFacetVisitor =
    std::function<void(const CollisionFacetView&)>;

[[nodiscard]] float InterpolateTerrainHeightDelta(
    const std::array<float, data::terrain::kVerticesPerChunk>& heights,
    int row, int col, float fx, float fy);

class TerrainCollision : public TerrainHeightProvider {
 public:
  struct ChunkView {
    float base_x = 0.0f;
    float base_y = 0.0f;
    float base_z = 0.0f;
    const float* heights = nullptr;
    std::uint32_t holes = 0u;
  };

  using ChunkVisitor = std::function<void(const ChunkView&)>;

  void SetTileData(int32_t tile_x, int32_t tile_y,
                   const data::terrain::AdtFile& adt);

  void RemoveTile(int32_t tile_x, int32_t tile_y);

  void Clear();

  [[nodiscard]] std::optional<float> GetHeight(float x, float y) const;

  [[nodiscard]] std::optional<float> GetHeightAt(float x,
                                                  float y) const override;
  [[nodiscard]] bool IsLoaded(float x, float y) const override;

  [[nodiscard]] bool AreTilesLoaded(float min_x, float max_x,
                                    float min_y, float max_y) const;

  [[nodiscard]] std::optional<std::array<float, 3>> GetNormal(
      float x, float y) const;

  [[nodiscard]] bool IsHole(float x, float y) const;

  void VisitChunksOverlappingBounds(float min_x, float max_x,
                                    float min_y, float max_y,
                                    const ChunkVisitor& visitor) const;

  void VisitFacets(float min_x, float max_x, float min_y, float max_y,
                   float min_z, float max_z,
                   const CollisionFacetVisitor& visitor) const;
  [[nodiscard]] std::uint64_t FacetRevision() const { return revision_; }

  [[nodiscard]] std::optional<RayHit> RaycastTerrain(
      float ox, float oy, float oz,
      float dx, float dy, float dz,
      float max_dist = 1000.0f) const;

 private:

  struct ChunkData {
    float base_x{};
    float base_y{};
    float base_z{};
    std::array<float, data::terrain::kVerticesPerChunk> heights{};
    uint32_t holes{};
  };

  struct TileData {
    int32_t tile_x{};
    int32_t tile_y{};
    std::array<ChunkData, data::terrain::kTotalChunks> chunks{};
  };

  struct TileKey {
    int32_t x, y;
    bool operator==(const TileKey& o) const { return x == o.x && y == o.y; }
  };
  struct TileKeyHash {
    std::size_t operator()(const TileKey& k) const {
      auto h = (static_cast<int64_t>(k.x) << 32) |
               static_cast<uint32_t>(k.y);
      return std::hash<int64_t>{}(h);
    }
  };

  std::unordered_map<TileKey, TileData, TileKeyHash> tiles_;
  std::uint64_t revision_{0};

  const ChunkData* ResolveChunk(float x, float y, int& cell_row,
                                int& cell_col, float& fx, float& fy) const;

  static std::array<float, 3> ComputeTriangleNormal(const ChunkData& chunk,
                                                     int cell_row,
                                                     int cell_col, float fx,
                                                     float fy);

  static constexpr float kTileSize  = 533.33333f;
  static constexpr float kChunkSize = data::terrain::kChunkSize;
  static constexpr float kUnitSize  = data::terrain::kUnitSize;
  static constexpr float kMapMidPoint = 32.0f;
};

class CollisionManager {
 public:
  TerrainCollision& terrain() { return terrain_; }
  const TerrainCollision& terrain() const { return terrain_; }

  using WmoFacetGather = std::function<void(const std::array<float, 6>&,
                                            const CollisionFacetVisitor&)>;
  void SetWmoFacetProvider(WmoFacetGather gather,
                           std::function<std::uint64_t()> revision) {
    wmo_facet_gather_ = std::move(gather);
    wmo_facet_revision_ = std::move(revision);
  }

  [[nodiscard]] std::optional<float> GetGroundHeight(
      float x, float y,
      float z = 10000.0f) const;

  [[nodiscard]] std::optional<RayHit> Raycast(
      float ox, float oy, float oz,
      float dx, float dy, float dz,
      float max_dist = 1000.0f) const;

  [[nodiscard]] bool RaycastWorld(
      float ox, float oy, float oz,
      float dx, float dy, float dz,
      float max_dist,
      float& hit_x, float& hit_y, float& hit_z) const;

  void VisitFacets(float min_x, float max_x, float min_y, float max_y,
                   float min_z, float max_z,
                   const CollisionFacetVisitor& visitor) const;
  [[nodiscard]] std::uint64_t FacetRevision() const;

 private:
  [[nodiscard]] std::optional<RayHit> RaycastWmoFacets(
      float ox, float oy, float oz,
      float dx, float dy, float dz,
      float max_dist) const;

  TerrainCollision terrain_;
  WmoFacetGather wmo_facet_gather_;
  std::function<std::uint64_t()> wmo_facet_revision_;
};

}

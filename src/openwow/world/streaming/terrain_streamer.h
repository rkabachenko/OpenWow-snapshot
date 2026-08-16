#pragma once

#include "openwow/world/coordinates/world_coordinates.h"

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace openwow::world {

struct StreamedTileEntry {
  TileCoord coord{};
  float distance_sq = 0.0f;
  int lod_level = 0;
};

class TerrainStreamer {
 public:
  TerrainStreamer() = default;

  void SetLoadRadius(int tiles);

  void SetUnloadRadius(int tiles);

  [[nodiscard]] int GetLoadRadius() const { return load_radius_; }
  [[nodiscard]] int GetUnloadRadius() const { return unload_radius_; }

  bool Update(float cam_x, float cam_y, uint32_t map_id);

  void OnMapChange(uint32_t new_map_id, float spawn_x, float spawn_y);

  [[nodiscard]] std::vector<TileCoord> GetLoadedTiles() const;

  [[nodiscard]] bool IsTileLoaded(int tile_x, int tile_y) const;

  [[nodiscard]] const StreamedTileEntry* GetTile(int tile_x, int tile_y) const;

  [[nodiscard]] std::size_t GetLoadedTileCount() const {
    return loaded_tiles_.size();
  }

  [[nodiscard]] TileCoord GetCenterTile() const { return center_tile_; }

  [[nodiscard]] uint32_t GetCurrentMapId() const { return current_map_; }

  [[nodiscard]] const std::vector<TileCoord>& GetPendingLoads() const {
    return pending_loads_;
  }

  [[nodiscard]] const std::vector<TileCoord>& GetPendingUnloads() const {
    return pending_unloads_;
  }

  static TileCoord WorldToTile(float x, float y);

  static std::pair<float, float> TileCenterToWorld(int tile_x, int tile_y);

  static int CalculateTileLOD(float tile_distance_sq);

  void Reset();

  using TileExistsFn = std::function<bool(int tile_x, int tile_y)>;
  void SetTileExistsCallback(TileExistsFn fn) { tile_exists_ = std::move(fn); }

 private:
  int load_radius_ = 5;
  int unload_radius_ = 7;
  uint32_t current_map_ = 0;
  TileCoord center_tile_{0, 0};
  bool has_center_ = false;

  std::unordered_map<uint64_t, StreamedTileEntry> loaded_tiles_;

  std::vector<TileCoord> pending_loads_;
  std::vector<TileCoord> pending_unloads_;

  TileExistsFn tile_exists_;

  static uint64_t PackKey(int x, int y) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) |
           static_cast<uint32_t>(y);
  }

  std::unordered_set<uint64_t> ComputeRequiredTiles() const;

  void UpdateLodLevels();

  static float TileDistanceSq(TileCoord a, TileCoord b);

  static constexpr float kLod0MaxDistSq = 4.0f;

  static constexpr float kLod1MaxDistSq = 16.0f;

  static constexpr float kLod2MaxDistSq = 36.0f;
};

}

#pragma once

#include <cmath>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace openwow::game {

class MapStreamer {
 public:
  static MapStreamer& Get();

  void LoadMap(uint32_t mapId);

  void UnloadMap();

  void Update(float playerX, float playerY);

  [[nodiscard]] uint32_t GetCurrentMapId() const;
  [[nodiscard]] bool IsMapLoaded() const;

  struct TileState {
    uint32_t x = 0;
    uint32_t y = 0;
    enum Status { Unloaded, Loading, Loaded, Unloading };
    Status status = Unloaded;
  };

  [[nodiscard]] TileState GetTileState(uint32_t x, uint32_t y) const;
  [[nodiscard]] bool IsTileLoaded(uint32_t x, uint32_t y) const;

  static void WorldToTile(float worldX, float worldY,
                          uint32_t& tileX, uint32_t& tileY);

  static void TileToWorld(uint32_t tileX, uint32_t tileY,
                          float& worldX, float& worldY);

  void SetViewRadius(uint32_t radius);
  [[nodiscard]] uint32_t GetViewRadius() const;

  [[nodiscard]] uint32_t GetLoadedTileCount() const;
  [[nodiscard]] uint32_t GetPendingTileCount() const;

  void PreloadTile(uint32_t tileX, uint32_t tileY);

  void Reset();

 private:
  MapStreamer() = default;

  void UpdateLoadedTiles(uint32_t centerX, uint32_t centerY);

  uint32_t map_id_ = 0;
  bool map_loaded_ = false;
  uint32_t view_radius_ = 2;

  struct TileEntry {
    TileState::Status status = TileState::Unloaded;
  };
  std::unordered_map<uint64_t, TileEntry> tiles_;

  bool has_wdt_ = false;
  bool wdt_tiles_[64][64]{};

  uint32_t last_center_x_ = UINT32_MAX;
  uint32_t last_center_y_ = UINT32_MAX;

  static uint64_t TileKey(uint32_t x, uint32_t y) {
    return (uint64_t(x) << 32) | y;
  }

  static constexpr float kTileSize = 533.33333f;
  static constexpr float kMapMidPoint = 32.0f;
  static constexpr uint32_t kMaxTile = 63;

  mutable std::mutex mutex_;
};

}

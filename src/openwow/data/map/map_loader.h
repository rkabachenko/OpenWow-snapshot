#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace openwow::data {

struct TileCoord {
  int32_t x = 0;
  int32_t y = 0;

  constexpr bool operator==(const TileCoord& o) const { return x == o.x && y == o.y; }
  constexpr bool operator!=(const TileCoord& o) const { return !(*this == o); }
};

enum class TileState : uint8_t {
  Unloaded,
  Loading,
  Loaded,
  Failed,
};

enum class LoadPriority : uint8_t {
  Immediate,
  High,
  Normal,
  Low,
};

class MapLoaderManager {
 public:
  MapLoaderManager() = default;

  void SetCurrentMap(uint32_t mapId, const std::string& mapName);
  [[nodiscard]] uint32_t GetCurrentMapId() const;
  [[nodiscard]] std::string GetCurrentMapName() const;

  void RequestTile(TileCoord coord, LoadPriority priority = LoadPriority::Normal);
  void UnloadTile(TileCoord coord);
  [[nodiscard]] TileState GetTileState(TileCoord coord) const;
  [[nodiscard]] std::vector<TileCoord> GetLoadedTiles() const;
  [[nodiscard]] uint32_t GetLoadedTileCount() const;
  [[nodiscard]] uint32_t GetPendingLoadCount() const;

  void SetMaxConcurrentLoads(uint32_t max);
  [[nodiscard]] uint32_t GetMaxConcurrentLoads() const;
  void SetMaxLoadedTiles(uint32_t max);
  [[nodiscard]] uint32_t GetMaxLoadedTiles() const;

  void UpdatePosition(float worldX, float worldY);

  static TileCoord WorldToTile(float worldX, float worldY);
  static std::pair<float, float> TileToWorld(TileCoord coord);

  [[nodiscard]] uint32_t GetLoadRadius() const;
  void SetLoadRadius(uint32_t radius);

  [[nodiscard]] bool IsMapLoaded() const;
  [[nodiscard]] float GetLoadProgress() const;

  void Update(float dt);
  void Reset();

  using LoadFileCallback = std::function<std::vector<uint8_t>(const std::string &path)>;

  void SetFileLoader(LoadFileCallback callback);

 private:

  static constexpr float kTileSize = 533.33333f;
  static constexpr float kMapMidPoint = 32.0f;
  static constexpr uint32_t kGridSize = 64;

  static bool IsValidCoord(TileCoord c) {
    return c.x >= 0 && c.x < static_cast<int32_t>(kGridSize) &&
           c.y >= 0 && c.y < static_cast<int32_t>(kGridSize);
  }

  struct TileEntry {
    TileState state = TileState::Unloaded;
    LoadPriority priority = LoadPriority::Normal;
  };

  static uint64_t Key(TileCoord c) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(c.x)) << 32) |
           static_cast<uint64_t>(static_cast<uint32_t>(c.y));
  }

  mutable std::mutex mutex_;
  uint32_t map_id_ = 0;
  LoadFileCallback load_file_;
  std::string map_name_;

  std::unordered_map<uint64_t, TileEntry> tiles_;

  uint32_t max_concurrent_loads_ = 4;
  uint32_t max_loaded_tiles_ = 49;
  uint32_t load_radius_ = 3;

  TileCoord last_center_{-1, -1};
};

}

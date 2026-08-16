#pragma once

#include "openwow/data/map/map_loader.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <vector>

namespace openwow::data {

struct StreamRequest {
  TileCoord tileCoord;
  LoadPriority priority = LoadPriority::Normal;
  float requestTime = 0.0f;
  uint32_t mapId = 0;
};

struct StreamResult {
  TileCoord tileCoord;
  bool success = false;
  float loadTimeMs = 0.0f;
  uint64_t dataSize = 0;
};

class TileStreamingSystem {
 public:
  TileStreamingSystem() = default;

  void EnqueueTile(TileCoord coord, LoadPriority priority);
  void CancelTile(TileCoord coord);
  [[nodiscard]] uint32_t GetQueueSize() const;
  [[nodiscard]] std::vector<TileCoord> GetQueuedTiles() const;
  [[nodiscard]] bool IsQueued(TileCoord coord) const;
  void ReprioritizeTile(TileCoord coord, LoadPriority newPriority);
  void ClearQueue();

  std::vector<StreamResult> ProcessQueue(uint32_t maxToProcess = 1);

  void SetBandwidthLimit(uint32_t bytesPerSec);
  [[nodiscard]] uint32_t GetBandwidthLimit() const;
  [[nodiscard]] uint64_t GetBytesLoaded() const;
  [[nodiscard]] float GetAverageLoadTime() const;
  void SetLoadSimulationTime(float ms);

  [[nodiscard]] uint32_t GetPeakQueueSize() const;

  void Reset();

 private:
  std::deque<StreamRequest> queue_;
  uint32_t bandwidth_limit_ = 0;
  uint64_t bytes_loaded_ = 0;
  float total_load_time_ms_ = 0.0f;
  uint32_t loads_completed_ = 0;
  float sim_load_time_ms_ = 5.0f;
  uint32_t peak_queue_size_ = 0;

  static constexpr uint64_t kDefaultTileDataSize = 262144;

  void UpdatePeakQueueSize();
  static int PriorityOrder(LoadPriority p);
};

}

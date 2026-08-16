
#include "openwow/data/map/tile_streaming.h"

#include <algorithm>

namespace openwow::data {

int TileStreamingSystem::PriorityOrder(LoadPriority p) {
  switch (p) {
    case LoadPriority::Immediate: return 0;
    case LoadPriority::High:      return 1;
    case LoadPriority::Normal:    return 2;
    case LoadPriority::Low:       return 3;
  }
  return 4;
}

void TileStreamingSystem::EnqueueTile(TileCoord coord, LoadPriority priority) {

  for (auto& req : queue_) {
    if (req.tileCoord == coord) return;
  }
  StreamRequest req;
  req.tileCoord = coord;
  req.priority = priority;
  req.requestTime = 0.0f;
  queue_.push_back(req);

  std::stable_sort(queue_.begin(), queue_.end(),
                   [](const StreamRequest& a, const StreamRequest& b) {
                     return PriorityOrder(a.priority) < PriorityOrder(b.priority);
                   });

  UpdatePeakQueueSize();
}

void TileStreamingSystem::CancelTile(TileCoord coord) {
  queue_.erase(
      std::remove_if(queue_.begin(), queue_.end(),
                     [&](const StreamRequest& r) { return r.tileCoord == coord; }),
      queue_.end());
}

uint32_t TileStreamingSystem::GetQueueSize() const {
  return static_cast<uint32_t>(queue_.size());
}

std::vector<TileCoord> TileStreamingSystem::GetQueuedTiles() const {
  std::vector<TileCoord> result;
  result.reserve(queue_.size());
  for (auto& req : queue_) {
    result.push_back(req.tileCoord);
  }
  return result;
}

bool TileStreamingSystem::IsQueued(TileCoord coord) const {
  for (auto& req : queue_) {
    if (req.tileCoord == coord) return true;
  }
  return false;
}

void TileStreamingSystem::ReprioritizeTile(TileCoord coord, LoadPriority newPriority) {
  for (auto& req : queue_) {
    if (req.tileCoord == coord) {
      req.priority = newPriority;
      break;
    }
  }

  std::stable_sort(queue_.begin(), queue_.end(),
                   [](const StreamRequest& a, const StreamRequest& b) {
                     return PriorityOrder(a.priority) < PriorityOrder(b.priority);
                   });
}

void TileStreamingSystem::ClearQueue() {
  queue_.clear();
}

std::vector<StreamResult> TileStreamingSystem::ProcessQueue(uint32_t maxToProcess) {
  std::vector<StreamResult> results;
  uint32_t processed = 0;
  while (!queue_.empty() && processed < maxToProcess) {
    StreamRequest req = queue_.front();
    queue_.pop_front();

    StreamResult result;
    result.tileCoord = req.tileCoord;
    result.success = true;
    result.loadTimeMs = sim_load_time_ms_;
    result.dataSize = kDefaultTileDataSize;

    bytes_loaded_ += result.dataSize;
    total_load_time_ms_ += result.loadTimeMs;
    ++loads_completed_;
    ++processed;

    results.push_back(result);
  }
  return results;
}

void TileStreamingSystem::SetBandwidthLimit(uint32_t bytesPerSec) {
  bandwidth_limit_ = bytesPerSec;
}

uint32_t TileStreamingSystem::GetBandwidthLimit() const {
  return bandwidth_limit_;
}

uint64_t TileStreamingSystem::GetBytesLoaded() const {
  return bytes_loaded_;
}

float TileStreamingSystem::GetAverageLoadTime() const {
  if (loads_completed_ == 0) return 0.0f;
  return total_load_time_ms_ / static_cast<float>(loads_completed_);
}

void TileStreamingSystem::SetLoadSimulationTime(float ms) {
  sim_load_time_ms_ = ms;
}

uint32_t TileStreamingSystem::GetPeakQueueSize() const {
  return peak_queue_size_;
}

void TileStreamingSystem::UpdatePeakQueueSize() {
  auto current = static_cast<uint32_t>(queue_.size());
  if (current > peak_queue_size_) {
    peak_queue_size_ = current;
  }
}

void TileStreamingSystem::Reset() {
  queue_.clear();
  bandwidth_limit_ = 0;
  bytes_loaded_ = 0;
  total_load_time_ms_ = 0.0f;
  loads_completed_ = 0;
  sim_load_time_ms_ = 5.0f;
  peak_queue_size_ = 0;
}

}


#include "openwow/data/map/map_loader.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace openwow::data {

void MapLoaderManager::SetCurrentMap(uint32_t mapId, const std::string& mapName) {
  std::lock_guard lock(mutex_);
  map_id_ = mapId;
  map_name_ = mapName;
}

uint32_t MapLoaderManager::GetCurrentMapId() const {
  std::lock_guard lock(mutex_);
  return map_id_;
}

std::string MapLoaderManager::GetCurrentMapName() const {
  std::lock_guard lock(mutex_);
  return map_name_;
}

void MapLoaderManager::RequestTile(TileCoord coord, LoadPriority priority) {
  if (!IsValidCoord(coord)) return;
  std::lock_guard lock(mutex_);
  auto key = Key(coord);
  auto it = tiles_.find(key);
  if (it == tiles_.end()) {
    tiles_[key] = TileEntry{TileState::Loading, priority};
  } else if (it->second.state == TileState::Unloaded ||
             it->second.state == TileState::Failed) {
    it->second.state = TileState::Loading;
    it->second.priority = priority;
  }
}

void MapLoaderManager::UnloadTile(TileCoord coord) {
  std::lock_guard lock(mutex_);
  tiles_.erase(Key(coord));
}

TileState MapLoaderManager::GetTileState(TileCoord coord) const {
  std::lock_guard lock(mutex_);
  auto it = tiles_.find(Key(coord));
  if (it == tiles_.end()) return TileState::Unloaded;
  return it->second.state;
}

std::vector<TileCoord> MapLoaderManager::GetLoadedTiles() const {
  std::lock_guard lock(mutex_);
  std::vector<TileCoord> result;
  for (auto& [k, entry] : tiles_) {
    if (entry.state == TileState::Loaded) {
      int32_t x = static_cast<int32_t>(k >> 32);
      int32_t y = static_cast<int32_t>(k & 0xFFFFFFFF);
      result.push_back({x, y});
    }
  }
  return result;
}

uint32_t MapLoaderManager::GetLoadedTileCount() const {
  std::lock_guard lock(mutex_);
  uint32_t count = 0;
  for (auto& [k, entry] : tiles_) {
    if (entry.state == TileState::Loaded) ++count;
  }
  return count;
}

uint32_t MapLoaderManager::GetPendingLoadCount() const {
  std::lock_guard lock(mutex_);
  uint32_t count = 0;
  for (auto& [k, entry] : tiles_) {
    if (entry.state == TileState::Loading) ++count;
  }
  return count;
}

void MapLoaderManager::SetMaxConcurrentLoads(uint32_t max) {
  std::lock_guard lock(mutex_);
  max_concurrent_loads_ = max;
}

uint32_t MapLoaderManager::GetMaxConcurrentLoads() const {
  std::lock_guard lock(mutex_);
  return max_concurrent_loads_;
}

void MapLoaderManager::SetMaxLoadedTiles(uint32_t max) {
  std::lock_guard lock(mutex_);
  max_loaded_tiles_ = max;
}

uint32_t MapLoaderManager::GetMaxLoadedTiles() const {
  std::lock_guard lock(mutex_);
  return max_loaded_tiles_;
}

void MapLoaderManager::UpdatePosition(float worldX, float worldY) {
  TileCoord center = WorldToTile(worldX, worldY);
  {
    std::lock_guard lock(mutex_);
    if (center == last_center_) return;
    last_center_ = center;
  }

  int32_t r = static_cast<int32_t>(load_radius_);

  std::vector<TileCoord> needed;
  for (int32_t dx = -r; dx <= r; ++dx) {
    for (int32_t dy = -r; dy <= r; ++dy) {
      TileCoord tc{center.x + dx, center.y + dy};
      if (IsValidCoord(tc)) needed.push_back(tc);
    }
  }

  std::lock_guard lock(mutex_);

  std::vector<uint64_t> to_remove;
  for (auto& [k, entry] : tiles_) {
    int32_t tx = static_cast<int32_t>(k >> 32);
    int32_t ty = static_cast<int32_t>(k & 0xFFFFFFFF);
    bool in_range = (std::abs(tx - center.x) <= r && std::abs(ty - center.y) <= r);
    if (!in_range) to_remove.push_back(k);
  }
  for (auto k : to_remove) tiles_.erase(k);

  for (auto& tc : needed) {
    auto key = Key(tc);
    if (tiles_.find(key) == tiles_.end()) {
      tiles_[key] = TileEntry{TileState::Loading, LoadPriority::Normal};
    }
  }
}

TileCoord MapLoaderManager::WorldToTile(float worldX, float worldY) {
  int32_t tx = static_cast<int32_t>(std::floor(kMapMidPoint - worldX / kTileSize));
  int32_t ty = static_cast<int32_t>(std::floor(kMapMidPoint - worldY / kTileSize));

  tx = std::clamp(tx, int32_t{0}, int32_t{63});
  ty = std::clamp(ty, int32_t{0}, int32_t{63});
  return {tx, ty};
}

std::pair<float, float> MapLoaderManager::TileToWorld(TileCoord coord) {
  float worldX = (kMapMidPoint - static_cast<float>(coord.x) - 0.5f) * kTileSize;
  float worldY = (kMapMidPoint - static_cast<float>(coord.y) - 0.5f) * kTileSize;
  return {worldX, worldY};
}

uint32_t MapLoaderManager::GetLoadRadius() const {
  std::lock_guard lock(mutex_);
  return load_radius_;
}

void MapLoaderManager::SetLoadRadius(uint32_t radius) {
  std::lock_guard lock(mutex_);
  load_radius_ = radius;
}

bool MapLoaderManager::IsMapLoaded() const {
  std::lock_guard lock(mutex_);

  if (last_center_.x < 0 || last_center_.y < 0) return false;
  auto it = tiles_.find(Key(last_center_));
  return it != tiles_.end() && it->second.state == TileState::Loaded;
}

float MapLoaderManager::GetLoadProgress() const {
  std::lock_guard lock(mutex_);
  if (tiles_.empty()) return 0.0f;
  uint32_t loaded = 0;
  uint32_t total = 0;
  for (auto& [k, entry] : tiles_) {
    ++total;
    if (entry.state == TileState::Loaded) ++loaded;
  }
  return total > 0 ? static_cast<float>(loaded) / static_cast<float>(total) : 0.0f;
}

void MapLoaderManager::SetFileLoader(LoadFileCallback callback) {
  std::lock_guard lock(mutex_);
  load_file_ = std::move(callback);
}

void MapLoaderManager::Update(float ) {
  std::lock_guard lock(mutex_);

  if (!load_file_) {

    uint32_t processed = 0;
    for (auto& [k, entry] : tiles_) {
      if (entry.state == TileState::Loading && processed < max_concurrent_loads_) {
        entry.state = TileState::Loaded;
        ++processed;
      }
    }
    return;
  }

  uint32_t loaded_count = 0;
  for (auto& [k, entry] : tiles_) {
    if (entry.state != TileState::Loading) continue;
    if (loaded_count >= max_concurrent_loads_) break;

    int32_t tx = static_cast<int32_t>(k >> 32);
    int32_t ty = static_cast<int32_t>(k & 0xFFFFFFFF);

    std::string path = "World/Maps/" + map_name_ + "/" + map_name_ + "_" +
                       std::to_string(tx) + "_" + std::to_string(ty) + ".adt";

    auto data = load_file_(path);
    if (data.empty()) {
      entry.state = TileState::Failed;
    } else {
      entry.state = TileState::Loaded;
      ++loaded_count;
    }
  }
}

void MapLoaderManager::Reset() {
  std::lock_guard lock(mutex_);
  tiles_.clear();
  map_id_ = 0;
  map_name_.clear();
  max_concurrent_loads_ = 4;
  max_loaded_tiles_ = 49;
  load_radius_ = 3;
  last_center_ = {-1, -1};
}

}

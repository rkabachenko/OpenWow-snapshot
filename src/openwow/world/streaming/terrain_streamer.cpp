
#include "openwow/world/streaming/terrain_streamer.h"

#include <algorithm>
#include <cmath>

namespace openwow::world {

namespace {

void SortPendingLoadsByVisibleAreaDistance(std::vector<TileCoord>& pending_loads,
                                           const float focus_x,
                                           const float focus_y) {
  std::stable_sort(
      pending_loads.begin(), pending_loads.end(),
      [focus_x, focus_y](const TileCoord& lhs, const TileCoord& rhs) {
        const float lhs_distance_sq =
            TileBoundsDistanceSq2D(focus_x, focus_y, lhs);
        const float rhs_distance_sq =
            TileBoundsDistanceSq2D(focus_x, focus_y, rhs);
        return rhs_distance_sq > lhs_distance_sq;
      });
}

}

void TerrainStreamer::SetLoadRadius(int tiles) {
  load_radius_ = std::max(1, tiles);

  if (unload_radius_ < load_radius_) {
    unload_radius_ = load_radius_ + 2;
  }
}

void TerrainStreamer::SetUnloadRadius(int tiles) {
  unload_radius_ = std::max(load_radius_, tiles);
}

bool TerrainStreamer::Update(float cam_x, float cam_y, uint32_t map_id) {
  pending_loads_.clear();
  pending_unloads_.clear();

  if (map_id != current_map_) {
    OnMapChange(map_id, cam_x, cam_y);
    return true;
  }

  TileCoord new_center = WorldToTile(cam_x, cam_y);

  new_center.x = std::max(0, std::min(63, new_center.x));
  new_center.y = std::max(0, std::min(63, new_center.y));

  if (has_center_ && new_center == center_tile_ && !loaded_tiles_.empty()) {
    return false;
  }

  center_tile_ = new_center;
  has_center_ = true;

  auto required = ComputeRequiredTiles();

  for (auto it = loaded_tiles_.begin(); it != loaded_tiles_.end();) {
    const auto& entry = it->second;
    float dist_sq = TileDistanceSq(center_tile_, entry.coord);
    float unload_sq = static_cast<float>(unload_radius_ * unload_radius_);

    if (dist_sq > unload_sq) {
      pending_unloads_.push_back(entry.coord);
      it = loaded_tiles_.erase(it);
    } else {
      ++it;
    }
  }

  for (uint64_t key : required) {
    if (loaded_tiles_.find(key) == loaded_tiles_.end()) {
      int tx = static_cast<int>(static_cast<uint32_t>(key >> 32));
      int ty = static_cast<int>(static_cast<uint32_t>(key & 0xFFFFFFFF));

      if (tile_exists_ && !tile_exists_(tx, ty)) {
        continue;
      }

      StreamedTileEntry entry;
      entry.coord = {tx, ty};
      entry.distance_sq = TileDistanceSq(center_tile_, entry.coord);
      entry.lod_level = CalculateTileLOD(entry.distance_sq);

      loaded_tiles_[key] = entry;
      pending_loads_.push_back({tx, ty});
    }
  }

  UpdateLodLevels();

  SortPendingLoadsByVisibleAreaDistance(pending_loads_, cam_x, cam_y);

  return !pending_loads_.empty() || !pending_unloads_.empty();
}

void TerrainStreamer::OnMapChange(uint32_t new_map_id,
                                   float spawn_x, float spawn_y) {
  pending_loads_.clear();
  pending_unloads_.clear();

  for (const auto& [key, entry] : loaded_tiles_) {
    pending_unloads_.push_back(entry.coord);
  }
  loaded_tiles_.clear();

  current_map_ = new_map_id;
  has_center_ = false;

  TileCoord new_center = WorldToTile(spawn_x, spawn_y);
  new_center.x = std::max(0, std::min(63, new_center.x));
  new_center.y = std::max(0, std::min(63, new_center.y));
  center_tile_ = new_center;
  has_center_ = true;

  auto required = ComputeRequiredTiles();
  for (uint64_t key : required) {
    int tx = static_cast<int>(static_cast<uint32_t>(key >> 32));
    int ty = static_cast<int>(static_cast<uint32_t>(key & 0xFFFFFFFF));

    if (tile_exists_ && !tile_exists_(tx, ty)) {
      continue;
    }

    StreamedTileEntry entry;
    entry.coord = {tx, ty};
    entry.distance_sq = TileDistanceSq(center_tile_, entry.coord);
    entry.lod_level = CalculateTileLOD(entry.distance_sq);

    loaded_tiles_[key] = entry;
    pending_loads_.push_back({tx, ty});
  }

  SortPendingLoadsByVisibleAreaDistance(pending_loads_, spawn_x, spawn_y);
}

std::vector<TileCoord> TerrainStreamer::GetLoadedTiles() const {
  std::vector<TileCoord> result;
  result.reserve(loaded_tiles_.size());
  for (const auto& [key, entry] : loaded_tiles_) {
    result.push_back(entry.coord);
  }
  return result;
}

bool TerrainStreamer::IsTileLoaded(int tile_x, int tile_y) const {
  return loaded_tiles_.find(PackKey(tile_x, tile_y)) != loaded_tiles_.end();
}

const StreamedTileEntry* TerrainStreamer::GetTile(int tile_x,
                                                   int tile_y) const {
  auto it = loaded_tiles_.find(PackKey(tile_x, tile_y));
  if (it == loaded_tiles_.end()) return nullptr;
  return &it->second;
}

TileCoord TerrainStreamer::WorldToTile(float x, float y) {
  return openwow::world::WorldToTile(x, y);
}

std::pair<float, float> TerrainStreamer::TileCenterToWorld(int tile_x,
                                                            int tile_y) {
  return openwow::world::TileCenterToWorld({tile_x, tile_y});
}

int TerrainStreamer::CalculateTileLOD(float tile_distance_sq) {
  if (tile_distance_sq < kLod0MaxDistSq) return 0;
  if (tile_distance_sq < kLod1MaxDistSq) return 1;
  if (tile_distance_sq < kLod2MaxDistSq) return 2;
  return 3;
}

void TerrainStreamer::Reset() {
  loaded_tiles_.clear();
  pending_loads_.clear();
  pending_unloads_.clear();
  center_tile_ = {0, 0};
  has_center_ = false;
  current_map_ = 0;
}

std::unordered_set<uint64_t> TerrainStreamer::ComputeRequiredTiles() const {
  std::unordered_set<uint64_t> result;
  for (int dx = -load_radius_; dx <= load_radius_; ++dx) {
    for (int dy = -load_radius_; dy <= load_radius_; ++dy) {
      int tx = center_tile_.x + dx;
      int ty = center_tile_.y + dy;
      if (tx >= 0 && tx <= 63 && ty >= 0 && ty <= 63) {
        result.insert(PackKey(tx, ty));
      }
    }
  }
  return result;
}

void TerrainStreamer::UpdateLodLevels() {
  for (auto& [key, entry] : loaded_tiles_) {
    entry.distance_sq = TileDistanceSq(center_tile_, entry.coord);
    entry.lod_level = CalculateTileLOD(entry.distance_sq);
  }
}

float TerrainStreamer::TileDistanceSq(TileCoord a, TileCoord b) {
  float dx = static_cast<float>(a.x - b.x);
  float dy = static_cast<float>(a.y - b.y);
  return dx * dx + dy * dy;
}

}


#include "openwow/game/map_streamer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace openwow::game {

MapStreamer& MapStreamer::Get() {
  static MapStreamer instance;
  return instance;
}

void MapStreamer::LoadMap(uint32_t mapId) {
  std::lock_guard lock(mutex_);

  tiles_.clear();
  std::memset(wdt_tiles_, 0, sizeof(wdt_tiles_));
  has_wdt_ = false;

  map_id_ = mapId;
  map_loaded_ = true;
  last_center_x_ = UINT32_MAX;
  last_center_y_ = UINT32_MAX;
}

void MapStreamer::UnloadMap() {
  std::lock_guard lock(mutex_);
  tiles_.clear();
  std::memset(wdt_tiles_, 0, sizeof(wdt_tiles_));
  has_wdt_ = false;
  map_loaded_ = false;
  map_id_ = 0;
  last_center_x_ = UINT32_MAX;
  last_center_y_ = UINT32_MAX;
}

void MapStreamer::Update(float playerX, float playerY) {
  uint32_t tileX = 0;
  uint32_t tileY = 0;
  WorldToTile(playerX, playerY, tileX, tileY);

  std::lock_guard lock(mutex_);
  if (!map_loaded_) return;

  if (tileX == last_center_x_ && tileY == last_center_y_) return;

  last_center_x_ = tileX;
  last_center_y_ = tileY;

  UpdateLoadedTiles(tileX, tileY);
}

uint32_t MapStreamer::GetCurrentMapId() const {
  std::lock_guard lock(mutex_);
  return map_id_;
}

bool MapStreamer::IsMapLoaded() const {
  std::lock_guard lock(mutex_);
  return map_loaded_;
}

MapStreamer::TileState MapStreamer::GetTileState(uint32_t x, uint32_t y) const {
  std::lock_guard lock(mutex_);
  TileState state;
  state.x = x;
  state.y = y;
  auto it = tiles_.find(TileKey(x, y));
  if (it != tiles_.end()) {
    state.status = it->second.status;
  }
  return state;
}

bool MapStreamer::IsTileLoaded(uint32_t x, uint32_t y) const {
  std::lock_guard lock(mutex_);
  auto it = tiles_.find(TileKey(x, y));
  return it != tiles_.end() && it->second.status == TileState::Loaded;
}

void MapStreamer::WorldToTile(float worldX, float worldY,
                              uint32_t& tileX, uint32_t& tileY) {

  float rawX = std::floor(kMapMidPoint - worldX / kTileSize);
  float rawY = std::floor(kMapMidPoint - worldY / kTileSize);

  tileX = static_cast<uint32_t>(
      std::clamp(rawX, 0.0f, static_cast<float>(kMaxTile)));
  tileY = static_cast<uint32_t>(
      std::clamp(rawY, 0.0f, static_cast<float>(kMaxTile)));
}

void MapStreamer::TileToWorld(uint32_t tileX, uint32_t tileY,
                              float& worldX, float& worldY) {

  worldX = (kMapMidPoint - static_cast<float>(tileX) - 0.5f) * kTileSize;
  worldY = (kMapMidPoint - static_cast<float>(tileY) - 0.5f) * kTileSize;
}

void MapStreamer::SetViewRadius(uint32_t radius) {
  std::lock_guard lock(mutex_);
  view_radius_ = radius;
}

uint32_t MapStreamer::GetViewRadius() const {
  std::lock_guard lock(mutex_);
  return view_radius_;
}

uint32_t MapStreamer::GetLoadedTileCount() const {
  std::lock_guard lock(mutex_);
  uint32_t count = 0;
  for (const auto& [key, entry] : tiles_) {
    if (entry.status == TileState::Loaded) ++count;
  }
  return count;
}

uint32_t MapStreamer::GetPendingTileCount() const {
  std::lock_guard lock(mutex_);
  uint32_t count = 0;
  for (const auto& [key, entry] : tiles_) {
    if (entry.status == TileState::Loading) ++count;
  }
  return count;
}

void MapStreamer::PreloadTile(uint32_t tileX, uint32_t tileY) {
  if (tileX > kMaxTile || tileY > kMaxTile) return;

  std::lock_guard lock(mutex_);
  auto key = TileKey(tileX, tileY);
  auto it = tiles_.find(key);
  if (it == tiles_.end()) {
    tiles_[key] = TileEntry{TileState::Loading};
  }
}

void MapStreamer::Reset() {
  std::lock_guard lock(mutex_);
  tiles_.clear();
  std::memset(wdt_tiles_, 0, sizeof(wdt_tiles_));
  has_wdt_ = false;
  map_loaded_ = false;
  map_id_ = 0;
  view_radius_ = 2;
  last_center_x_ = UINT32_MAX;
  last_center_y_ = UINT32_MAX;
}

void MapStreamer::UpdateLoadedTiles(uint32_t centerX, uint32_t centerY) {

  const int32_t cx = static_cast<int32_t>(centerX);
  const int32_t cy = static_cast<int32_t>(centerY);
  const int32_t r  = static_cast<int32_t>(view_radius_);

  for (auto it = tiles_.begin(); it != tiles_.end();) {
    uint32_t tx = static_cast<uint32_t>(it->first >> 32);
    uint32_t ty = static_cast<uint32_t>(it->first & 0xFFFFFFFF);
    int32_t dx = static_cast<int32_t>(tx) - cx;
    int32_t dy = static_cast<int32_t>(ty) - cy;
    if (std::abs(dx) > r || std::abs(dy) > r) {
      it = tiles_.erase(it);
    } else {
      ++it;
    }
  }

  for (int32_t dx = -r; dx <= r; ++dx) {
    for (int32_t dy = -r; dy <= r; ++dy) {
      int32_t tx = cx + dx;
      int32_t ty = cy + dy;
      if (tx < 0 || tx > static_cast<int32_t>(kMaxTile)) continue;
      if (ty < 0 || ty > static_cast<int32_t>(kMaxTile)) continue;

      uint32_t ux = static_cast<uint32_t>(tx);
      uint32_t uy = static_cast<uint32_t>(ty);

      if (has_wdt_ && !wdt_tiles_[uy][ux]) continue;

      auto key = TileKey(ux, uy);
      if (tiles_.find(key) == tiles_.end()) {

        tiles_[key] = TileEntry{TileState::Loaded};
      }
    }
  }
}

}


#include "openwow/game/world_marker.h"

namespace openwow::game {

static constexpr std::uint32_t kMarkerColors[kMaxWorldMarkers] = {
    0xFF0000FF,
    0xFF00FF00,
    0xFF800080,
    0xFFFF0000,
    0xFFFFFF00,
};

static constexpr const char* kMarkerNames[kMaxWorldMarkers] = {
    "Blue", "Green", "Purple", "Red", "Yellow",
};

void WorldMarkerSystem::PlaceMarker(std::uint32_t index, float x, float y,
                                    float z, std::uint32_t mapId) {
  if (index >= kMaxWorldMarkers) return;
  auto& m    = markers_[index];
  m.index    = index;
  m.x        = x;
  m.y        = y;
  m.z        = z;
  m.mapId    = mapId;
  m.isActive = true;
}

void WorldMarkerSystem::ClearMarker(std::uint32_t index) {
  if (index >= kMaxWorldMarkers) return;
  markers_[index] = WorldMarkerEntry{};
  markers_[index].index = index;
}

void WorldMarkerSystem::ClearAllMarkers() {
  for (std::uint32_t i = 0; i < kMaxWorldMarkers; ++i) {
    markers_[i] = WorldMarkerEntry{};
    markers_[i].index = i;
  }
}

std::optional<WorldMarkerEntry> WorldMarkerSystem::GetMarker(
    std::uint32_t index) const {
  if (index >= kMaxWorldMarkers || !markers_[index].isActive)
    return std::nullopt;
  return markers_[index];
}

bool WorldMarkerSystem::IsMarkerActive(std::uint32_t index) const {
  if (index >= kMaxWorldMarkers) return false;
  return markers_[index].isActive;
}

std::vector<WorldMarkerEntry> WorldMarkerSystem::GetAllMarkers() const {
  return {markers_.begin(), markers_.end()};
}

std::uint32_t WorldMarkerSystem::GetActiveMarkerCount() const {
  std::uint32_t n = 0;
  for (const auto& m : markers_) {
    if (m.isActive) ++n;
  }
  return n;
}

std::uint32_t WorldMarkerSystem::GetMarkerColor(std::uint32_t index) {
  if (index >= kMaxWorldMarkers) return 0x00000000;
  return kMarkerColors[index];
}

std::string WorldMarkerSystem::GetMarkerName(std::uint32_t index) {
  if (index >= kMaxWorldMarkers) return "Unknown";
  return kMarkerNames[index];
}

float WorldMarkerSystem::GetDistanceToMarker(std::uint32_t index,
                                             float playerX, float playerY,
                                             float playerZ) const {
  if (index >= kMaxWorldMarkers || !markers_[index].isActive) return -1.0f;
  const auto& m = markers_[index];
  const float dx = m.x - playerX;
  const float dy = m.y - playerY;
  const float dz = m.z - playerZ;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

}

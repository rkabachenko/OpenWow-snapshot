
#include "openwow/game/world_spawner.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace openwow::game {

void WorldSpawner::AddSpawn(const SpawnEntry& entry) {
  spawns_[GuidKey(entry.guid)] = entry;
}

void WorldSpawner::RemoveSpawn(ObjectGuid guid) {
  spawns_.erase(GuidKey(guid));
}

void WorldSpawner::UpdateSpawn(ObjectGuid guid, const SpawnEntry& entry) {
  auto it = spawns_.find(GuidKey(guid));
  if (it != spawns_.end()) {
    it->second = entry;
  }
}

std::optional<SpawnEntry> WorldSpawner::GetSpawn(ObjectGuid guid) const {
  auto it = spawns_.find(GuidKey(guid));
  if (it != spawns_.end()) return it->second;
  return std::nullopt;
}

std::vector<SpawnEntry> WorldSpawner::GetSpawnsInRange(
    float x, float y, float z, float range) const {
  std::vector<SpawnEntry> result;
  float rangeSq = range * range;
  for (auto& [k, entry] : spawns_) {
    if (DistSq(x, y, z, entry.x, entry.y, entry.z) <= rangeSq) {
      result.push_back(entry);
    }
  }
  return result;
}

std::optional<SpawnEntry> WorldSpawner::GetNearestSpawn(
    float x, float y, float z, SpawnType filterType) const {
  float bestDist = std::numeric_limits<float>::max();
  const SpawnEntry* best = nullptr;
  for (auto& [k, entry] : spawns_) {
    if (entry.spawnType != filterType) continue;
    float d = DistSq(x, y, z, entry.x, entry.y, entry.z);
    if (d < bestDist) {
      bestDist = d;
      best = &entry;
    }
  }
  if (best) return *best;
  return std::nullopt;
}

std::vector<SpawnEntry> WorldSpawner::GetSpawnsByType(SpawnType type) const {
  std::vector<SpawnEntry> result;
  for (auto& [k, entry] : spawns_) {
    if (entry.spawnType == type) result.push_back(entry);
  }
  return result;
}

uint32_t WorldSpawner::GetSpawnCount() const {
  return static_cast<uint32_t>(spawns_.size());
}

uint32_t WorldSpawner::GetSpawnCountByType(SpawnType type) const {
  uint32_t count = 0;
  for (auto& [k, entry] : spawns_) {
    if (entry.spawnType == type) ++count;
  }
  return count;
}

void WorldSpawner::SetVisible(ObjectGuid guid, bool visible) {
  auto it = spawns_.find(GuidKey(guid));
  if (it != spawns_.end()) {
    it->second.isVisible = visible;
  }
}

bool WorldSpawner::IsVisible(ObjectGuid guid) const {
  auto it = spawns_.find(GuidKey(guid));
  if (it != spawns_.end()) return it->second.isVisible;
  return false;
}

std::vector<SpawnEntry> WorldSpawner::GetVisibleSpawns() const {
  std::vector<SpawnEntry> result;
  for (auto& [k, entry] : spawns_) {
    if (entry.isVisible) result.push_back(entry);
  }
  return result;
}

void WorldSpawner::SetPhaseId(uint32_t phaseId) {
  phase_id_ = phaseId;
}

uint32_t WorldSpawner::GetPhaseId() const {
  return phase_id_;
}

std::vector<SpawnEntry> WorldSpawner::GetSpawnsInPhase(uint32_t phaseId) const {
  std::vector<SpawnEntry> result;
  for (auto& [k, entry] : spawns_) {
    if (entry.phaseId == phaseId) result.push_back(entry);
  }
  return result;
}

void WorldSpawner::ClearByType(SpawnType type) {
  for (auto it = spawns_.begin(); it != spawns_.end();) {
    if (it->second.spawnType == type) {
      it = spawns_.erase(it);
    } else {
      ++it;
    }
  }
}

void WorldSpawner::Clear() {
  spawns_.clear();
}

void WorldSpawner::Reset() {
  spawns_.clear();
  phase_id_ = 0;
}

}

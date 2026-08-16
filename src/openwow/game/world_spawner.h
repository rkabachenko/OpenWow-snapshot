#pragma once

#include "openwow/game/object_guid.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::game {

enum class SpawnType : uint8_t {
  Creature,
  GameObject,
  DynamicObject,
  Corpse,
  AreaTrigger,
  Transport,
};

struct SpawnEntry {
  ObjectGuid guid;
  SpawnType spawnType = SpawnType::Creature;
  uint32_t entryId = 0;
  uint32_t displayId = 0;
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float orientation = 0.0f;
  uint32_t mapId = 0;
  uint32_t phaseId = 0;
  bool isVisible = true;
  std::string modelPath;
};

class WorldSpawner {
 public:
  WorldSpawner() = default;

  void AddSpawn(const SpawnEntry& entry);
  void RemoveSpawn(ObjectGuid guid);
  void UpdateSpawn(ObjectGuid guid, const SpawnEntry& entry);
  [[nodiscard]] std::optional<SpawnEntry> GetSpawn(ObjectGuid guid) const;

  [[nodiscard]] std::vector<SpawnEntry> GetSpawnsInRange(
      float x, float y, float z, float range) const;
  [[nodiscard]] std::optional<SpawnEntry> GetNearestSpawn(
      float x, float y, float z, SpawnType filterType) const;

  [[nodiscard]] std::vector<SpawnEntry> GetSpawnsByType(SpawnType type) const;
  [[nodiscard]] uint32_t GetSpawnCount() const;
  [[nodiscard]] uint32_t GetSpawnCountByType(SpawnType type) const;

  void SetVisible(ObjectGuid guid, bool visible);
  [[nodiscard]] bool IsVisible(ObjectGuid guid) const;
  [[nodiscard]] std::vector<SpawnEntry> GetVisibleSpawns() const;

  void SetPhaseId(uint32_t phaseId);
  [[nodiscard]] uint32_t GetPhaseId() const;
  [[nodiscard]] std::vector<SpawnEntry> GetSpawnsInPhase(uint32_t phaseId) const;

  void ClearByType(SpawnType type);
  void Clear();
  void Reset();

 private:
  std::unordered_map<uint64_t, SpawnEntry> spawns_;
  uint32_t phase_id_ = 0;

  static uint64_t GuidKey(ObjectGuid g) { return g.GetRawValue(); }

  static float DistSq(float x1, float y1, float z1,
                       float x2, float y2, float z2) {
    float dx = x1 - x2;
    float dy = y1 - y2;
    float dz = z1 - z2;
    return dx * dx + dy * dy + dz * dz;
  }
};

}

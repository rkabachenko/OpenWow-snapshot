#pragma once

#include "openwow/game/object_guid.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::game {

enum class ObjectCategory : std::uint8_t {
  Player,
  Unit,
  Creature,
  Pet,
  GameObject,
  DynamicObject,
  Corpse,
  AreaTrigger,
  Item,
};

struct ObjectRegistryEntry {
  ObjectGuid guid;
  ObjectCategory category{ObjectCategory::Unit};
  std::string name;
  std::uint32_t displayId{0};
  std::uint32_t mapId{0};
  float x{0.0f};
  float y{0.0f};
  float z{0.0f};
  float facing{0.0f};
  bool isVisible{true};
  float updateTimestamp{0.0f};
};

class ObjectRegistry {
 public:
  ObjectRegistry() = default;

  void RegisterObject(const ObjectRegistryEntry& entry);

  void UnregisterObject(ObjectGuid guid);

  [[nodiscard]] std::optional<ObjectRegistryEntry> GetObject(
      ObjectGuid guid) const;

  [[nodiscard]] bool HasObject(ObjectGuid guid) const;

  [[nodiscard]] std::vector<ObjectRegistryEntry> GetObjectsByCategory(
      ObjectCategory category) const;

  [[nodiscard]] std::vector<ObjectRegistryEntry> GetNearbyObjects(
      float x, float y, float z, float range) const;

  [[nodiscard]] std::vector<ObjectRegistryEntry> GetVisibleObjects() const;

  void UpdatePosition(ObjectGuid guid, float x, float y, float z,
                      float facing);

  void UpdateVisibility(ObjectGuid guid, bool visible);

  [[nodiscard]] std::uint32_t GetObjectCount() const;

  [[nodiscard]] std::uint32_t GetObjectCountByCategory(
      ObjectCategory category) const;

  [[nodiscard]] static std::string GetCategoryName(ObjectCategory category);

  void Clear();

 private:
  std::unordered_map<ObjectGuid, ObjectRegistryEntry, ObjectGuid::Hash>
      entries_;
};

}

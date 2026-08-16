
#include "openwow/game/object_registry.h"

#include <cmath>

namespace openwow::game {

void ObjectRegistry::RegisterObject(const ObjectRegistryEntry& entry) {
  entries_.insert_or_assign(entry.guid, entry);
}

void ObjectRegistry::UnregisterObject(ObjectGuid guid) {
  entries_.erase(guid);
}

std::optional<ObjectRegistryEntry> ObjectRegistry::GetObject(
    ObjectGuid guid) const {
  auto it = entries_.find(guid);
  if (it != entries_.end()) return it->second;
  return std::nullopt;
}

bool ObjectRegistry::HasObject(ObjectGuid guid) const {
  return entries_.contains(guid);
}

std::vector<ObjectRegistryEntry> ObjectRegistry::GetObjectsByCategory(
    ObjectCategory category) const {
  std::vector<ObjectRegistryEntry> result;
  for (const auto& [guid, entry] : entries_) {
    if (entry.category == category) result.push_back(entry);
  }
  return result;
}

std::vector<ObjectRegistryEntry> ObjectRegistry::GetNearbyObjects(
    float x, float y, float z, float range) const {
  const float range_sq = range * range;
  std::vector<ObjectRegistryEntry> result;
  for (const auto& [guid, entry] : entries_) {
    const float dx = entry.x - x;
    const float dy = entry.y - y;
    const float dz = entry.z - z;
    if (dx * dx + dy * dy + dz * dz <= range_sq) {
      result.push_back(entry);
    }
  }
  return result;
}

std::vector<ObjectRegistryEntry> ObjectRegistry::GetVisibleObjects() const {
  std::vector<ObjectRegistryEntry> result;
  for (const auto& [guid, entry] : entries_) {
    if (entry.isVisible) result.push_back(entry);
  }
  return result;
}

void ObjectRegistry::UpdatePosition(ObjectGuid guid, float x, float y,
                                    float z, float facing) {
  auto it = entries_.find(guid);
  if (it != entries_.end()) {
    it->second.x = x;
    it->second.y = y;
    it->second.z = z;
    it->second.facing = facing;
  }
}

void ObjectRegistry::UpdateVisibility(ObjectGuid guid, bool visible) {
  auto it = entries_.find(guid);
  if (it != entries_.end()) {
    it->second.isVisible = visible;
  }
}

std::uint32_t ObjectRegistry::GetObjectCount() const {
  return static_cast<std::uint32_t>(entries_.size());
}

std::uint32_t ObjectRegistry::GetObjectCountByCategory(
    ObjectCategory category) const {
  std::uint32_t count = 0;
  for (const auto& [guid, entry] : entries_) {
    if (entry.category == category) ++count;
  }
  return count;
}

std::string ObjectRegistry::GetCategoryName(ObjectCategory category) {
  switch (category) {
    case ObjectCategory::Player:        return "Player";
    case ObjectCategory::Unit:          return "Unit";
    case ObjectCategory::Creature:      return "Creature";
    case ObjectCategory::Pet:           return "Pet";
    case ObjectCategory::GameObject:    return "GameObject";
    case ObjectCategory::DynamicObject: return "DynamicObject";
    case ObjectCategory::Corpse:        return "Corpse";
    case ObjectCategory::AreaTrigger:   return "AreaTrigger";
    case ObjectCategory::Item:          return "Item";
  }
  return "Unknown";
}

void ObjectRegistry::Clear() {
  entries_.clear();
}

}

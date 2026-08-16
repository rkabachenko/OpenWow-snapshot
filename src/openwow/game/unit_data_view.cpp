
#include "openwow/game/unit_data_view.h"

#include <cmath>

namespace openwow::game {

void UnitDataView::SetUnitData(ObjectGuid guid,
                               const UnitDataSnapshot& data) {
  units_.insert_or_assign(guid, data);
}

void UnitDataView::RemoveUnit(ObjectGuid guid) {
  units_.erase(guid);
}

void UnitDataView::Clear() {
  units_.clear();
}

std::optional<UnitDataSnapshot> UnitDataView::GetUnitData(
    ObjectGuid guid) const {
  auto it = units_.find(guid);
  if (it != units_.end()) return it->second;
  return std::nullopt;
}

bool UnitDataView::HasUnit(ObjectGuid guid) const {
  return units_.contains(guid);
}

std::uint32_t UnitDataView::GetHealth(ObjectGuid guid) const {
  auto it = units_.find(guid);
  return it != units_.end() ? it->second.health : 0;
}

std::uint32_t UnitDataView::GetMaxHealth(ObjectGuid guid) const {
  auto it = units_.find(guid);
  return it != units_.end() ? it->second.maxHealth : 0;
}

float UnitDataView::GetHealthPercent(ObjectGuid guid) const {
  auto it = units_.find(guid);
  if (it == units_.end() || it->second.maxHealth == 0) return 0.0f;
  return (static_cast<float>(it->second.health) /
          static_cast<float>(it->second.maxHealth)) *
         100.0f;
}

std::uint32_t UnitDataView::GetLevel(ObjectGuid guid) const {
  auto it = units_.find(guid);
  return it != units_.end() ? it->second.level : 0;
}

std::string UnitDataView::GetName(ObjectGuid guid) const {
  auto it = units_.find(guid);
  return it != units_.end() ? it->second.name : std::string{};
}

UnitReaction UnitDataView::GetReaction(ObjectGuid guid) const {
  auto it = units_.find(guid);
  return it != units_.end() ? it->second.reaction : UnitReaction::Neutral;
}

std::uint32_t UnitDataView::GetReactionColor(UnitReaction reaction) {
  switch (reaction) {
    case UnitReaction::Hostile:    return 0xFFCC0000;
    case UnitReaction::Unfriendly: return 0xFFFF6600;
    case UnitReaction::Neutral:    return 0xFFFFFF00;
    case UnitReaction::Friendly:   return 0xFF00CC00;
    case UnitReaction::Honored:    return 0xFF00CC00;
    case UnitReaction::Revered:    return 0xFF00CC00;
    case UnitReaction::Exalted:    return 0xFF00CC00;
  }
  return 0xFFFFFFFF;
}

std::string UnitDataView::GetReactionName(UnitReaction reaction) {
  switch (reaction) {
    case UnitReaction::Hostile:    return "Hostile";
    case UnitReaction::Unfriendly: return "Unfriendly";
    case UnitReaction::Neutral:    return "Neutral";
    case UnitReaction::Friendly:   return "Friendly";
    case UnitReaction::Honored:    return "Honored";
    case UnitReaction::Revered:    return "Revered";
    case UnitReaction::Exalted:    return "Exalted";
  }
  return "Unknown";
}

std::string UnitDataView::GetCreatureTypeName(CCreatureType type) {
  switch (type) {
    case CCreatureType::Beast:         return "Beast";
    case CCreatureType::Dragonkin:     return "Dragonkin";
    case CCreatureType::Demon:         return "Demon";
    case CCreatureType::Elemental:     return "Elemental";
    case CCreatureType::Giant:         return "Giant";
    case CCreatureType::Undead:        return "Undead";
    case CCreatureType::Humanoid:      return "Humanoid";
    case CCreatureType::Critter:       return "Critter";
    case CCreatureType::Mechanical:    return "Mechanical";
    case CCreatureType::NotSpecified:  return "Not specified";
    case CCreatureType::Totem:         return "Totem";
    case CCreatureType::NonCombatPet:  return "Non-Combat Pet";
    case CCreatureType::GasCloud:      return "Gas Cloud";
  }
  return "Unknown";
}

std::uint32_t UnitDataView::GetUnitCount() const {
  return static_cast<std::uint32_t>(units_.size());
}

std::vector<ObjectGuid> UnitDataView::GetUnitsInRange(float x, float y,
                                                      float z,
                                                      float range) const {
  const float range_sq = range * range;
  std::vector<ObjectGuid> result;
  for (const auto& [guid, snap] : units_) {
    const float dx = snap.x - x;
    const float dy = snap.y - y;
    const float dz = snap.z - z;
    if (dx * dx + dy * dy + dz * dz <= range_sq) {
      result.push_back(guid);
    }
  }
  return result;
}

}

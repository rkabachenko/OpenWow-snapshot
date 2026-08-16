#pragma once

#include "openwow/game/object_guid.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::game {

enum class UnitReaction : std::uint8_t {
  Hostile,
  Unfriendly,
  Neutral,
  Friendly,
  Honored,
  Revered,
  Exalted,
};

enum class CCreatureType : std::uint8_t {
  Beast,
  Dragonkin,
  Demon,
  Elemental,
  Giant,
  Undead,
  Humanoid,
  Critter,
  Mechanical,
  NotSpecified,
  Totem,
  NonCombatPet,
  GasCloud,
};

struct UnitDataSnapshot {
  ObjectGuid guid;
  std::string name;
  std::uint32_t level{0};
  std::uint32_t health{0};
  std::uint32_t maxHealth{0};
  std::uint32_t power{0};
  std::uint32_t maxPower{0};
  std::uint32_t powerType{0};
  std::uint32_t classId{0};
  std::uint32_t raceId{0};
  std::uint32_t genderId{0};
  CCreatureType creatureType{CCreatureType::NotSpecified};
  std::uint32_t displayId{0};
  std::uint32_t npcFlags{0};
  std::uint32_t unitFlags{0};
  UnitReaction reaction{UnitReaction::Neutral};
  bool isCombat{false};
  bool isMounted{false};
  bool isSitting{false};
  bool isFlying{false};

  float x{0.0f};
  float y{0.0f};
  float z{0.0f};
};

class UnitDataView {
 public:
  UnitDataView() = default;

  void SetUnitData(ObjectGuid guid, const UnitDataSnapshot& data);

  [[nodiscard]] std::optional<UnitDataSnapshot> GetUnitData(
      ObjectGuid guid) const;

  [[nodiscard]] bool HasUnit(ObjectGuid guid) const;

  [[nodiscard]] std::uint32_t GetHealth(ObjectGuid guid) const;
  [[nodiscard]] std::uint32_t GetMaxHealth(ObjectGuid guid) const;
  [[nodiscard]] float GetHealthPercent(ObjectGuid guid) const;

  [[nodiscard]] std::uint32_t GetLevel(ObjectGuid guid) const;

  [[nodiscard]] std::string GetName(ObjectGuid guid) const;

  [[nodiscard]] UnitReaction GetReaction(ObjectGuid guid) const;

  [[nodiscard]] static std::uint32_t GetReactionColor(UnitReaction reaction);

  [[nodiscard]] static std::string GetReactionName(UnitReaction reaction);

  [[nodiscard]] static std::string GetCreatureTypeName(CCreatureType type);

  [[nodiscard]] std::uint32_t GetUnitCount() const;

  [[nodiscard]] std::vector<ObjectGuid> GetUnitsInRange(float x, float y,
                                                        float z,
                                                        float range) const;

  void RemoveUnit(ObjectGuid guid);

  void Clear();

 private:
  std::unordered_map<ObjectGuid, UnitDataSnapshot, ObjectGuid::Hash> units_;
};

}


#include "openwow/game/power_lua_bridge.h"

#include "openwow/game/spell_failure_names.h"
#include "openwow/game/unit_query_bridge.h"

#include <algorithm>
#include <cctype>

namespace openwow::game {

PowerLuaBridge& PowerLuaBridge::Get() {
  static PowerLuaBridge instance;
  return instance;
}

static std::optional<UnitQuerySnapshot> ResolveUnit(
    const std::string& unitId) {
  return UnitQueryBridge::Get().Query(nullptr, unitId);
}

std::string PowerLuaBridge::PowerTypeToken(int powerType) {
  return PowerTypeToString(static_cast<std::uint32_t>(powerType));
}

std::string PowerLuaBridge::ClassNameFromId(std::uint8_t classId) {
  switch (classId) {
    case 1:  return "Warrior";
    case 2:  return "Paladin";
    case 3:  return "Hunter";
    case 4:  return "Rogue";
    case 5:  return "Priest";
    case 6:  return "Death Knight";
    case 7:  return "Shaman";
    case 8:  return "Mage";
    case 9:  return "Warlock";
    case 11: return "Druid";
    default: return "Unknown";
  }
}

std::string PowerLuaBridge::ClassFileFromId(std::uint8_t classId) {
  switch (classId) {
    case 1:  return "WARRIOR";
    case 2:  return "PALADIN";
    case 3:  return "HUNTER";
    case 4:  return "ROGUE";
    case 5:  return "PRIEST";
    case 6:  return "DEATHKNIGHT";
    case 7:  return "SHAMAN";
    case 8:  return "MAGE";
    case 9:  return "WARLOCK";
    case 11: return "DRUID";
    default: return "UNKNOWN";
  }
}

std::string PowerLuaBridge::RaceNameFromId(std::uint8_t raceId) {
  switch (raceId) {
    case 1:  return "Human";
    case 2:  return "Orc";
    case 3:  return "Dwarf";
    case 4:  return "Night Elf";
    case 5:  return "Undead";
    case 6:  return "Tauren";
    case 7:  return "Gnome";
    case 8:  return "Troll";
    case 10: return "Blood Elf";
    case 11: return "Draenei";
    default: return "Unknown";
  }
}

std::string PowerLuaBridge::RaceFileFromId(std::uint8_t raceId) {
  std::string name = RaceNameFromId(raceId);

  std::string result;
  result.reserve(name.size());
  for (char c : name) {
    if (c != ' ') {
      result += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
  }
  return result;
}

std::int32_t PowerLuaBridge::UnitPower(const std::string& unitId,
                                       int powerType) const {
  auto snap = ResolveUnit(unitId);
  if (!snap) return 0;

  if (powerType == -1 || powerType == static_cast<int>(snap->powerType)) {
    return static_cast<std::int32_t>(snap->power);
  }

  if (powerType == kMana && snap->powerType == 0) {
    return static_cast<std::int32_t>(snap->power);
  }

  return 0;
}

std::int32_t PowerLuaBridge::UnitPowerMax(const std::string& unitId,
                                          int powerType) const {
  auto snap = ResolveUnit(unitId);
  if (!snap) return 0;

  if (powerType == -1 || powerType == static_cast<int>(snap->powerType)) {
    return static_cast<std::int32_t>(snap->maxPower);
  }

  if (powerType == kMana && snap->powerType == 0) {
    return static_cast<std::int32_t>(snap->maxPower);
  }

  return 0;
}

PowerLuaBridge::PowerTypeResult PowerLuaBridge::UnitPowerType(
    const std::string& unitId) const {
  auto snap = ResolveUnit(unitId);
  if (!snap) return {kMana, ""};

  int pt = static_cast<int>(snap->powerType);
  return {pt, PowerTypeToken(pt)};
}

std::int32_t PowerLuaBridge::UnitHealth(const std::string& unitId) const {
  auto snap = ResolveUnit(unitId);
  return snap ? static_cast<std::int32_t>(snap->health) : 0;
}

std::int32_t PowerLuaBridge::UnitHealthMax(const std::string& unitId) const {
  auto snap = ResolveUnit(unitId);
  return snap ? static_cast<std::int32_t>(snap->maxHealth) : 0;
}

std::int32_t PowerLuaBridge::UnitMana(const std::string& unitId) const {
  return UnitPower(unitId, kMana);
}

std::int32_t PowerLuaBridge::UnitManaMax(const std::string& unitId) const {
  return UnitPowerMax(unitId, kMana);
}

std::int32_t PowerLuaBridge::UnitLevel(const std::string& unitId) const {
  auto snap = ResolveUnit(unitId);
  return snap ? static_cast<std::int32_t>(snap->level) : 0;
}

std::string PowerLuaBridge::UnitName(const std::string& unitId) const {
  auto snap = ResolveUnit(unitId);
  return snap ? snap->name : std::string{};
}

PowerLuaBridge::ClassResult PowerLuaBridge::UnitClass(
    const std::string& unitId) const {
  auto snap = ResolveUnit(unitId);
  if (!snap) return {"Unknown", "UNKNOWN", 0};

  return {ClassNameFromId(snap->classId), ClassFileFromId(snap->classId),
          snap->classId};
}

PowerLuaBridge::RaceResult PowerLuaBridge::UnitRace(
    const std::string& unitId) const {
  auto snap = ResolveUnit(unitId);
  if (!snap) return {"Unknown", "UNKNOWN", 0};

  return {RaceNameFromId(snap->raceId), RaceFileFromId(snap->raceId),
          snap->raceId};
}

std::uint8_t PowerLuaBridge::UnitSex(const std::string& unitId) const {
  auto snap = ResolveUnit(unitId);
  if (!snap) return 1;

  switch (snap->genderId) {
    case 0: return 2;
    case 1: return 3;
    default: return 1;
  }
}

bool PowerLuaBridge::UnitIsDeadOrGhost(const std::string& unitId) const {
  auto snap = ResolveUnit(unitId);
  if (!snap) return true;
  return snap->isDead || snap->isGhost;
}

bool PowerLuaBridge::UnitIsConnected(const std::string& unitId) const {
  auto snap = ResolveUnit(unitId);
  if (!snap) return false;
  return snap->isConnected;
}

bool PowerLuaBridge::UnitIsUnit(const std::string& unitId1,
                                const std::string& unitId2) const {
  auto snap1 = ResolveUnit(unitId1);
  auto snap2 = ResolveUnit(unitId2);
  if (!snap1 || !snap2) return false;
  return snap1->guid == snap2->guid;
}

bool PowerLuaBridge::UnitExists(const std::string& unitId) const {
  auto snap = ResolveUnit(unitId);
  return snap.has_value();
}

}

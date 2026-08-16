
#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

struct RacialTraitEntry {
    uint32_t spellId = 0;
    std::string name;
    std::string description;
};

struct RaceInfoDisplayEntry {
    uint32_t raceId    = 0;
    std::string raceName;
    uint32_t factionId = 0;
    std::string description;
    std::vector<RacialTraitEntry> racialTraits;
    std::string startingArea;
};

struct ClassInfoDisplayEntry {
    uint32_t classId = 0;
    std::string className;
    std::string description;
    std::string primaryStats;
    std::string roles;
    std::string iconPath;
};

struct RaceClassValidEntry {
    uint32_t raceId  = 0;
    uint32_t classId = 0;
    bool isAllowed   = false;
};

class RaceClassInfoManager {
 public:
  static RaceClassInfoManager& Get();

  RaceClassInfoManager(const RaceClassInfoManager&) = delete;
  RaceClassInfoManager& operator=(const RaceClassInfoManager&) = delete;

  void AddRaceInfo(const RaceInfoDisplayEntry& entry);
  [[nodiscard]] std::optional<RaceInfoDisplayEntry> GetRaceInfo(
      uint32_t raceId) const;
  [[nodiscard]] std::vector<RaceInfoDisplayEntry> GetAllRaces() const;
  [[nodiscard]] std::vector<RaceInfoDisplayEntry> GetRacesByFaction(
      uint32_t factionId) const;

  void AddClassInfo(const ClassInfoDisplayEntry& entry);
  [[nodiscard]] std::optional<ClassInfoDisplayEntry> GetClassInfo(
      uint32_t classId) const;
  [[nodiscard]] std::vector<ClassInfoDisplayEntry> GetAllClasses() const;

  void SetRaceClassAllowed(uint32_t raceId, uint32_t classId, bool allowed);
  [[nodiscard]] bool IsRaceClassAllowed(uint32_t raceId,
                                        uint32_t classId) const;
  [[nodiscard]] std::vector<uint32_t> GetAllowedClassesForRace(
      uint32_t raceId) const;
  [[nodiscard]] std::vector<uint32_t> GetAllowedRacesForClass(
      uint32_t classId) const;

  void RegisterWotLKDefaults();

  [[nodiscard]] std::string GetFactionName(uint32_t factionId) const;

  void Reset();

 private:
  RaceClassInfoManager() = default;

  std::vector<RaceInfoDisplayEntry> races_;
  std::vector<ClassInfoDisplayEntry> classes_;
  std::vector<RaceClassValidEntry> combos_;

  mutable std::mutex mutex_;
};

}

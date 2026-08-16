
#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace openwow::game {

namespace world_state_ids {

inline constexpr std::int32_t kWintergraspController = 3803;
inline constexpr std::int32_t kWintergraspTimer = 3781;
inline constexpr std::int32_t kWintergraspDefending = 3802;

inline constexpr std::int32_t kHellfireOverlookHorde = 2398;
inline constexpr std::int32_t kHellfireOverlookAlliance = 2397;
inline constexpr std::int32_t kHellfireStadiumHorde = 2393;
inline constexpr std::int32_t kHellfireStadiumAlliance = 2392;
inline constexpr std::int32_t kHellfireBrokenHillHorde = 2396;
inline constexpr std::int32_t kHellfireBrokenHillAlliance = 2395;

inline constexpr std::int32_t kTerokkarTowersAllianceCount = 2621;
inline constexpr std::int32_t kTerokkarTowersHordeCount = 2622;

inline constexpr std::int32_t kZangarmarshAllianceBeacon = 2655;
inline constexpr std::int32_t kZangarmarshHordeBeacon = 2656;

inline constexpr std::int32_t kHalaaController = 2491;
inline constexpr std::int32_t kHalaaAllianceGuards = 2493;
inline constexpr std::int32_t kHalaaHordeGuards = 2491;
}

enum class PvPFaction : std::uint8_t {
  kNeutral = 0,
  kAlliance = 1,
  kHorde = 2,
};

struct WorldStateFlagEntry {
  std::int32_t state_id{0};
  std::int32_t value{0};
};

class WorldStateFlags {
 public:
  static WorldStateFlags& Get();

  void SetWorldState(std::int32_t state_id, std::int32_t value);
  [[nodiscard]] std::int32_t GetWorldState(std::int32_t state_id) const;

  void ClearAllStates();
  [[nodiscard]] std::vector<WorldStateFlagEntry> GetAllStates() const;
  [[nodiscard]] std::size_t GetStateCount() const;

  [[nodiscard]] bool IsWintergraspControlled(PvPFaction faction) const;
  [[nodiscard]] std::int32_t GetWintergraspTimer() const;

  [[nodiscard]] std::int32_t GetHellfireTowersControlled(
      PvPFaction faction) const;

  [[nodiscard]] std::int32_t GetTerokkarTowersControlled(
      PvPFaction faction) const;

  [[nodiscard]] bool IsZangarmarshBeaconActive(PvPFaction faction) const;

  [[nodiscard]] PvPFaction GetHalaaController() const;

  [[nodiscard]] std::int32_t GetOutdoorObjectivesCaptured(
      PvPFaction faction) const;

  void Reset();

 private:
  WorldStateFlags() = default;

  [[nodiscard]] std::int32_t GetStateLocked(std::int32_t state_id) const;

  std::unordered_map<std::int32_t, std::int32_t> states_;
  mutable std::mutex mutex_;
};

}

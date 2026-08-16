#pragma once

#include "openwow/data/formats/dbc/dbc_entries_extended.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"

#include <cstdint>

namespace openwow::ui::game::detail {

enum class AchievementFaction : std::int32_t {
  kAny = -1,
  kAlliance = 0,
  kHorde = 1,
};

inline AchievementFaction ResolveAchievementFaction(
    const openwow::game::WorldObject* unit) {
  switch (GetUnitRace(unit)) {
    case 1:
    case 3:
    case 4:
    case 7:
    case 11:
      return AchievementFaction::kAlliance;
    case 2:
    case 5:
    case 6:
    case 8:
    case 10:
      return AchievementFaction::kHorde;
    default:
      return AchievementFaction::kAny;
  }
}

inline bool MatchesAchievementFaction(
    const openwow::data::dbc::AchievementEntry& achievement,
    const AchievementFaction faction) {
  return faction == AchievementFaction::kAny || achievement.faction == -1 ||
         achievement.faction == static_cast<std::int32_t>(faction);
}

}

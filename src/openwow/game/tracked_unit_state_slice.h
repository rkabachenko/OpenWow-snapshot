
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "openwow/game/object_guid.h"

namespace openwow::game {

class BattlefieldInfo;
class GroupSystem;
class PartyStatsManager;
class ObjectManager;

struct TrackedControlledUnitStateSlice {
  ObjectGuid controlled_unit_guid;
  ObjectGuid owner_guid;
  std::uint32_t display_id = 0;

  std::uint32_t cur_hp = 0;
  std::uint32_t max_hp = 0;
  std::uint8_t power_type = 0;
  std::uint16_t cur_power = 0;
  std::uint16_t max_power = 0;
  std::uint16_t status = 0;
  std::string name;
};

[[nodiscard]] std::optional<TrackedControlledUnitStateSlice>
FindTrackedPartyControlledUnitStateSliceByGuid(
    const ObjectManager& objects, const ObjectGuid &guid,
    const GroupSystem &group,
    const PartyStatsManager &party_stats);

[[nodiscard]] std::optional<TrackedControlledUnitStateSlice>
FindTrackedRaidMemberStateSliceByControlledUnitGuid(
    const ObjectManager& objects, const ObjectGuid &guid,
    const GroupSystem &group,
    const PartyStatsManager &party_stats);

[[nodiscard]] std::optional<TrackedControlledUnitStateSlice>
FindArenaOpponentPetStateSliceByGuid(
    const ObjectGuid &guid,
    const BattlefieldInfo &battlefield,
    const PartyStatsManager &party_stats);

[[nodiscard]] std::optional<TrackedControlledUnitStateSlice>
FindTrackedUnitStateSliceByGuid(
    const ObjectManager& objects, const ObjectGuid &guid,
    const GroupSystem &group,
    const PartyStatsManager &party_stats,
    const BattlefieldInfo &battlefield);

}

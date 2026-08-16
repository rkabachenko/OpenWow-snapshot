
#include "openwow/game/tracked_unit_state_slice.h"

#include "openwow/game/battlefield_info.h"
#include "openwow/game/group_system.h"
#include "openwow/game/party_stats.h"
#include "openwow/game/object_manager.h"

namespace openwow::game {

namespace {

TrackedControlledUnitStateSlice
MakeSliceFromCachedStats(const CachedPartyMemberStats &cached,
                         const ObjectGuid controlled_unit_guid,
                         const ObjectGuid owner_guid) {
  return TrackedControlledUnitStateSlice{
      .controlled_unit_guid = controlled_unit_guid,
      .owner_guid = owner_guid,
      .display_id = cached.stats.pet_display_id,
      .cur_hp = cached.stats.pet_cur_hp,
      .max_hp = cached.stats.pet_max_hp,
      .power_type = cached.stats.pet_power_type,
      .cur_power = cached.stats.pet_cur_power,
      .max_power = cached.stats.pet_max_power,
      .status = cached.stats.status,
      .name = cached.stats.pet_name,
  };
}

}

std::optional<TrackedControlledUnitStateSlice>
FindTrackedPartyControlledUnitStateSliceByGuid(
    const ObjectManager& objects, const ObjectGuid &guid,
    const GroupSystem &group,
    const PartyStatsManager &party_stats) {
  if (guid.IsEmpty()) {
    return std::nullopt;
  }

  int party_index = -1;
  if (!group.FindPartyMemberByControlledUnitGuid(objects, guid.GetRawValue(),
                                                  &party_index) ||
      party_index < 0) {
    return std::nullopt;
  }

  const auto owner_raw =
      group.GetTrackedPartyMemberGuid(static_cast<std::uint32_t>(party_index));
  if (owner_raw == 0) {
    return std::nullopt;
  }

  const auto cached = party_stats.GetCachedMember(owner_raw);
  if (!cached.has_value() ||
      cached->stats.pet_guid != guid.GetRawValue()) {
    return std::nullopt;
  }

  return MakeSliceFromCachedStats(*cached, guid, ObjectGuid(owner_raw));
}

std::optional<TrackedControlledUnitStateSlice>
FindTrackedRaidMemberStateSliceByControlledUnitGuid(
    const ObjectManager& objects, const ObjectGuid &guid,
    const GroupSystem &group,
    const PartyStatsManager &party_stats) {
  if (guid.IsEmpty()) {
    return std::nullopt;
  }

  int raid_index = -1;
  if (!group.FindRaidMemberByControlledUnitGuid(objects, guid.GetRawValue(),
                                                 &raid_index) ||
      raid_index < 0) {
    return std::nullopt;
  }

  const auto *member =
      group.GetMember(static_cast<std::size_t>(raid_index));
  if (member == nullptr || member->guid == 0) {
    return std::nullopt;
  }

  const auto cached = party_stats.GetCachedMember(member->guid);
  if (!cached.has_value() ||
      cached->stats.pet_guid != guid.GetRawValue()) {
    return std::nullopt;
  }

  return MakeSliceFromCachedStats(*cached, guid,
                                  ObjectGuid(member->guid));
}

std::optional<TrackedControlledUnitStateSlice>
FindArenaOpponentPetStateSliceByGuid(
    const ObjectGuid &guid,
    const BattlefieldInfo &battlefield,
    const PartyStatsManager &party_stats) {
  if (guid.IsEmpty()) {
    return std::nullopt;
  }

  for (std::size_t slot = 0; slot < kMaxArenaOpponents; ++slot) {
    const auto &opponent = battlefield.GetArenaOpponent(slot);
    if (opponent.guid.IsEmpty()) {
      continue;
    }
    if (opponent.pet_guid != guid) {
      continue;
    }

    if (opponent.pet_state.has_value() &&
        opponent.pet_state->controlled_unit_guid == guid) {
      return opponent.pet_state;
    }

    const auto cached =
        party_stats.GetCachedMember(opponent.guid.GetRawValue());
    if (cached.has_value() &&
        cached->stats.pet_guid == guid.GetRawValue()) {
      return MakeSliceFromCachedStats(*cached, guid, opponent.guid);
    }

    return TrackedControlledUnitStateSlice{
        .controlled_unit_guid = guid,
        .owner_guid = opponent.guid,
    };
  }

  return std::nullopt;
}

std::optional<TrackedControlledUnitStateSlice>
FindTrackedUnitStateSliceByGuid(
    const ObjectManager& objects, const ObjectGuid &guid,
    const GroupSystem &group,
    const PartyStatsManager &party_stats,
    const BattlefieldInfo &battlefield) {
  if (guid.IsEmpty()) {
    return std::nullopt;
  }

  if (auto result = FindTrackedPartyControlledUnitStateSliceByGuid(
          objects, guid, group, party_stats)) {
    return result;
  }

  if (auto result = FindTrackedRaidMemberStateSliceByControlledUnitGuid(
          objects, guid, group, party_stats)) {
    return result;
  }

  return FindArenaOpponentPetStateSliceByGuid(guid, battlefield,
                                              party_stats);
}

}

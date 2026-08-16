
#include "openwow/game/world_state_flags.h"

#include "openwow/foundation/diagnostics/logging.h"

namespace openwow::game {

using openwow::diagnostics::Log;
using openwow::diagnostics::LogLevel;

WorldStateFlags& WorldStateFlags::Get() {
  static WorldStateFlags instance;
  return instance;
}

void WorldStateFlags::SetWorldState(std::int32_t state_id,
                                    std::int32_t value) {
  std::lock_guard lock(mutex_);
  states_[state_id] = value;
}

std::int32_t WorldStateFlags::GetWorldState(std::int32_t state_id) const {
  std::lock_guard lock(mutex_);
  return GetStateLocked(state_id);
}

void WorldStateFlags::ClearAllStates() {
  std::lock_guard lock(mutex_);
  states_.clear();
}

std::vector<WorldStateFlagEntry> WorldStateFlags::GetAllStates() const {
  std::lock_guard lock(mutex_);
  std::vector<WorldStateFlagEntry> result;
  result.reserve(states_.size());
  for (const auto& [id, val] : states_) {
    result.push_back({id, val});
  }
  return result;
}

std::size_t WorldStateFlags::GetStateCount() const {
  std::lock_guard lock(mutex_);
  return states_.size();
}

std::int32_t WorldStateFlags::GetStateLocked(std::int32_t state_id) const {
  auto it = states_.find(state_id);
  return (it != states_.end()) ? it->second : 0;
}

bool WorldStateFlags::IsWintergraspControlled(PvPFaction faction) const {
  std::lock_guard lock(mutex_);
  auto controller = GetStateLocked(world_state_ids::kWintergraspController);

  return controller == static_cast<std::int32_t>(faction);
}

std::int32_t WorldStateFlags::GetWintergraspTimer() const {
  std::lock_guard lock(mutex_);
  return GetStateLocked(world_state_ids::kWintergraspTimer);
}

std::int32_t WorldStateFlags::GetHellfireTowersControlled(
    PvPFaction faction) const {
  std::lock_guard lock(mutex_);
  std::int32_t count = 0;
  if (faction == PvPFaction::kAlliance) {
    count += (GetStateLocked(world_state_ids::kHellfireOverlookAlliance) != 0)
                 ? 1
                 : 0;
    count += (GetStateLocked(world_state_ids::kHellfireStadiumAlliance) != 0)
                 ? 1
                 : 0;
    count +=
        (GetStateLocked(world_state_ids::kHellfireBrokenHillAlliance) != 0)
            ? 1
            : 0;
  } else if (faction == PvPFaction::kHorde) {
    count += (GetStateLocked(world_state_ids::kHellfireOverlookHorde) != 0)
                 ? 1
                 : 0;
    count += (GetStateLocked(world_state_ids::kHellfireStadiumHorde) != 0)
                 ? 1
                 : 0;
    count += (GetStateLocked(world_state_ids::kHellfireBrokenHillHorde) != 0)
                 ? 1
                 : 0;
  }
  return count;
}

std::int32_t WorldStateFlags::GetTerokkarTowersControlled(
    PvPFaction faction) const {
  std::lock_guard lock(mutex_);
  if (faction == PvPFaction::kAlliance) {
    return GetStateLocked(world_state_ids::kTerokkarTowersAllianceCount);
  }
  if (faction == PvPFaction::kHorde) {
    return GetStateLocked(world_state_ids::kTerokkarTowersHordeCount);
  }
  return 0;
}

bool WorldStateFlags::IsZangarmarshBeaconActive(PvPFaction faction) const {
  std::lock_guard lock(mutex_);
  if (faction == PvPFaction::kAlliance) {
    return GetStateLocked(world_state_ids::kZangarmarshAllianceBeacon) != 0;
  }
  if (faction == PvPFaction::kHorde) {
    return GetStateLocked(world_state_ids::kZangarmarshHordeBeacon) != 0;
  }
  return false;
}

PvPFaction WorldStateFlags::GetHalaaController() const {
  std::lock_guard lock(mutex_);
  auto val = GetStateLocked(world_state_ids::kHalaaController);
  if (val == static_cast<std::int32_t>(PvPFaction::kAlliance))
    return PvPFaction::kAlliance;
  if (val == static_cast<std::int32_t>(PvPFaction::kHorde))
    return PvPFaction::kHorde;
  return PvPFaction::kNeutral;
}

std::int32_t WorldStateFlags::GetOutdoorObjectivesCaptured(
    PvPFaction faction) const {

  std::int32_t total = 0;
  total += GetHellfireTowersControlled(faction);
  total += GetTerokkarTowersControlled(faction);
  if (IsZangarmarshBeaconActive(faction)) ++total;
  if (GetHalaaController() == faction) ++total;
  if (IsWintergraspControlled(faction)) ++total;
  return total;
}

void WorldStateFlags::Reset() {
  std::lock_guard lock(mutex_);
  states_.clear();
}

}

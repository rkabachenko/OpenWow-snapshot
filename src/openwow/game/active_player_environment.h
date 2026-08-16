#pragma once

#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/world_environment_state.h"

namespace openwow::game {

[[nodiscard]] inline bool IsPlayerIndoors(const CGPlayer_C& player) {
  const auto* const world_environment = player.world_environment();
  if (world_environment == nullptr) {
    return false;
  }

  const auto player_position = player.GetPosition();
  if (const auto outdoors =
          world_environment->QueryOutdoorStateAtWorldPosition(
              player_position.x, player_position.y, player_position.z);
      outdoors.has_value()) {
    return !*outdoors;
  }
  return world_environment->IsIndoors();
}

[[nodiscard]] inline bool IsPlayerOutdoors(const CGPlayer_C& player) {
  const auto* const world_environment = player.world_environment();
  if (world_environment == nullptr) {
    return false;
  }

  const auto player_position = player.GetPosition();
  if (const auto outdoors =
          world_environment->QueryOutdoorStateAtWorldPosition(
              player_position.x, player_position.y, player_position.z);
      outdoors.has_value()) {
    return *outdoors;
  }
  return !world_environment->IsIndoors();
}

}

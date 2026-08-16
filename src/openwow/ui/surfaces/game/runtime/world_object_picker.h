#pragma once

#include "openwow/game/object_guid.h"

#include <cstdint>

namespace openwow::game {

class CGObject_C;
class CGPlayer_C;
class WorldSession;

}

namespace openwow::ui::game {

struct WorldObjectPickContext {
  const openwow::game::WorldSession* session = nullptr;
  const openwow::game::CGPlayer_C* active_player = nullptr;
  const openwow::game::CGObject_C* facing_anchor = nullptr;
  float direction_x = 0.0f;
  float direction_y = 0.0f;
  float direction_z = 0.0f;
  std::uint32_t client_tick = 0;
  openwow::game::ObjectGuid best_target{};
  float best_dot = 0.0f;
  float best_distance_squared = 10000.0f;
};

void EvaluateWorldObjectPickCandidate(
    openwow::game::ObjectGuid candidate,
    WorldObjectPickContext& context);

[[nodiscard]] openwow::game::ObjectGuid ResolveFacingMouseoverTarget(
    const openwow::game::WorldSession& session);

[[nodiscard]] bool TryResolveGatherInteractionMaxRange(
    const openwow::game::WorldSession& session,
    const openwow::game::CGPlayer_C& active_player,
    std::uint32_t spell_id,
    float& max_range);

}

#pragma once

#include "openwow/game/world_session_fwd.h"

#include <cstdint>

namespace openwow::ui::game {

struct Vec3f {
  float x{0.0f};
  float y{0.0f};
  float z{0.0f};
};

struct CorpsePositionData {
  bool found{false};
  std::int32_t map_id{-1};
  std::int32_t corpse_map_id{-1};
  Vec3f position{};
};

[[nodiscard]] CorpsePositionData GameUI_GetCorpsePositionData();
[[nodiscard]] CorpsePositionData GameUI_GetCorpsePositionData(
    openwow::game::WorldSession& session);
Vec3f GameUI_GetCorpseMapPosition();

namespace detail {
void ResetCorpseMapPositionQueryThrottleForTests();
}

}

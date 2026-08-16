#pragma once

#include <cstdint>

namespace openwow::game {
class WorldSession;
}

namespace openwow::ui::game {

void GameUI_UpdateComboPoints(
    openwow::game::WorldSession& session, std::uint64_t target_guid,
    std::uint8_t combo_points);
}

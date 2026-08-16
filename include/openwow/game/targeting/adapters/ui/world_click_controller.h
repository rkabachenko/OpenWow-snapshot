#pragma once

#include "openwow/game/targeting/world_click_types.h"

namespace openwow::game {
class WorldSession;

namespace targeting::ui {

void HandleWorldObjectClick(WorldSession &session,
                            const WorldObjectClick &click);
void HandleWorldTerrainClick(WorldSession &session,
                             const WorldTerrainClick &click);
void HandleEmptyWorldClick(WorldSession &session,
                           const EmptyWorldClick &click);

}
}

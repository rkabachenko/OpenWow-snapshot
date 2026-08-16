#pragma once

#include <cstdint>

namespace openwow::game {

class WorldSession;

}

namespace openwow::world {
class WorldMap;
}

namespace openwow::game {

int Player_C_TickAreaCheck(WorldSession& session,
                           const openwow::world::WorldMap& world_map);

void Player_C_ResetAreaTickCounter();

void Player_C_ResetAreaStateCache();

std::int32_t Player_C_GetCachedAreaEntryId();
std::int32_t Player_C_GetCachedZoneEntryId();
std::int32_t Player_C_GetIndoorAreaActive();
std::int32_t Player_C_GetCachedZoneAmbienceId();

}

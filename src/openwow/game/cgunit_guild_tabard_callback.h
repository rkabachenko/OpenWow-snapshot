#pragma once

#include <cstdint>

#include "openwow/game/guild_manager.h"
#include "openwow/game/object_guid.h"

namespace openwow::game {

class GuildManager;
class ObjectManager;

[[nodiscard]] bool CGUnit_GuildCacheCallback_RefreshTabardTextures(
    std::uint32_t guild_id, const ObjectGuid& unit_guid,
    const GuildManager& guild_manager, ObjectManager& object_manager,
    bool resolved);

}

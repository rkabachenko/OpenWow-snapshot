#pragma once

#include <cstdint>

namespace openwow::net::wotlk {
struct WorldPacket;
}

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::ui::game {
class CVarSystem;
}

namespace openwow::game {

class ItemDefinitions;
class Localization;
class LootInteraction;
class ObjectManager;
class QueryCache;

void HandleLootStartRollPacket(
    LootInteraction& loot, ItemDefinitions& items, QueryCache& queries,
    std::uint32_t active_map_id, std::uint32_t now,
    const net::wotlk::WorldPacket& packet);
void HandleLootRollPacket(
    ObjectManager& objects, QueryCache& queries, Localization& localization,
    openwow::ui::game::CVarSystem& cvars,
    const openwow::data::dbc::DbcLoader* dbc,
    const net::wotlk::WorldPacket& packet);
void HandleLootAllPassedPacket(
    LootInteraction& loot, ObjectManager& objects, QueryCache& queries,
    Localization& localization, openwow::ui::game::CVarSystem& cvars,
    const openwow::data::dbc::DbcLoader* dbc,
    const net::wotlk::WorldPacket& packet);
void HandleLootRollWonPacket(
    LootInteraction& loot, ObjectManager& objects, QueryCache& queries,
    Localization& localization, openwow::ui::game::CVarSystem& cvars,
    const openwow::data::dbc::DbcLoader* dbc,
    const net::wotlk::WorldPacket& packet);

}

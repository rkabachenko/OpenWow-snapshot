
#pragma once

#include "openwow/game/inventory/items/item_definitions.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::game {
class PlayerInventoryReplica;
class WorldSession;
}

namespace openwow::ui {

struct TooltipLine {
    std::string text;
    std::string color;
    bool is_left = true;
    std::string right_text;
    std::string right_color;
    bool wrap = false;
};

struct TooltipItemInstanceData {
    std::uint32_t permanent_enchant_id = 0;
    std::array<std::uint32_t, 3> gem_item_ids = {};
    std::uint32_t durability = 0;
    std::uint32_t max_durability = 0;
    std::uint32_t runtime_flags = 0;
    std::uint64_t item_guid = 0;
    std::uint32_t remaining_duration_seconds = 0;
    std::uint32_t create_played_time = 0;
    std::uint64_t creator_guid = 0;
    std::array<std::int32_t, 5> spell_charges = {};
    bool locked = false;
    bool live_item = false;
};

class TooltipBuilder {
public:

    static std::vector<TooltipLine> BuildItemTooltip(
        const openwow::game::ItemTemplate& item,
        const openwow::game::ItemDefinitions& item_definitions,
        const openwow::game::PlayerInventoryReplica* inventory,
        uint32_t player_level = 80,
        uint32_t player_class_mask = 0xFFFF,
        uint32_t player_race_mask = 0xFFFF,
        const openwow::data::dbc::DbcLoader* dbc = nullptr,
        uint32_t scaling_level = 0,
        const TooltipItemInstanceData* instance_data = nullptr,
        const openwow::game::WorldSession* session = nullptr
    );

    static std::vector<TooltipLine> BuildSpellTooltip(
        uint32_t spell_id,
        const std::string& name,
        const std::string& rank,
        const std::string& description,
        uint32_t mana_cost,
        float range,
        float cast_time,
        float cooldown
    );

    static std::vector<TooltipLine> BuildAchievementTooltip(
        const std::string& name,
        const std::string& description,
        uint32_t points,
        bool completed,
        const std::string& date = ""
    );
};

}

#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "openwow/game/inventory/loot/loot_interaction.h"
#include "openwow/game/inventory/loot/loot_roll_messages.h"

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::game {

class ObjectManager;
class QueryCache;
class Localization;
enum class ItemQuality : std::uint8_t;
}
namespace openwow::ui::game {
class CVarSystem;
}
namespace openwow::game {

struct LootRollResultDisplayParams {
  std::string player_name;
  std::uint64_t roller_guid = 0;
  std::uint32_t item_id = 0;
  ItemQuality item_quality{};
  std::int32_t random_property_id = 0;
  std::int32_t random_suffix = 0;
  std::string item_link;

  std::uint8_t roll_number = 0;

  GroupRollType roll_type = GroupRollType::kPass;

  bool auto_pass = false;

  std::uint64_t active_player_guid = 0;

  bool detailed_loot_messages = true;
};

[[nodiscard]] std::string FormatLootRollResultMessage(
    Localization& localization, const LootRollResultDisplayParams& params);

[[nodiscard]] std::string FormatLootRollWinnerMessage(
    Localization& localization, const LootRollResultDisplayParams& params);

void ResolveAndDisplayLootRollResult(
    ObjectManager& objects, QueryCache& queries,
    Localization& localization, openwow::ui::game::CVarSystem& cvars,
    std::function<void(std::string)> display,
    const openwow::data::dbc::DbcLoader* dbc, const GroupLootRollResult& result,
    std::function<void(bool)> on_complete = {});
void ResolveAndDisplayLootRollWinner(
    ObjectManager& objects, QueryCache& queries,
    Localization& localization, openwow::ui::game::CVarSystem& cvars,
    std::function<void(std::string)> display,
    const openwow::data::dbc::DbcLoader* dbc, const LootRollWon& winner,
    std::function<void(bool)> on_complete = {});
void ResolveAndDisplayLootAllPassed(
    ObjectManager& objects, QueryCache& queries,
    Localization& localization, openwow::ui::game::CVarSystem& cvars,
    std::function<void(std::string)> display,
    const openwow::data::dbc::DbcLoader* dbc, const GroupLootAllPassed& all_passed,
    std::function<void(bool)> on_complete = {});

}

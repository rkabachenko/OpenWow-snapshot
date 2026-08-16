#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

struct lua_State;

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::game {
class CGItem_C;
class ItemDefinitions;
class LootInteraction;
class ObjectManager;
struct LootWindow;
}

namespace openwow::ui::game::detail {

struct LootTooltipItemData {
  std::uint32_t item_id = 0;
  std::uint32_t display_id = 0;
  std::uint32_t item_count = 0;
  std::uint32_t suffix_factor = 0;
  std::int32_t random_property_id = 0;
  std::uint8_t quality = 0;
  bool is_locked = false;
};

const openwow::game::LootWindow* GetActiveLootWindow(
    const openwow::game::LootInteraction& loot);
int UISlotToItemIndex(bool has_gold, int ui_slot, std::size_t item_count);
std::optional<LootTooltipItemData> ResolveLootTooltipItem(
    const openwow::game::LootInteraction& loot, int ui_slot);

std::string ResolveLootItemBaseName(
    const openwow::game::ItemDefinitions& item_definitions, std::uint32_t item_id);
int ResolveCachedLootItemQuality(
    const openwow::game::ItemDefinitions& item_definitions, std::uint32_t item_id);
std::string ResolveLootSlotDisplayName(
    const openwow::game::ItemDefinitions& item_definitions,
    const openwow::data::dbc::DbcLoader* dbc,
    std::uint32_t item_id,
    std::int32_t random_property_id);
std::string ResolveLootSlotIconTexturePath(
    std::uint32_t display_id,
    const openwow::data::dbc::DbcLoader* dbc);
std::uint8_t ResolveLootItemQuality(
                                    const openwow::game::ItemDefinitions& item_definitions,
                                    std::uint32_t item_id,
                                    std::uint8_t fallback = 1);
std::string ResolveLootItemDisplayName(
    const openwow::data::dbc::DbcLoader* dbc,
    std::string_view base_name,
    std::int32_t random_property_id);
std::optional<std::string> TryBuildCachedLootItemLink(
    const openwow::game::ItemDefinitions& item_definitions,
    const openwow::game::ObjectManager& objects,
    const openwow::data::dbc::DbcLoader* dbc,
    std::uint32_t item_id,
    std::int32_t random_property_id,
    std::uint32_t suffix_factor);
std::string BuildLootItemLink(std::uint32_t item_id,
                              std::uint8_t quality,
                              const std::string& display_name,
                              std::int32_t random_property_id,
                              std::uint32_t suffix_factor,
                              std::uint32_t player_level = 0);

struct ItemHyperlinkTemplateView {
  std::string_view name;
  std::uint32_t quality = 1;
};

std::string BuildItemHyperlinkStringFromObject(
    const openwow::game::CGItem_C* item,
    ItemHyperlinkTemplateView item_template,
    const openwow::data::dbc::DbcLoader* dbc,
    std::uint32_t player_level);

}

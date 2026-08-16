#include "openwow/ui/game/loot_tooltip_support.h"

#include "openwow/game/inventory/items/adapters/retail/item_display_name_formatter.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/items/item_icon_resolver.h"
#include "openwow/game/localization.h"
#include "openwow/game/inventory/loot/loot_interaction.h"
#include "openwow/game/objects/cgitem.h"
#include "openwow/game/object_manager.h"

#include <cstdio>

namespace openwow::ui::game::detail {

const ::openwow::game::LootWindow* GetActiveLootWindow(
    const ::openwow::game::LootInteraction& loot) {
  if (!loot.is_looting()) {
    return nullptr;
  }
  return &loot.loot_window();
}

int UISlotToItemIndex(bool has_gold, int ui_slot, std::size_t item_count) {
  if (has_gold) {
    if (ui_slot == 1) {
      return -1;
    }
    const int idx = ui_slot - 2;
    if (idx < 0 || idx >= static_cast<int>(item_count)) {
      return -2;
    }
    return idx;
  }

  const int idx = ui_slot - 1;
  if (idx < 0 || idx >= static_cast<int>(item_count)) {
    return -2;
  }
  return idx;
}

std::optional<LootTooltipItemData> ResolveLootTooltipItem(
    const ::openwow::game::LootInteraction& loot, const int ui_slot) {
  const auto* loot_window = GetActiveLootWindow(loot);
  if (loot_window == nullptr) {
    return std::nullopt;
  }

  const int idx = UISlotToItemIndex(
      loot_window->gold_slot_reserved, ui_slot,
      ::openwow::game::LootInteraction::GetDisplaySlotCount(*loot_window));
  if (idx < 0) {
    return std::nullopt;
  }

  const auto* item = ::openwow::game::LootInteraction::FindItemByDisplayIndex(
      *loot_window, static_cast<std::size_t>(idx));
  if (item == nullptr) {
    return std::nullopt;
  }
  return LootTooltipItemData{
      .item_id = item->item_id,
      .display_id = item->display_info_id,
      .item_count = item->count,
      .suffix_factor = item->random_suffix,
      .random_property_id = static_cast<std::int32_t>(item->random_property_id),
      .quality = 0,
      .is_locked = item->slot_type == ::openwow::game::LootSlotType::kLocked,
  };
}

std::string ResolveLootItemBaseName(
    const ::openwow::game::ItemDefinitions& item_definitions, std::uint32_t item_id) {
  if (const auto* item = item_definitions.GetItem(item_id);
      item != nullptr && !item->name.empty()) {
    return item->name;
  }

  return "Item #" + std::to_string(item_id);
}

int ResolveCachedLootItemQuality(
    const ::openwow::game::ItemDefinitions& item_definitions, std::uint32_t item_id) {
  if (const auto* item = item_definitions.GetItem(item_id);
      item != nullptr) {
    return static_cast<int>(item->quality);
  }

  return -1;
}

std::string ResolveLootSlotDisplayName(
    const ::openwow::game::ItemDefinitions& item_definitions,
    const openwow::data::dbc::DbcLoader* dbc,
    const std::uint32_t item_id,
    const std::int32_t random_property_id) {
  const auto* item = item_definitions.GetItem(item_id);
  if (item == nullptr || item->name.empty()) {
    return {};
  }

  return ::openwow::game::FormatItemDisplayNameWithRandomProperty(
      ::openwow::game::Localization::Get(), dbc, item->name,
      random_property_id);
}

std::string ResolveLootSlotIconTexturePath(
    const std::uint32_t display_id,
    const openwow::data::dbc::DbcLoader* dbc) {
  return ::openwow::game::ResolveItemInventoryIconTexturePath(dbc, display_id);
}

std::uint8_t ResolveLootItemQuality(
                                    const ::openwow::game::ItemDefinitions& item_definitions,
                                    std::uint32_t item_id,
                                    std::uint8_t fallback) {
  if (const auto* item = item_definitions.GetItem(item_id);
      item != nullptr) {
    return static_cast<std::uint8_t>(item->quality);
  }

  return fallback;
}

std::string ResolveLootItemDisplayName(
    const openwow::data::dbc::DbcLoader* dbc,
    const std::string_view base_name,
    std::int32_t random_property_id) {
  return ::openwow::game::FormatItemDisplayNameWithRandomProperty(
      ::openwow::game::Localization::Get(), dbc, base_name,
      random_property_id);
}

std::optional<std::string> TryBuildCachedLootItemLink(
    const ::openwow::game::ItemDefinitions& item_definitions,
    const ::openwow::game::ObjectManager& objects,
    const openwow::data::dbc::DbcLoader* dbc,
    const std::uint32_t item_id,
    const std::int32_t random_property_id,
    const std::uint32_t suffix_factor) {
  const auto* item = item_definitions.GetItem(item_id);
  if (item == nullptr || item->name.empty()) {
    return std::nullopt;
  }

  std::uint32_t player_level = 0;
  if (const auto* player = objects.GetActivePlayer();
      player != nullptr) {
    player_level = player->State().GetLevel();
  }

  return BuildLootItemLink(
      item_id,
      static_cast<std::uint8_t>(item->quality),
      ResolveLootItemDisplayName(dbc, item->name, random_property_id),
      random_property_id,
      suffix_factor,
      player_level);
}

std::string BuildLootItemLink(std::uint32_t item_id,
                              std::uint8_t quality,
                              const std::string& display_name,
                              std::int32_t random_property_id,
                              std::uint32_t suffix_factor,
                              std::uint32_t player_level) {
  const auto color =
      ::openwow::game::ItemTemplate::GetQualityColorInfo(quality).argb_hex;

  char link[1024];
  std::snprintf(link, sizeof(link),
                "|c%s|Hitem:%u:0:0:0:0:0:%d:%u:%u|h[%s]|h|r",
                color,
                item_id,
                random_property_id,
                suffix_factor,
                player_level,
                display_name.c_str());
  return link;
}

std::string BuildItemHyperlinkStringFromObject(
    const ::openwow::game::CGItem_C* item,
    const ItemHyperlinkTemplateView item_template,
    const openwow::data::dbc::DbcLoader* dbc,
    const std::uint32_t player_level) {
  if (item == nullptr) {
    return {};
  }

  const auto entry = item->GetEntry();

  auto quality = item_template.quality;
  if (quality >= 8) {
    quality = 1;
  }

  const bool is_wrapped =
      item->HasItemFlag(::openwow::game::kItemFieldFlagWrapped);

  std::array<std::uint32_t, 3> gem_ids = {};
  std::uint32_t enchant_id = 0;
  std::int32_t random_property_id = 0;
  std::int32_t suffix_factor = 0;

  if (!is_wrapped) {
    gem_ids[0] = item->GetEnchantIdIfVisible(
        ::openwow::game::kEnchantSlotSocket1);
    gem_ids[1] = item->GetEnchantIdIfVisible(
        ::openwow::game::kEnchantSlotSocket2);
    gem_ids[2] = item->GetEnchantIdIfVisible(
        ::openwow::game::kEnchantSlotSocket3);

    enchant_id = item->GetEnchantIdIfVisible(
        ::openwow::game::kEnchantSlotPermanent);
    random_property_id = item->GetRandomPropertiesId();
    suffix_factor = static_cast<std::int32_t>(item->GetPropertySeed());
  }

  std::string display_name;
  if (!item_template.name.empty()) {
    display_name = ResolveLootItemDisplayName(
        dbc, item_template.name, random_property_id);
  }

  const auto* color_str =
      ::openwow::game::ItemTemplate::GetQualityColorInfo(quality)
          .hyperlink_color;

  char link[1024];
  std::snprintf(
      link, sizeof(link),
      "%s|Hitem:%u:%u:%u:%u:%u:%u:%d:%d:%u|h[%s]|h|r",
      color_str, entry, enchant_id, gem_ids[0], gem_ids[1], gem_ids[2], 0u,
      random_property_id, suffix_factor, player_level, display_name.c_str());
  return link;
}

}

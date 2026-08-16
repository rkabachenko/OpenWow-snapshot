#pragma once

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/query_cache.h"

#include <array>
#include <cctype>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace openwow::game {

struct EquippedItemTypeMatchContext {
  const QueryCache* query_cache = nullptr;
  const data::dbc::DbcLoader* dbc = nullptr;
  std::function<std::string(std::string_view)> localize;
};

namespace detail {

inline bool EqualsAsciiNoCase(std::string_view lhs, std::string_view rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }

  for (std::size_t index = 0; index < lhs.size(); ++index) {
    if (std::tolower(static_cast<unsigned char>(lhs[index])) !=
        std::tolower(static_cast<unsigned char>(rhs[index]))) {
      return false;
    }
  }

  return true;
}

inline bool MatchesInventoryTypeName(std::string_view query,
                                     std::string_view inventory_type_key,
                                     const std::function<std::string(
                                         std::string_view)>& localize) {
  if (EqualsAsciiNoCase(query, inventory_type_key)) {
    return true;
  }

  const auto localized_name =
      localize ? localize(inventory_type_key) : std::string{};
  return !localized_name.empty() &&
         EqualsAsciiNoCase(query, localized_name);
}

inline std::optional<std::uint32_t> ResolveInventoryTypeQuery(
    std::string_view query,
    const std::function<std::string(std::string_view)>& localize) {
  static constexpr std::array<std::string_view, 29> kInventoryTypeKeys = {
      "",
      "INVTYPE_HEAD",
      "INVTYPE_NECK",
      "INVTYPE_SHOULDER",
      "INVTYPE_BODY",
      "INVTYPE_CHEST",
      "INVTYPE_WAIST",
      "INVTYPE_LEGS",
      "INVTYPE_FEET",
      "INVTYPE_WRIST",
      "INVTYPE_HAND",
      "INVTYPE_FINGER",
      "INVTYPE_TRINKET",
      "INVTYPE_WEAPON",
      "INVTYPE_SHIELD",
      "INVTYPE_RANGED",
      "INVTYPE_CLOAK",
      "INVTYPE_2HWEAPON",
      "INVTYPE_BAG",
      "INVTYPE_TABARD",
      "INVTYPE_ROBE",
      "INVTYPE_WEAPONMAINHAND",
      "INVTYPE_WEAPONOFFHAND",
      "INVTYPE_HOLDABLE",
      "INVTYPE_AMMO",
      "INVTYPE_THROWN",
      "INVTYPE_RANGEDRIGHT",
      "INVTYPE_QUIVER",
      "INVTYPE_RELIC",
  };

  for (std::uint32_t inventory_type = 1;
       inventory_type < kInventoryTypeKeys.size();
       ++inventory_type) {
    if (MatchesInventoryTypeName(
            query, kInventoryTypeKeys[inventory_type], localize)) {
      return inventory_type;
    }
  }

  return std::nullopt;
}

inline bool MatchesClassOrSubclassQuery(
    std::string_view query,
    std::uint32_t item_class,
    std::uint32_t sub_class,
    const data::dbc::DbcLoader& dbc) {
  for (const auto& class_entry : dbc.item_class().entries()) {
    if (class_entry.id == item_class &&
        EqualsAsciiNoCase(query, class_entry.name)) {
      return true;
    }
  }

  for (const auto& subclass_entry : dbc.item_sub_class().entries()) {
    if (subclass_entry.class_id != item_class ||
        subclass_entry.subclass_id != sub_class) {
      continue;
    }

    if (EqualsAsciiNoCase(query, subclass_entry.display_name) ||
        (!subclass_entry.verbose_name.empty() &&
         EqualsAsciiNoCase(query, subclass_entry.verbose_name))) {
      return true;
    }
  }

  if (sub_class < 32) {
    const auto sub_class_mask = 1u << sub_class;
    for (const auto& mask_entry : dbc.item_sub_class_mask().entries()) {
      if (mask_entry.class_id == item_class &&
          (mask_entry.mask & sub_class_mask) != 0 &&
          EqualsAsciiNoCase(query, mask_entry.name)) {
        return true;
      }
    }
  }

  return false;
}

template <typename Predicate>
inline bool AnyEquippedItemMatches(const PlayerInventoryReplica& inventory,
                                   const Predicate& predicate) {
  for (std::uint8_t slot = 0; slot < PlayerInventoryReplica::kMaxEquipSlots; ++slot) {
    const auto* item = inventory.GetEquipSlot(slot);
    if (item != nullptr && predicate(item->entry)) {
      return true;
    }
  }

  for (std::uint8_t bag = 1; bag <= PlayerInventoryReplica::kMaxBags; ++bag) {
    const auto* bag_info = inventory.GetBag(bag);
    if (bag_info != nullptr && bag_info->entry != 0 &&
        predicate(bag_info->entry)) {
      return true;
    }
  }

  return false;
}

}

inline bool MatchesEquippedItemTypeQuery(
    const PlayerInventoryReplica& inventory,
    std::string_view query,
    const EquippedItemTypeMatchContext& context) {
  if (query.empty() || context.query_cache == nullptr) {
    return false;
  }

  if (const auto inventory_type =
          detail::ResolveInventoryTypeQuery(query, context.localize)) {
    return detail::AnyEquippedItemMatches(inventory, [&](const std::uint32_t entry) {
      const auto* item_template = context.query_cache->GetItemTemplate(entry);
      return item_template != nullptr &&
             static_cast<std::uint32_t>(item_template->inventory_type) ==
                 *inventory_type;
    });
  }

  if (context.dbc == nullptr) {
    return false;
  }

  return detail::AnyEquippedItemMatches(inventory, [&](const std::uint32_t entry) {
    const auto* item_template = context.query_cache->GetItemTemplate(entry);
    return item_template != nullptr &&
           detail::MatchesClassOrSubclassQuery(
               query,
               static_cast<std::uint32_t>(item_template->item_class),
               item_template->subclass,
               *context.dbc);
  });
}

}

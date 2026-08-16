
#pragma once

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/items/item_on_use_spell.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace openwow::game {

inline constexpr std::uint32_t kItemFlagArenaRestrictionBypass = 0x00200000u;
inline constexpr std::int32_t kArenaItemCooldownLimitMs = 900000;

template <typename Visitor>
inline bool VisitDefaultCarriedInventoryItems(
    const PlayerInventoryReplica& inventory, Visitor&& visitor) {
  for (std::uint8_t slot = 0;
       slot < PlayerInventoryReplica::kBackpackSize; ++slot) {
    if (const auto* item = inventory.GetBackpackSlot(slot);
        item != nullptr && visitor(*item)) {
      return true;
    }
  }

  for (std::uint8_t bag = 1;
       bag <= PlayerInventoryReplica::kMaxBags; ++bag) {
    if (const auto* bag_info = inventory.GetBag(bag); bag_info != nullptr) {
      for (std::uint8_t slot = 0; slot < bag_info->num_slots; ++slot) {
        if (const auto* item = inventory.GetBagSlot(bag, slot);
            item != nullptr && visitor(*item)) {
          return true;
        }
      }
    }
  }

  for (std::uint8_t slot = 0;
       slot < PlayerInventoryReplica::kKeyringSlots; ++slot) {
    if (const auto* item = inventory.GetKeyringSlot(slot);
        item != nullptr && visitor(*item)) {
      return true;
    }
  }

  return false;
}

template <typename Predicate>
inline const ItemInstance* FindFirstDefaultCarriedInventoryItem(
    const PlayerInventoryReplica& inventory, Predicate&& predicate) {
  const ItemInstance* match = nullptr;
  VisitDefaultCarriedInventoryItems(
      inventory, [&](const ItemInstance& item) {
        if (!predicate(item)) {
          return false;
        }
        match = &item;
        return true;
      });
  return match;
}

template <typename ItemTemplateT>
inline std::int32_t ResolveActionItemUseSpellCharges(
    const ItemInstance& item,
    const ItemTemplateT& item_template,
    const data::dbc::DbcLoader* dbc = nullptr) {
  if (dbc != nullptr) {
    const auto enchant_match = FindFirstOnUseSpellEnchantment(
        item, [&](const std::uint32_t enchantment_id) {
          return dbc->spell_item_enchantment().LookupEntry(enchantment_id);
        });
    if (enchant_match.has_value()) {
      return static_cast<std::int32_t>(
          item.enchantments[enchant_match->enchantment_slot].charges);
    }
  }

  const auto spell_index = FindFirstOnUseSpellIndex(item_template);
  if (spell_index < 0) {
    return 0;
  }

  const auto charge_index = static_cast<std::size_t>(spell_index);
  return charge_index < item.charges.size() ? item.charges[charge_index] : 0;
}

template <typename ItemTemplateT>
inline std::uint32_t ResolveItemInstanceUseSpellId(
    const ItemInstance& item,
    const ItemTemplateT& item_template,
    const data::dbc::DbcLoader* dbc = nullptr) {
  if (const auto* item_spell = FindFirstOnUseSpell(item_template);
      item_spell != nullptr) {
    return item_spell->spell_id;
  }

  if (dbc == nullptr) {
    return 0;
  }

  const auto enchant_match = FindFirstOnUseSpellEnchantment(
      item, [&](const std::uint32_t enchantment_id) {
        return dbc->spell_item_enchantment().LookupEntry(enchantment_id);
      });
  return enchant_match.has_value() ? enchant_match->spell_id() : 0u;
}

template <typename Visitor>
inline void VisitCarriedInventoryItemsByEntry(const PlayerInventoryReplica& inventory,
                                              const std::uint32_t item_id,
                                              Visitor&& visitor) {
  if (item_id == 0) {
    return;
  }

  for (std::uint8_t slot = 0; slot < PlayerInventoryReplica::kMaxEquipSlots; ++slot) {
    if (const auto* item = inventory.GetEquipSlot(slot);
        item != nullptr && item->entry == item_id) {
      visitor(*item);
    }
  }

  for (std::uint8_t slot = 0; slot < PlayerInventoryReplica::kBackpackSize; ++slot) {
    if (const auto* item = inventory.GetBackpackSlot(slot);
        item != nullptr && item->entry == item_id) {
      visitor(*item);
    }
  }

  for (std::uint8_t bag = 1; bag <= PlayerInventoryReplica::kMaxBags; ++bag) {
    if (const auto* bag_info = inventory.GetBag(bag); bag_info != nullptr) {
      for (std::uint8_t slot = 0; slot < bag_info->num_slots; ++slot) {
        if (const auto* item = inventory.GetBagSlot(bag, slot);
            item != nullptr && item->entry == item_id) {
          visitor(*item);
        }
      }
    }
  }
}

template <typename Visitor>
inline void VisitEquippedInventoryItemsByEntry(const PlayerInventoryReplica& inventory,
                                               const std::uint32_t item_id,
                                               Visitor&& visitor) {
  if (item_id == 0) {
    return;
  }

  for (std::uint8_t slot = 0; slot < PlayerInventoryReplica::kMaxEquipSlots; ++slot) {
    if (const auto* item = inventory.GetEquipSlot(slot);
        item != nullptr && item->entry == item_id) {
      visitor(*item);
    }
  }
}

template <typename Visitor>
inline const ItemInstance* FindFirstMatchingInventoryItemByEntry(
    Visitor&& visitor) {
  const ItemInstance* match = nullptr;
  visitor([&](const ItemInstance& item) {
    if (match == nullptr) {
      match = &item;
    }
  });
  return match;
}

template <typename ItemTemplateT, typename Visitor>
inline const ItemInstance* FindFirstMatchingInventoryItemByEntryWithNonZeroUseCharges(
    const ItemTemplateT& item_template,
    const data::dbc::DbcLoader* dbc,
    Visitor&& visitor) {
  const ItemInstance* match = nullptr;
  visitor([&](const ItemInstance& item) {
    if (match != nullptr) {
      return;
    }
    if (ResolveActionItemUseSpellCharges(item, item_template, dbc) != 0) {
      match = &item;
    }
  });
  return match;
}

template <typename ItemTemplateT>
inline const ItemInstance* FindActionBarInventoryItemByEntry(
    const PlayerInventoryReplica& inventory,
    const std::uint32_t item_id,
    const ItemTemplateT* item_template,
    const data::dbc::DbcLoader* dbc = nullptr) {
  if (item_id == 0 || item_template == nullptr ||
      FindFirstOnUseSpell(*item_template) == nullptr) {
    return nullptr;
  }

  const ItemInstance* first_match = nullptr;
  const ItemInstance* preferred_match = nullptr;
  std::uint32_t preferred_stack_count = 0;

  VisitCarriedInventoryItemsByEntry(inventory, item_id, [&](const ItemInstance& item) {
    if (first_match == nullptr) {
      first_match = &item;
    }

    if (ResolveActionItemUseSpellCharges(item, *item_template, dbc) == 0) {
      return;
    }

    const auto stack_count = std::max(item.count, 1u);
    if (preferred_match == nullptr || stack_count < preferred_stack_count) {
      preferred_match = &item;
      preferred_stack_count = stack_count;
    }
  });

  return preferred_match != nullptr ? preferred_match : first_match;
}

inline const ItemInstance* FindFirstEquippedInventoryItemByEntry(
    const PlayerInventoryReplica& inventory,
    const std::uint32_t item_id) {
  return FindFirstMatchingInventoryItemByEntry([&](auto&& visitor) {
    VisitEquippedInventoryItemsByEntry(inventory, item_id, visitor);
  });
}

template <typename ItemTemplateT>
inline const ItemInstance* FindFirstEquippedInventoryItemByEntryWithNonZeroUseCharges(
    const PlayerInventoryReplica& inventory,
    const std::uint32_t item_id,
    const ItemTemplateT& item_template,
    const data::dbc::DbcLoader* dbc = nullptr) {
  return FindFirstMatchingInventoryItemByEntryWithNonZeroUseCharges(
      item_template, dbc, [&](auto&& visitor) {
        VisitEquippedInventoryItemsByEntry(inventory, item_id, visitor);
      });
}

template <typename ItemTemplateT>
inline const ItemInstance* FindActionBarEquippedInventoryItemByEntry(
    const PlayerInventoryReplica& inventory,
    const std::uint32_t item_id,
    const ItemTemplateT* item_template,
    const data::dbc::DbcLoader* dbc = nullptr) {
  if (item_id == 0 || item_template == nullptr ||
      FindFirstOnUseSpell(*item_template) != nullptr) {
    return nullptr;
  }

  if (const auto* equipped_item =
          FindFirstEquippedInventoryItemByEntryWithNonZeroUseCharges(
              inventory, item_id, *item_template, dbc);
      equipped_item != nullptr) {
    return equipped_item;
  }

  return FindFirstEquippedInventoryItemByEntry(inventory, item_id);
}

template <typename ItemTemplateT>
inline std::uint32_t ResolveItemUseSpellIdWithEquippedFallback(
    const PlayerInventoryReplica& inventory,
    const std::uint32_t item_id,
    const ItemTemplateT* item_template,
    const data::dbc::DbcLoader* dbc = nullptr) {
  if (item_id == 0 || item_template == nullptr) {
    return 0;
  }

  if (const auto* item_spell = FindFirstOnUseSpell(*item_template);
      item_spell != nullptr) {
    return item_spell->spell_id;
  }

  const auto* equipped_item =
      FindActionBarEquippedInventoryItemByEntry(inventory, item_id, item_template, dbc);
  if (equipped_item == nullptr) {
    return 0;
  }

  return ResolveItemInstanceUseSpellId(*equipped_item, *item_template, dbc);
}

template <typename ItemTemplateT>
inline bool ItemPassesScriptArenaUseRestrictions(
    const PlayerInventoryReplica& inventory,
    const std::uint32_t item_id,
    const ItemTemplateT& item_template,
    const data::dbc::DbcLoader* dbc = nullptr) {
  if ((item_template.flags & kItemFlagArenaRestrictionBypass) != 0u) {
    return true;
  }

  if (const auto* item_spell = FindFirstOnUseSpell(item_template);
      item_spell != nullptr) {
    return item_spell->charges >= 0 &&
           static_cast<std::int32_t>(item_spell->cooldown) <=
               kArenaItemCooldownLimitMs &&
           static_cast<std::int32_t>(item_spell->category_cooldown) <=
               kArenaItemCooldownLimitMs;
  }

  const auto* equipped =
      FindActionBarEquippedInventoryItemByEntry(
          inventory, item_id, &item_template, dbc);
  if (equipped == nullptr) {
    return true;
  }

  const auto spell_id =
      ResolveItemInstanceUseSpellId(*equipped, item_template, dbc);
  const auto* spell = dbc != nullptr && spell_id != 0
      ? dbc->spell().LookupEntry(spell_id)
      : nullptr;
  if (spell == nullptr) {
    return true;
  }

  return ResolveActionItemUseSpellCharges(*equipped, item_template, dbc) >= 0 &&
         static_cast<std::int32_t>(spell->recovery_time) <=
             kArenaItemCooldownLimitMs &&
         static_cast<std::int32_t>(spell->category_recovery_time) <=
             kArenaItemCooldownLimitMs;
}

template <typename ItemTemplateT>
inline bool HasAnyCarriedInventoryItemByEntryWithNonZeroUseCharges(
    const PlayerInventoryReplica& inventory,
    const std::uint32_t item_id,
    const ItemTemplateT& item_template,
    const data::dbc::DbcLoader* dbc = nullptr) {
  return FindFirstMatchingInventoryItemByEntryWithNonZeroUseCharges(
             item_template, dbc, [&](auto&& visitor) {
               VisitCarriedInventoryItemsByEntry(inventory, item_id, visitor);
             }) != nullptr;
}

inline std::int32_t ResolveIsEquippedActionUseCharges(
    const PlayerInventoryReplica& inventory,
    const std::uint32_t item_id,
    const ItemTemplate& item_template,
    const data::dbc::DbcLoader* dbc = nullptr) {
  if (const auto* first_on_use_spell = FindFirstOnUseSpell(item_template);
      first_on_use_spell != nullptr) {
    return first_on_use_spell->charges;
  }

  if (const auto* equipped_with_charges =
          FindFirstEquippedInventoryItemByEntryWithNonZeroUseCharges(
              inventory, item_id, item_template, dbc);
      equipped_with_charges != nullptr) {
    return ResolveActionItemUseSpellCharges(
        *equipped_with_charges, item_template, dbc);
  }

  if (const auto* equipped_item =
          FindFirstEquippedInventoryItemByEntry(inventory, item_id);
      equipped_item != nullptr) {
    return ResolveActionItemUseSpellCharges(*equipped_item, item_template, dbc);
  }

  return 0;
}

template <typename ItemTemplateT>
inline std::int32_t CountActionBarItemUseChargesByEntry(
    const PlayerInventoryReplica& inventory,
    const std::uint32_t item_id,
    const ItemTemplateT& item_template,
    const data::dbc::DbcLoader* dbc = nullptr) {
  std::int32_t total = 0;
  VisitCarriedInventoryItemsByEntry(inventory, item_id, [&](const ItemInstance& item) {
    if (ResolveItemInstanceUseSpellId(item, item_template, dbc) == 0) {
      return;
    }

    auto charges = ResolveActionItemUseSpellCharges(item, item_template, dbc);
    if (charges <= 0) {
      charges = -charges;
    }
    total += charges;
  });
  return total;
}

inline const data::dbc::SpellEntry* LookupItemDisplayCountSpell(
    const data::dbc::DbcLoader* dbc,
    const std::uint32_t spell_id) {
  if (dbc == nullptr || spell_id == 0) {
    return nullptr;
  }

  return dbc->spell().LookupEntry(spell_id);
}

inline bool SpellHasDisplayReagentCount(const data::dbc::DbcLoader* dbc,
                                        const std::uint32_t spell_id) {
  const auto* spell = LookupItemDisplayCountSpell(dbc, spell_id);
  if (spell == nullptr) {
    return false;
  }

  for (std::size_t index = 0; index < spell->reagent.size(); ++index) {
    if (spell->reagent[index] != 0 && spell->reagent[index] != -2 &&
        spell->reagent_count[index] > 0) {
      return true;
    }
  }

  return false;
}

inline std::int32_t ComputeSpellReagentCastCount(
                                                 const PlayerInventoryReplica& inventory,
                                                 const data::dbc::DbcLoader* dbc,
                                                 const std::uint32_t spell_id) {
  const auto* spell = LookupItemDisplayCountSpell(dbc, spell_id);
  if (spell == nullptr) {
    return 0;
  }

  std::int32_t cast_count = std::numeric_limits<std::int32_t>::max();
  bool found_reagent = false;

  for (std::size_t index = 0; index < spell->reagent.size(); ++index) {
    const auto reagent = spell->reagent[index];
    const auto reagent_count = spell->reagent_count[index];
    if (reagent == 0 || reagent == -2 || reagent_count == 0) {
      continue;
    }

    found_reagent = true;
    const auto owned = static_cast<std::int32_t>(
        inventory.GetItemCount(static_cast<std::uint32_t>(reagent)));
    cast_count =
        std::min(cast_count, owned / static_cast<std::int32_t>(reagent_count));
  }

  if (!found_reagent || cast_count == std::numeric_limits<std::int32_t>::max()) {
    return 0;
  }

  return std::max(cast_count, 0);
}

template <typename ItemTemplateT>
inline std::int32_t ComputeDisplayedInventoryItemCount(
    const PlayerInventoryReplica& inventory,
    const std::uint32_t item_id,
    const std::uint32_t base_count,
    const ItemTemplateT& item_template,
    const data::dbc::DbcLoader* dbc = nullptr) {
  std::int32_t display_count = static_cast<std::int32_t>(base_count);
  if (base_count == 0 || item_template.stackable > 1) {
    return display_count;
  }

  const auto use_spell_charges =
      ResolveIsEquippedActionUseCharges(inventory, item_id, item_template, dbc);
  if (use_spell_charges < 0) {

    return CountActionBarItemUseChargesByEntry(
        inventory, item_id, item_template, dbc);
  }

  const auto spell_id =
      ResolveItemUseSpellIdWithEquippedFallback(
          inventory, item_id, &item_template, dbc);
  const auto reagent_count =
      ComputeSpellReagentCastCount(inventory, dbc, spell_id);
  if (reagent_count > 0) {
    display_count = reagent_count;
  }

  return display_count;
}

inline bool IsEquippedActionItemByEntry(
    const PlayerInventoryReplica& inventory,
    const std::uint32_t item_id,
    const ItemTemplate* item_template,
    const data::dbc::DbcLoader* dbc = nullptr) {
  if (item_id == 0 || item_template == nullptr || !item_template->IsEquippable()) {
    return false;
  }

  const auto* equipped_item =
      FindFirstEquippedInventoryItemByEntry(inventory, item_id);
  if (equipped_item == nullptr) {
    return false;
  }

  const auto use_spell_charges =
      ResolveIsEquippedActionUseCharges(inventory, item_id, *item_template, dbc);
  if (use_spell_charges == -1 || use_spell_charges == 0) {
    return true;
  }

  if (FindFirstEquippedInventoryItemByEntryWithNonZeroUseCharges(
          inventory, item_id, *item_template, dbc) != nullptr) {
    return true;
  }

  return !HasAnyCarriedInventoryItemByEntryWithNonZeroUseCharges(
      inventory, item_id, *item_template, dbc);
}

inline const ItemInstance* FindActionBarInventoryItemByEntry(
    const PlayerInventoryReplica& inventory,
    const ItemDefinitions& item_definitions,
    const std::uint32_t item_id,
    const data::dbc::DbcLoader* dbc = nullptr) {
  if (item_id == 0) {
    return nullptr;
  }

  const auto* item_template = item_definitions.GetItem(item_id);
  return FindActionBarInventoryItemByEntry(
      inventory, item_id, item_template, dbc);
}

}

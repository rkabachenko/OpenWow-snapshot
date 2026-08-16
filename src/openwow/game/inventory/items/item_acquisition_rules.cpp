#include "openwow/game/inventory/items/item_acquisition_rules.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/player_inventory_replica.h"

namespace openwow::game {
namespace {

template <typename Predicate>
std::uint32_t CountCarried(
    const PlayerInventoryReplica& inventory, Predicate predicate) {
  std::uint32_t count = 0;
  for (std::uint8_t slot = 0;
       slot < PlayerInventoryReplica::kBackpackSize; ++slot) {
    if (const auto* item = inventory.GetBackpackSlot(slot);
        item != nullptr && predicate(*item)) {
      count += item->count;
    }
  }
  for (std::uint8_t bag = 1; bag <= PlayerInventoryReplica::kMaxBags; ++bag) {
    const auto* contents = inventory.GetBag(bag);
    if (contents == nullptr) continue;
    for (std::uint8_t slot = 0; slot < contents->num_slots; ++slot) {
      if (const auto* item = inventory.GetBagSlot(bag, slot);
          item != nullptr && predicate(*item)) {
        count += item->count;
      }
    }
  }
  return count;
}

}

ItemAcquisitionResult EvaluateItemAcquisition(
    const PlayerInventoryReplica& inventory,
    const ItemDefinitions& definitions,
    const data::dbc::DbcLoader* dbc,
    const ItemTemplate& item,
    const std::uint32_t count) {
  if (item.max_count != 0 &&
      CountCarried(inventory, [&item](const ItemInstance& carried) {
        return carried.entry == item.entry;
      }) +
              count >
          item.max_count) {
    return {.failure = ItemAcquisitionFailure::kUniqueItemLimit,
            .limit = item.max_count};
  }

  if (item.item_limit_category == 0 || dbc == nullptr) return {};
  const auto* limit =
      dbc->item_limit_category().LookupEntry(item.item_limit_category);
  if (limit == nullptr) return {};
  const auto carried = CountCarried(
      inventory, [&definitions, &item](const ItemInstance& instance) {
        const auto* definition = definitions.GetItem(instance.entry);
        return definition != nullptr &&
               definition->item_limit_category == item.item_limit_category;
      });
  if (carried + count <= limit->quantity) return {};
  return {
      .failure = ItemAcquisitionFailure::kLimitCategory,
      .limit = limit->quantity,
      .category = std::string(limit->name),
  };
}

}

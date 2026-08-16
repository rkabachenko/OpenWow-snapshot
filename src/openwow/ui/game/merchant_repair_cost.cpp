#include "openwow/ui/game/merchant_repair_cost.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/actions/held_cursor/adapters/platform/cursor_surface.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/gossip_manager.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/reputation_info.h"

#include <cmath>
#include <optional>

namespace openwow::ui::game::detail {
namespace {

std::uint32_t GetMerchantRepairDiscountBasisPoints(
    const ::openwow::data::dbc::DbcLoader* dbc,
    const ::openwow::game::ReputationInfo& reputation,
    const ::openwow::game::CGUnit_C& vendor) {
  if (dbc == nullptr) {
    return 0;
  }

  const auto* faction_template =
      dbc->faction_template().LookupEntry(vendor.State().GetFactionTemplate());
  if (faction_template == nullptr || faction_template->faction == 0) {
    return 0;
  }

  const auto* faction_entry =
      dbc->faction().LookupEntry(faction_template->faction);
  if (faction_entry == nullptr || faction_entry->reputation_list_id < 0) {
    return 0;
  }

  switch (reputation.GetStandingLevel(
      static_cast<std::int32_t>(faction_template->faction))) {
    case 4: return 500;
    case 5: return 1000;
    case 6: return 1500;
    case 7: return 2000;
    default: return 0;
  }
}

int RoundMerchantRepairCost(const float value) {
  return static_cast<int>(
      std::trunc(value <= 0.0f ? value - 0.5f : value + 0.5f));
}

int RoundDiscountedMerchantRepairCost(const float value) {
  if (value < 0.0f) {
    return -RoundDiscountedMerchantRepairCost(-value);
  }

  const auto floored = std::floor(static_cast<double>(value));
  const auto lower = static_cast<int>(floored);
  const auto fraction = static_cast<double>(value) - floored;
  if (fraction < 0.5) {
    return lower;
  }
  if (fraction > 0.5) {
    return lower + 1;
  }

  return (lower & 1) == 0 ? lower : lower + 1;
}

std::optional<std::size_t> ResolveRepairCostIndex(
    const ::openwow::game::ItemTemplate& item_template) {
  if (item_template.item_class == ::openwow::game::ItemClass::Weapon) {
    if (item_template.subclass > 21) {
      return std::nullopt;
    }
    return static_cast<std::size_t>(item_template.subclass);
  }

  if (item_template.item_class == ::openwow::game::ItemClass::Armor) {
    if (item_template.subclass > 8) {
      return std::nullopt;
    }
    return static_cast<std::size_t>(21u + item_template.subclass);
  }

  return std::nullopt;
}

void AccumulateRepairCost(MerchantRepairSummary& summary,
                          const ::openwow::game::QueryCache& items,
                          const ::openwow::game::ObjectManager& objects,
                          const ::openwow::game::ReputationInfo& reputation,
                          const ::openwow::data::dbc::DbcLoader* dbc,
                          const ::openwow::game::CGUnit_C& vendor,
                          const ::openwow::game::ItemInstance* item) {
  if (item == nullptr || item->IsEmpty() || item->max_durability == 0) {
    return;
  }

  if (item->durability < item->max_durability) {
    summary.has_damaged_items = true;
  }

  summary.total_cost += CalculateMerchantRepairCost(
      items, objects, reputation, dbc, vendor, *item);
}

}

const ::openwow::game::CGUnit_C* ResolveMerchantRepairVendor(
    const ::openwow::game::GossipManager& gossip,
    const ::openwow::game::ObjectManager& objects) {
  if (!gossip.merchant().active()) {
    return nullptr;
  }

  const auto vendor_guid = gossip.merchant().snapshot().vendor_guid;
  if (vendor_guid.IsEmpty()) {
    return nullptr;
  }

  const auto* vendor = objects.GetUnit(vendor_guid);
  if (vendor == nullptr ||
      (vendor->State().GetNpcFlags() & ::openwow::game::UNIT_NPC_FLAG_REPAIR) == 0) {
    return nullptr;
  }

  return vendor;
}

std::uint32_t CalculateMerchantRepairCost(
    const ::openwow::game::QueryCache& items,
    const ::openwow::game::ObjectManager& objects,
    const ::openwow::game::ReputationInfo& reputation,
    const ::openwow::data::dbc::DbcLoader* dbc,
    const ::openwow::game::CGUnit_C& vendor,
    const ::openwow::game::ItemInstance& item) {
  if (item.max_durability == 0 || item.durability >= item.max_durability ||
      dbc == nullptr) {
    return 0;
  }

  const auto* item_template = items.GetItemTemplate(item.entry);
  if (item_template == nullptr) {
    return 0;
  }

  const auto repair_cost_index = ResolveRepairCostIndex(*item_template);
  if (!repair_cost_index.has_value()) {
    return 0;
  }

  const auto* quality_entry =
      dbc->durability_quality().LookupEntry(
          static_cast<std::uint32_t>(item_template->quality) * 2u + 1u);
  const auto* cost_entry =
      dbc->durability_costs().LookupEntry(item_template->item_level);
  if (quality_entry == nullptr || cost_entry == nullptr) {
    return 0;
  }

  const auto missing_durability = item.max_durability - item.durability;

  const auto base_repair_cost = static_cast<float>(missing_durability) *
                                quality_entry->quality_mod *
                                static_cast<float>(cost_entry->cost[*repair_cost_index]);
  const auto rounded_base_cost = RoundMerchantRepairCost(base_repair_cost);
  const auto clamped_base_cost =
      rounded_base_cost == 0 ? 1u : static_cast<std::uint32_t>(rounded_base_cost);

  if (objects.GetActivePlayer() == nullptr) {
    return clamped_base_cost;
  }

  const auto price_modifier =
      1.0f -
      static_cast<float>(
          GetMerchantRepairDiscountBasisPoints(dbc, reputation, vendor)) /
      10000.0f;
  return static_cast<std::uint32_t>(RoundDiscountedMerchantRepairCost(
      static_cast<float>(clamped_base_cost) * price_modifier));
}

MerchantRepairSummary CalculateMerchantRepairSummary(
    const ::openwow::game::PlayerInventoryReplica& inventory,
    const ::openwow::game::QueryCache& items,
    const ::openwow::game::ObjectManager& objects,
    const ::openwow::game::ReputationInfo& reputation,
    const ::openwow::data::dbc::DbcLoader* dbc,
    const ::openwow::game::CGUnit_C& vendor) {
  MerchantRepairSummary summary;
  for (std::uint8_t slot = ::openwow::game::InventorySlots::kEquipStart;
       slot < ::openwow::game::InventorySlots::kEquipEnd; ++slot) {
    AccumulateRepairCost(summary, items, objects, reputation, dbc, vendor,
                         inventory.GetItemInSlot(slot));
  }

  for (std::uint8_t slot = ::openwow::game::InventorySlots::kBackpackStart;
       slot < ::openwow::game::InventorySlots::kBackpackEnd; ++slot) {
    AccumulateRepairCost(summary, items, objects, reputation, dbc, vendor,
                         inventory.GetItemInSlot(slot));
  }

  for (std::uint8_t bag_index = 1;
       bag_index <= ::openwow::game::PlayerInventoryReplica::kMaxBags; ++bag_index) {
    const auto slot_count = inventory.GetContainerNumSlots(bag_index);
    for (std::uint8_t slot = 0; slot < slot_count; ++slot) {
      AccumulateRepairCost(summary, items, objects, reputation, dbc, vendor,
                           inventory.GetBagSlot(bag_index, slot));
    }
  }

  return summary;
}

}

#pragma once

#include <cstdint>

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::game {
class CGUnit_C;
class GossipManager;
class ItemDefinitions;
class ObjectManager;
class PlayerInventoryReplica;
class QueryCache;
class ReputationInfo;
struct ItemInstance;
}

namespace openwow::ui::game::detail {

struct MerchantRepairSummary {
  std::uint32_t total_cost = 0;
  bool has_damaged_items = false;
};

[[nodiscard]] const ::openwow::game::CGUnit_C* ResolveMerchantRepairVendor(
    const ::openwow::game::GossipManager& gossip,
    const ::openwow::game::ObjectManager& objects);

[[nodiscard]] std::uint32_t CalculateMerchantRepairCost(
    const ::openwow::game::QueryCache& items,
    const ::openwow::game::ObjectManager& objects,
    const ::openwow::game::ReputationInfo& reputation,
    const ::openwow::data::dbc::DbcLoader* dbc,
    const ::openwow::game::CGUnit_C& vendor,
    const ::openwow::game::ItemInstance& item);

[[nodiscard]] MerchantRepairSummary CalculateMerchantRepairSummary(
    const ::openwow::game::PlayerInventoryReplica& inventory,
    const ::openwow::game::QueryCache& items,
    const ::openwow::game::ObjectManager& objects,
    const ::openwow::game::ReputationInfo& reputation,
    const ::openwow::data::dbc::DbcLoader* dbc,
    const ::openwow::game::CGUnit_C& vendor);

}

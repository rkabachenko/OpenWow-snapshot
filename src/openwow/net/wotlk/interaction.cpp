
#include "openwow/net/wotlk/interaction.h"

namespace openwow::net::wotlk {

NpcInteractRequest BuildNpcInteractRequest(std::uint64_t entity_id) {
  return {.entity_id = entity_id};
}

GossipSelectRequest BuildGossipSelectRequest(std::uint64_t npc_guid,
                                              std::uint32_t menu_id,
                                              std::uint32_t option_id,
                                              const std::string& code) {
  GossipSelectRequest r;
  r.npc_guid = npc_guid;
  r.gossip_menu_id = menu_id;
  r.gossip_option_id = option_id;
  r.code = code;
  return r;
}

TrainerBuySpellRequest BuildTrainerBuySpellRequest(std::uint64_t trainer_guid,
                                                    std::uint32_t spell_id) {
  TrainerBuySpellRequest r;
  r.trainer_guid = trainer_guid;
  r.spell_id = spell_id;
  return r;
}

VendorBuyItemRequest BuildVendorBuyItemRequest(std::uint64_t vendor_guid,
                                                std::uint32_t item_id,
                                                std::uint32_t quantity,
                                                std::uint32_t bag_slot) {
  VendorBuyItemRequest r;
  r.vendor_guid = vendor_guid;
  r.item_id = item_id;
  r.quantity = (quantity > 0) ? quantity : 1;
  r.bag_slot = bag_slot;
  return r;
}

VendorSellItemRequest BuildVendorSellItemRequest(std::uint64_t vendor_guid,
                                                  std::uint64_t item_guid,
                                                  std::uint32_t quantity) {
  VendorSellItemRequest r;
  r.vendor_guid = vendor_guid;
  r.item_guid = item_guid;
  r.quantity = (quantity > 0) ? quantity : 1;
  return r;
}

TaxiNodeQueryRequest BuildTaxiNodeQueryRequest(std::uint64_t unit_guid) {
  return {.unit_guid = unit_guid};
}

TaxiActivateRequest BuildTaxiActivateRequest(std::uint64_t unit_guid,
                                              std::uint32_t path_node) {
  TaxiActivateRequest r;
  r.unit_guid = unit_guid;
  r.path_node = path_node;
  return r;
}

RepairItemRequest BuildRepairItemRequest(std::uint64_t npc_guid,
                                          std::uint64_t item_guid,
                                          bool use_guild) {
  RepairItemRequest r;
  r.npc_guid = npc_guid;
  r.item_guid = item_guid;
  r.use_guild_funds = use_guild;
  return r;
}

SpiritHealerActivateRequest BuildSpiritHealerActivateRequest(
    std::uint64_t npc_guid) {
  return {.npc_guid = npc_guid};
}

BinderActivateRequest BuildBinderActivateRequest(std::uint64_t npc_guid) {
  return {.npc_guid = npc_guid};
}

const char* GetInteractionTypeName(InteractionType type) {
  switch (type) {
    case InteractionType::Gossip:       return "Gossip";
    case InteractionType::Vendor:       return "Vendor";
    case InteractionType::Trainer:      return "Trainer";
    case InteractionType::Banker:       return "Banker";
    case InteractionType::TaxiNode:     return "TaxiNode";
    case InteractionType::Innkeeper:    return "Innkeeper";
    case InteractionType::SpiritHealer: return "SpiritHealer";
    case InteractionType::Repair:       return "Repair";
    case InteractionType::Mailbox:      return "Mailbox";
    case InteractionType::Auctioneer:   return "Auctioneer";
    case InteractionType::StableMaster: return "StableMaster";
    case InteractionType::BattleMaster: return "BattleMaster";
    case InteractionType::GuildBanker:  return "GuildBanker";
  }
  return "Unknown";
}

}

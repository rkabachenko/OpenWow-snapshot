#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace openwow::net::wotlk {

enum class InteractionType : std::uint8_t {
  Gossip       = 0,
  Vendor       = 1,
  Trainer      = 2,
  Banker       = 3,
  TaxiNode     = 4,
  Innkeeper    = 5,
  SpiritHealer = 6,
  Repair       = 7,
  Mailbox      = 8,
  Auctioneer   = 9,
  StableMaster = 10,
  BattleMaster = 11,
  GuildBanker  = 12,
};

struct NpcInteractRequest {
  std::uint64_t entity_id{0};
};

struct GossipSelectRequest {
  std::uint64_t npc_guid{0};
  std::uint32_t gossip_menu_id{0};
  std::uint32_t gossip_option_id{0};
  std::string   code;
};

struct TrainerBuySpellRequest {
  std::uint64_t trainer_guid{0};
  std::uint32_t spell_id{0};
};

struct VendorBuyItemRequest {
  std::uint64_t vendor_guid{0};
  std::uint32_t item_id{0};
  std::uint32_t quantity{1};
  std::uint32_t bag_slot{0};
};

struct VendorSellItemRequest {
  std::uint64_t vendor_guid{0};
  std::uint64_t item_guid{0};
  std::uint32_t quantity{1};
};

struct TaxiNodeQueryRequest {
  std::uint64_t unit_guid{0};
};

struct TaxiActivateRequest {
  std::uint64_t unit_guid{0};
  std::uint32_t path_node{0};
};

struct RepairItemRequest {
  std::uint64_t npc_guid{0};
  std::uint64_t item_guid{0};
  bool use_guild_funds{false};
};

struct SpiritHealerActivateRequest {
  std::uint64_t npc_guid{0};
};

struct BinderActivateRequest {
  std::uint64_t npc_guid{0};
};

NpcInteractRequest           BuildNpcInteractRequest(std::uint64_t entity_id);
GossipSelectRequest          BuildGossipSelectRequest(std::uint64_t npc_guid,
                                                      std::uint32_t menu_id,
                                                      std::uint32_t option_id,
                                                      const std::string& code = "");
TrainerBuySpellRequest       BuildTrainerBuySpellRequest(std::uint64_t trainer_guid,
                                                         std::uint32_t spell_id);
VendorBuyItemRequest         BuildVendorBuyItemRequest(std::uint64_t vendor_guid,
                                                       std::uint32_t item_id,
                                                       std::uint32_t quantity = 1,
                                                       std::uint32_t bag_slot = 0);
VendorSellItemRequest        BuildVendorSellItemRequest(std::uint64_t vendor_guid,
                                                        std::uint64_t item_guid,
                                                        std::uint32_t quantity = 1);
TaxiNodeQueryRequest         BuildTaxiNodeQueryRequest(std::uint64_t unit_guid);
TaxiActivateRequest          BuildTaxiActivateRequest(std::uint64_t unit_guid,
                                                      std::uint32_t path_node);
RepairItemRequest            BuildRepairItemRequest(std::uint64_t npc_guid,
                                                    std::uint64_t item_guid = 0,
                                                    bool use_guild = false);
SpiritHealerActivateRequest  BuildSpiritHealerActivateRequest(std::uint64_t npc_guid);
BinderActivateRequest        BuildBinderActivateRequest(std::uint64_t npc_guid);

[[nodiscard]] const char* GetInteractionTypeName(InteractionType type);

}

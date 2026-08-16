#pragma once

#include <cstdint>
#include <functional>

namespace openwow::net::wotlk {
struct WorldPacket;
}

namespace openwow::game {

class MerchantInteraction;
class ObjectManager;
class GossipManager;
class InteractionSender;
class PetitionHandler;
class QueryCache;

void HandleMerchantBuyPacket(MerchantInteraction& merchant,
                             const net::wotlk::WorldPacket& packet);
void HandleMerchantSellPacket(ObjectManager& objects,
                              const net::wotlk::WorldPacket& packet);
void HandleMerchantBuyFailurePacket(
    MerchantInteraction& merchant,
    const net::wotlk::WorldPacket& packet);
void HandleMerchantListPacket(ObjectManager& objects, GossipManager& gossip,
                              QueryCache& queries,
                              const net::wotlk::WorldPacket& packet);
void HandleGossipMessagePacket(
    GossipManager& gossip, QueryCache& queries, InteractionSender& sender,
    const std::function<void(std::uint64_t)>& close_interaction,
    const std::function<bool()>& prepare_gossip_text,
    const net::wotlk::WorldPacket& packet);
void HandleNpcTextUpdatePacket(
    GossipManager& gossip, QueryCache& queries,
    const std::function<bool()>& prepare_gossip_text,
    const net::wotlk::WorldPacket& packet);
void HandleTrainerListPacket(
    GossipManager& gossip, const std::function<void()>& update_greeting,
    const net::wotlk::WorldPacket& packet);
void HandleTrainerBuySucceededPacket(
    PetitionHandler& petition, const net::wotlk::WorldPacket& packet);
void HandleTrainerBuyFailedPacket(
    PetitionHandler& petition, const net::wotlk::WorldPacket& packet);

}

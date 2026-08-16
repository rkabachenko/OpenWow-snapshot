#pragma once

#include <functional>

namespace openwow::net::wotlk {
struct WorldPacket;
}

namespace openwow::game {

class MailInteraction;
class QueryCache;
class SocialManager;

using MailPacketSender =
    std::function<bool(const net::wotlk::WorldPacket&)>;

void FlushMailProtocolUpdates(MailInteraction& mail,
                              const MailPacketSender& send);
void HandleMailListPacket(MailInteraction& mail, SocialManager& social,
                          QueryCache& queries,
                          const MailPacketSender& send,
                          const net::wotlk::WorldPacket& packet);
void HandleSendMailResultPacket(MailInteraction& mail,
                                bool local_player_exists,
                                const MailPacketSender& send,
                                const net::wotlk::WorldPacket& packet);
void HandleReceivedMailPacket(MailInteraction& mail,
                              const MailPacketSender& send,
                              const net::wotlk::WorldPacket& packet);
void HandleShowMailboxPacket(MailInteraction& mail,
                             const net::wotlk::WorldPacket& packet);
void HandleNextMailTimePacket(MailInteraction& mail,
                              const MailPacketSender& send,
                              const net::wotlk::WorldPacket& packet);

}

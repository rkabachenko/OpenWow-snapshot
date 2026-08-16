#pragma once

namespace openwow::net::wotlk {
struct WorldPacket;
}

namespace openwow::game {

namespace actions::held_cursor {
class HeldCursor;
}
class InteractionSender;
class ObjectManager;
class PlayerInventoryReplica;
class QueryCache;
class SocialManager;
class TradeInteraction;

void HandleTradeStatusPacket(
                             TradeInteraction& trade,
                             InteractionSender& interaction,
                             const SocialManager& social,
                             const PlayerInventoryReplica& inventory,
                             actions::held_cursor::HeldCursor* held_cursor,
                             const ObjectManager& objects,
                             const QueryCache& queries,
                             bool cinematic_active,
                             bool has_blocking_interaction,
                             const net::wotlk::WorldPacket& packet);
void HandleTradeExtendedPacket(TradeInteraction& trade,
                               InteractionSender& interaction,
                               const net::wotlk::WorldPacket& packet);

}

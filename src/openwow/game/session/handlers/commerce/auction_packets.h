#pragma once

#include "openwow/game/commerce/auctions/auction_state.h"

#include <cstdint>
#include <functional>
#include <unordered_map>

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::net::wotlk {
struct WorldPacket;
}

namespace openwow::game {

class AuctionInteraction;
class InteractionSender;
class MailInteraction;
class QueryCache;

class AuctionPacketHandler {
 public:
  using SendPacket =
      std::function<bool(const net::wotlk::WorldPacket&)>;

  AuctionPacketHandler(AuctionInteraction& auction, MailInteraction& mail,
                       QueryCache& queries, InteractionSender& interaction,
                       SendPacket send);

  void SetDbcLoader(const data::dbc::DbcLoader* dbc);
  bool TrySendOwnerRefresh();
  void RequestListRefreshOnNameResolve(std::uint64_t guid,
                                       AuctionSelectionList list);
  void ResolvePendingNameQuery(std::uint64_t guid, bool name_cache_updated);
  void Clear();

  void HandleHello(const net::wotlk::WorldPacket& packet);
  void HandleListResult(const net::wotlk::WorldPacket& packet);
  void HandleOwnerListResult(const net::wotlk::WorldPacket& packet);
  void HandleBidderListResult(const net::wotlk::WorldPacket& packet);
  void HandleCommandResult(const net::wotlk::WorldPacket& packet);
  void HandleBidderNotification(const net::wotlk::WorldPacket& packet);
  void HandleOwnerNotification(const net::wotlk::WorldPacket& packet);
  void HandleListPendingSales(const net::wotlk::WorldPacket& packet);
  void HandleRemovedNotification(const net::wotlk::WorldPacket& packet);

 private:
  AuctionInteraction& auction_;
  MailInteraction& mail_;
  QueryCache& queries_;
  InteractionSender& interaction_;
  const data::dbc::DbcLoader* dbc_{nullptr};
  SendPacket send_;
  std::unordered_map<std::uint64_t, std::uint8_t> pending_name_queries_;
};

}

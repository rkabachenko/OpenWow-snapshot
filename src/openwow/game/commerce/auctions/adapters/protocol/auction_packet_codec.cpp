#include "openwow/game/commerce/auctions/adapters/protocol/auction_packet_codec.h"

#include "openwow/game/packet_reader.h"
#include "openwow/network/protocol/wotlk/opcodes.h"
#include "openwow/runtime/time/game_clock.h"

#include <algorithm>
#include <cstdlib>

namespace openwow::game::auction_protocol {

std::optional<AuctionHelloPayload> DecodeHello(const std::uint8_t* data,
                                               const std::size_t size) {
  PacketReader reader(data, size);
  AuctionHelloPayload hello;
  std::uint8_t enabled = 0;
  if (!reader.ReadU64(hello.auctioneer_guid) ||
      !reader.ReadU32(hello.auction_house_id) || !reader.ReadU8(enabled)) {
    return std::nullopt;
  }
  hello.enabled = enabled != 0;
  return hello;
}

std::optional<AuctionListResult> DecodeList(const std::uint8_t* data,
                                            const std::size_t size) {
  PacketReader reader(data, size);
  std::uint32_t count = 0;
  if (!reader.ReadU32(count)) {
    return std::nullopt;
  }
  AuctionListResult result;
  result.received_at_tick_ms = core::GameClock::GetTickCount32();
  result.auctions.reserve(count);
  for (std::uint32_t index = 0; index < count; ++index) {
    AuctionEntry entry;
    if (!reader.ReadU32(entry.auction_id) ||
        !reader.ReadU32(entry.item_entry)) {
      return std::nullopt;
    }
    for (AuctionEnchant& enchant : entry.enchants) {
      if (!reader.ReadU32(enchant.id) ||
          !reader.ReadU32(enchant.duration) ||
          !reader.ReadU32(enchant.charges)) {
        return std::nullopt;
      }
    }
    if (!reader.ReadI32(entry.random_property_id) ||
        !reader.ReadU32(entry.suffix_factor) ||
        !reader.ReadU32(entry.stack_count) ||
        !reader.ReadU32(entry.spell_charges) ||
        !reader.ReadU32(entry.unk_flags) ||
        !reader.ReadU64(entry.owner_guid) ||
        !reader.ReadU32(entry.start_bid) ||
        !reader.ReadU32(entry.minimum_outbid) ||
        !reader.ReadU32(entry.buyout) ||
        !reader.ReadU32(entry.time_left_ms) ||
        !reader.ReadU64(entry.bidder_guid) ||
        !reader.ReadU32(entry.current_bid)) {
      return std::nullopt;
    }
    entry.expiration_tick_ms =
        result.received_at_tick_ms + entry.time_left_ms;
    result.auctions.push_back(entry);
  }
  if (!reader.ReadU32(result.total_count) ||
      !reader.ReadU32(result.search_delay)) {
    return std::nullopt;
  }
  return result;
}

std::optional<AuctionCommandResult> DecodeCommandResult(
    const std::uint8_t* data, const std::size_t size) {
  PacketReader reader(data, size);
  AuctionCommandResult result;
  std::uint32_t action = 0;
  if (!reader.ReadU32(result.auction_id) || !reader.ReadU32(action) ||
      !reader.ReadU32(result.error_code)) {
    return std::nullopt;
  }
  result.action = static_cast<AuctionAction>(action);
  if (result.error_code == 1) {
    if (!reader.ReadU32(result.bid_error)) {
      return std::nullopt;
    }
  } else if (result.error_code == 5) {
    ObjectGuid bidder;
    if (!reader.ReadPackedGuid(bidder) ||
        !reader.ReadU32(result.competing_bid) ||
        !reader.ReadU32(result.minimum_increment)) {
      return std::nullopt;
    }
    result.competing_bidder_guid = bidder.GetRawValue();
  } else if (result.error_code == 0 &&
             result.action == AuctionAction::kPlaceBid &&
             !reader.ReadU32(result.minimum_increment)) {
    return std::nullopt;
  }
  return result;
}

std::optional<AuctionBidderNotification> DecodeBidderNotification(
    const std::uint8_t* data, const std::size_t size) {
  PacketReader reader(data, size);
  AuctionBidderNotification notification;
  if (!reader.ReadU32(notification.auction_house_id) ||
      !reader.ReadU32(notification.auction_id) ||
      !reader.ReadU64(notification.bidder_guid) ||
      !reader.ReadU32(notification.current_bid) ||
      !reader.ReadU32(notification.time_left_ms) ||
      !reader.ReadU32(notification.item_template) ||
      !reader.ReadI32(notification.random_property_id)) {
    return std::nullopt;
  }
  return notification;
}

std::optional<AuctionOwnerNotification> DecodeOwnerNotification(
    const std::uint8_t* data, const std::size_t size) {
  PacketReader reader(data, size);
  AuctionOwnerNotification notification;
  if (!reader.ReadU32(notification.auction_id) ||
      !reader.ReadU32(notification.bid) ||
      !reader.ReadU32(notification.time_left_ms) ||
      !reader.ReadU64(notification.bidder_guid) ||
      !reader.ReadU32(notification.item_template) ||
      !reader.ReadI32(notification.random_property_id) ||
      !reader.ReadFloat(notification.money)) {
    return std::nullopt;
  }
  return notification;
}

std::optional<std::vector<PendingSaleEntry>> DecodePendingSales(
    const std::uint8_t* data, const std::size_t size) {
  PacketReader reader(data, size);
  std::uint32_t count = 0;
  if (!reader.ReadU32(count)) {
    return std::vector<PendingSaleEntry>{};
  }
  std::vector<PendingSaleEntry> sales;
  for (std::uint32_t index = 0; index < count; ++index) {
    PendingSaleEntry entry;
    if (!reader.ReadCString(entry.item_key) ||
        !reader.ReadCString(entry.full_item_desc) ||
        !reader.ReadU32(entry.amount) || !reader.ReadU32(entry.count) ||
        !reader.ReadFloat(entry.time_remaining_days)) {
      break;
    }
    const char* cursor = entry.item_key.c_str();
    char* end = nullptr;
    entry.parsed_item_id =
        static_cast<std::uint32_t>(std::strtoul(cursor, &end, 10));
    if (end && *end == ':') {
      cursor = end + 1;
      entry.parsed_stack_count =
          static_cast<std::uint32_t>(std::strtoul(cursor, &end, 10));
    }
    if (end && *end == ':') {
      cursor = end + 1;
      entry.parsed_type =
          static_cast<std::uint32_t>(std::strtoul(cursor, &end, 10));
    }
    if (end && *end == ':') {
      cursor = end + 1;
      entry.parsed_auction_id =
          static_cast<std::uint32_t>(std::strtoul(cursor, &end, 10));
    }
    if (entry.parsed_type != 2) {
      continue;
    }
    cursor = entry.full_item_desc.c_str();
    entry.bidder_guid = std::strtoull(cursor, &end, 16);
    if (end && *end == ':') {
      cursor = end + 1;
      entry.current_bid =
          static_cast<std::uint32_t>(std::strtoul(cursor, &end, 10));
    }
    if (end && *end == ':') {
      cursor = end + 1;
      entry.buyout =
          static_cast<std::uint32_t>(std::strtoul(cursor, &end, 10));
    }
    sales.push_back(std::move(entry));
  }
  return sales;
}

std::optional<AuctionRemovedNotification> DecodeRemovedNotification(
    const std::uint8_t* data, const std::size_t size) {
  PacketReader reader(data, size);
  AuctionRemovedNotification notification;
  if (!reader.ReadU32(notification.auction_id) ||
      !reader.ReadU32(notification.item_template) ||
      !reader.ReadU32(notification.random_property_id)) {
    return std::nullopt;
  }
  return notification;
}

net::wotlk::WorldPacket EncodeHello(const std::uint64_t auctioneer) {
  net::wotlk::WorldPacket packet(net::wotlk::Opcode::MSG_AUCTION_HELLO);
  packet.AppendU64(auctioneer);
  return packet;
}

net::wotlk::WorldPacket EncodeSellItem(
    const std::uint64_t auctioneer,
    const std::vector<std::pair<std::uint64_t, std::uint32_t>>& items,
    const std::uint32_t bid, const std::uint32_t buyout,
    const std::uint32_t duration) {
  net::wotlk::WorldPacket packet(net::wotlk::Opcode::CMSG_AUCTION_SELL_ITEM);
  packet.AppendU64(auctioneer);
  packet.AppendU32(static_cast<std::uint32_t>(items.size()));
  for (const auto& [guid, count] : items) {
    packet.AppendU64(guid);
    packet.AppendU32(count);
  }
  packet.AppendU32(bid);
  packet.AppendU32(buyout);
  packet.AppendU32(duration);
  return packet;
}

net::wotlk::WorldPacket EncodePlaceBid(const std::uint64_t auctioneer,
                                       const std::uint32_t auction_id,
                                       const std::uint32_t price) {
  net::wotlk::WorldPacket packet(net::wotlk::Opcode::CMSG_AUCTION_PLACE_BID);
  packet.AppendU64(auctioneer);
  packet.AppendU32(auction_id);
  packet.AppendU32(price);
  return packet;
}

net::wotlk::WorldPacket EncodeRemoveItem(const std::uint64_t auctioneer,
                                         const std::uint32_t auction_id) {
  net::wotlk::WorldPacket packet(net::wotlk::Opcode::CMSG_AUCTION_REMOVE_ITEM);
  packet.AppendU64(auctioneer);
  packet.AppendU32(auction_id);
  return packet;
}

net::wotlk::WorldPacket EncodeListItems(
    const std::uint64_t auctioneer, const std::uint32_t list_from,
    const std::string_view search_name, const std::uint8_t level_min,
    const std::uint8_t level_max, const std::uint32_t slot_id,
    const std::uint32_t main_category, const std::uint32_t sub_category,
    const std::uint32_t quality, const std::uint8_t usable,
    const std::uint8_t get_all,
    const std::vector<AuctionSortCriteria>& sort) {
  net::wotlk::WorldPacket packet(net::wotlk::Opcode::CMSG_AUCTION_LIST_ITEMS);
  packet.AppendU64(auctioneer);
  packet.AppendU32(list_from);
  packet.AppendString(search_name);
  packet.AppendU8(level_min);
  packet.AppendU8(level_max);
  packet.AppendU32(slot_id);
  packet.AppendU32(main_category);
  packet.AppendU32(sub_category);
  packet.AppendU32(quality);
  packet.AppendU8(usable);
  packet.AppendU8(get_all);
  const auto sort_count =
      static_cast<std::uint8_t>(std::min<std::size_t>(sort.size(), 12));
  packet.AppendU8(sort_count);
  for (std::size_t index = 0; index < sort_count; ++index) {
    const AuctionSortCriteria& criterion = sort[index];
    packet.AppendU8(criterion.sort_mode);
    packet.AppendU8(criterion.is_desc);
  }
  return packet;
}

net::wotlk::WorldPacket EncodeListOwnerItems(const std::uint64_t auctioneer,
                                             const std::uint32_t list_from) {
  net::wotlk::WorldPacket packet(
      net::wotlk::Opcode::CMSG_AUCTION_LIST_OWNER_ITEMS);
  packet.AppendU64(auctioneer);
  packet.AppendU32(list_from);
  return packet;
}

net::wotlk::WorldPacket EncodeListPendingSales(
    const std::uint64_t auctioneer) {
  net::wotlk::WorldPacket packet(
      net::wotlk::Opcode::CMSG_AUCTION_LIST_PENDING_SALES);
  packet.AppendU64(auctioneer);
  return packet;
}

net::wotlk::WorldPacket EncodeListBidderItems(
    const std::uint64_t auctioneer, const std::uint32_t list_from,
    const std::vector<std::uint32_t>& outbid_ids) {
  net::wotlk::WorldPacket packet(
      net::wotlk::Opcode::CMSG_AUCTION_LIST_BIDDER_ITEMS);
  packet.AppendU64(auctioneer);
  packet.AppendU32(list_from);
  packet.AppendU32(static_cast<std::uint32_t>(outbid_ids.size()));
  for (const std::uint32_t id : outbid_ids) {
    packet.AppendU32(id);
  }
  return packet;
}

}

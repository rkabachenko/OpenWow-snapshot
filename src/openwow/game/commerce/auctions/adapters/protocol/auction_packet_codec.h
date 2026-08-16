#pragma once

#include "openwow/game/commerce/auctions/auction_interaction.h"
#include "openwow/network/protocol/wotlk/world_packet.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace openwow::game::auction_protocol {

[[nodiscard]] std::optional<AuctionHelloPayload> DecodeHello(
    const std::uint8_t* data, std::size_t size);
[[nodiscard]] std::optional<AuctionListResult> DecodeList(
    const std::uint8_t* data, std::size_t size);
[[nodiscard]] std::optional<AuctionCommandResult> DecodeCommandResult(
    const std::uint8_t* data, std::size_t size);
[[nodiscard]] std::optional<AuctionBidderNotification>
DecodeBidderNotification(const std::uint8_t* data, std::size_t size);
[[nodiscard]] std::optional<AuctionOwnerNotification> DecodeOwnerNotification(
    const std::uint8_t* data, std::size_t size);
[[nodiscard]] std::optional<std::vector<PendingSaleEntry>> DecodePendingSales(
    const std::uint8_t* data, std::size_t size);
[[nodiscard]] std::optional<AuctionRemovedNotification>
DecodeRemovedNotification(const std::uint8_t* data, std::size_t size);

[[nodiscard]] net::wotlk::WorldPacket EncodeHello(std::uint64_t auctioneer);
[[nodiscard]] net::wotlk::WorldPacket EncodeSellItem(
    std::uint64_t auctioneer,
    const std::vector<std::pair<std::uint64_t, std::uint32_t>>& items,
    std::uint32_t bid, std::uint32_t buyout, std::uint32_t duration);
[[nodiscard]] net::wotlk::WorldPacket EncodePlaceBid(
    std::uint64_t auctioneer, std::uint32_t auction_id, std::uint32_t price);
[[nodiscard]] net::wotlk::WorldPacket EncodeRemoveItem(
    std::uint64_t auctioneer, std::uint32_t auction_id);
[[nodiscard]] net::wotlk::WorldPacket EncodeListItems(
    std::uint64_t auctioneer, std::uint32_t list_from,
    std::string_view search_name, std::uint8_t level_min,
    std::uint8_t level_max, std::uint32_t slot_id,
    std::uint32_t main_category, std::uint32_t sub_category,
    std::uint32_t quality, std::uint8_t usable, std::uint8_t get_all,
    const std::vector<AuctionSortCriteria>& sort);
[[nodiscard]] net::wotlk::WorldPacket EncodeListOwnerItems(
    std::uint64_t auctioneer, std::uint32_t list_from);
[[nodiscard]] net::wotlk::WorldPacket EncodeListPendingSales(
    std::uint64_t auctioneer);
[[nodiscard]] net::wotlk::WorldPacket EncodeListBidderItems(
    std::uint64_t auctioneer, std::uint32_t list_from,
    const std::vector<std::uint32_t>& outbid_ids);

}

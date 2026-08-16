#pragma once

#include "openwow/game/commerce/trade/trade_interaction.h"
#include "openwow/network/protocol/wotlk/world_packet.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace openwow::game::trade_protocol {

[[nodiscard]] std::optional<TradeStatusMessage> DecodeStatus(
    const std::uint8_t* data, std::size_t size);
[[nodiscard]] std::optional<TradeExtendedPrefix> DecodeExtendedPrefix(
    const std::uint8_t* data, std::size_t size);
[[nodiscard]] std::optional<TradeExtendedSnapshot> DecodeExtendedSnapshot(
    const TradeExtendedPrefix& prefix, const std::uint8_t* body,
    std::size_t body_size);

[[nodiscard]] net::wotlk::WorldPacket EncodeInitiate(std::uint64_t target_guid);
[[nodiscard]] net::wotlk::WorldPacket EncodeBegin();
[[nodiscard]] net::wotlk::WorldPacket EncodeSetItem(
    std::uint8_t trade_slot, std::uint8_t bag, std::uint8_t bag_slot);
[[nodiscard]] net::wotlk::WorldPacket EncodeClearItem(
    std::uint8_t trade_slot);
[[nodiscard]] net::wotlk::WorldPacket EncodeSetGold(std::uint32_t copper);
[[nodiscard]] net::wotlk::WorldPacket EncodeAccept(
    std::uint32_t state_index);
[[nodiscard]] net::wotlk::WorldPacket EncodeUnaccept();
[[nodiscard]] net::wotlk::WorldPacket EncodeCancel();
[[nodiscard]] net::wotlk::WorldPacket EncodeBusy();
[[nodiscard]] net::wotlk::WorldPacket EncodeIgnore();

}

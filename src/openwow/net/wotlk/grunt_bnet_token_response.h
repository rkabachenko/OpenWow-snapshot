#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "openwow/net/wotlk/grunt_packet_io.h"

namespace openwow::net::wotlk {

inline constexpr std::uint8_t kBNetTokenResponseOpcode = 0x11;

using AuthPacketSendFn =
    std::function<bool(const std::vector<std::uint8_t>&)>;

using TickCountFn = std::function<std::uint32_t()>;

using StoreTickFn = std::function<void(std::uint32_t tick)>;

PacketHandlerResult HandleBNetTokenResponse(
    const std::uint8_t* data,
    std::size_t size,
    std::size_t& read_pos,
    const AuthPacketSendFn& send_fn);

PacketHandlerResult HandleBNetKeepAlive(
    const std::uint8_t* data,
    std::size_t size,
    std::size_t& read_pos,
    const StoreTickFn& store_tick,
    const TickCountFn& get_tick);

}

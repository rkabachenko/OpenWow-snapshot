#pragma once

#include <cstdint>
#include <functional>

#include "openwow/net/wotlk/grunt_packet_io.h"

namespace openwow::net::wotlk {

inline constexpr std::size_t kBNetKickMinBytes = 9;

using KickAccountFn = std::function<void(std::uint32_t account_id,
                                         std::uint64_t account_guid,
                                         std::uint8_t reason)>;

PacketHandlerResult HandleBNetKickMessage(
    const std::uint8_t* data,
    std::size_t size,
    std::size_t& read_pos,
    const KickAccountFn& kick_fn);

}

#pragma once

#include <cstdint>
#include <functional>

#include "openwow/net/wotlk/grunt_packet_io.h"

namespace openwow::net::wotlk {

using RealmListReceiverFn =
    std::function<void(const std::uint8_t* data,
                       std::size_t size,
                       std::size_t& read_pos)>;

PacketHandlerResult HandleRealmList(
    const std::uint8_t* data,
    std::size_t size,
    std::size_t& read_pos,
    const RealmListReceiverFn& receiver);

}

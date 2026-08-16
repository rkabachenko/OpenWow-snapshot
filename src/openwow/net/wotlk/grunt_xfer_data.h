#pragma once

#include <cstdint>
#include <functional>

#include "openwow/net/wotlk/grunt_packet_io.h"

namespace openwow::net::wotlk {

using XferDataReceiverFn =
    std::function<void(const std::uint8_t* chunk_data,
                       std::uint16_t chunk_length)>;

PacketHandlerResult HandleXferData(
    const std::uint8_t* data,
    std::size_t size,
    std::size_t& read_pos,
    const XferDataReceiverFn& receiver);

}

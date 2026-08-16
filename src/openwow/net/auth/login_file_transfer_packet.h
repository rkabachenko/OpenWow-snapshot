#pragma once

#include <cstdint>
#include <vector>

namespace openwow::net::auth {

[[nodiscard]] std::vector<std::uint8_t> BuildLoginFileTransferResponsePacket(
    bool transfer_needed,
    std::uint64_t resume_offset);

}

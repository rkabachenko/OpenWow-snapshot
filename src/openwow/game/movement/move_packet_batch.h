#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace openwow::game::movement {

struct BatchedMovePacket {
  std::uint16_t opcode{0};
  std::vector<std::uint8_t> payload;
};

bool DecodeMovePacketBatch(const std::uint8_t* data, std::size_t len,
                           std::vector<BatchedMovePacket> &out);

bool DecodeCompressedMovePacketBatch(
    const std::uint8_t* data, std::size_t len,
    std::vector<BatchedMovePacket> &out);

}

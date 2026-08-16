#include "openwow/game/movement/move_packet_batch.h"

#include <zlib.h>

#include <utility>

namespace openwow::game::movement {

namespace {

std::uint32_t ReadU32(const std::uint8_t* bytes) {
  return static_cast<std::uint32_t>(bytes[0]) |
         static_cast<std::uint32_t>(bytes[1]) << 8u |
         static_cast<std::uint32_t>(bytes[2]) << 16u |
         static_cast<std::uint32_t>(bytes[3]) << 24u;
}

bool DecodePacketStream(const std::uint8_t* data, const std::size_t len,
                        std::vector<BatchedMovePacket> &out) {
  std::vector<BatchedMovePacket> decoded;
  std::size_t offset = 0;
  while (offset < len) {
    const std::uint8_t packet_size = data[offset++];
    if (packet_size < sizeof(std::uint16_t) ||
        packet_size > len - offset) {
      return false;
    }

    BatchedMovePacket packet;
    packet.opcode = static_cast<std::uint16_t>(data[offset]) |
                    static_cast<std::uint16_t>(data[offset + 1u]) << 8u;
    packet.payload.assign(data + offset + sizeof(std::uint16_t),
                          data + offset + packet_size);
    decoded.push_back(std::move(packet));
    offset += packet_size;
  }

  out = std::move(decoded);
  return true;
}

}

bool DecodeMovePacketBatch(const std::uint8_t* data, const std::size_t len,
                           std::vector<BatchedMovePacket> &out) {
  if (data == nullptr || len < sizeof(std::uint32_t)) {
    return false;
  }

  static_cast<void>(ReadU32(data));
  return DecodePacketStream(data + sizeof(std::uint32_t),
                            len - sizeof(std::uint32_t), out);
}

bool DecodeCompressedMovePacketBatch(
    const std::uint8_t* data, const std::size_t len,
    std::vector<BatchedMovePacket> &out) {
  if (data == nullptr || len < sizeof(std::uint32_t)) {
    return false;
  }

  const auto stream_size = static_cast<std::size_t>(ReadU32(data));
  if (stream_size == 0u) {
    return false;
  }
  std::vector<std::uint8_t> stream(stream_size);
  uLongf decoded_size = static_cast<uLongf>(stream.size());
  const auto result = uncompress(
      stream.data(), &decoded_size, data + sizeof(std::uint32_t),
      static_cast<uLong>(len - sizeof(std::uint32_t)));
  if (result != Z_OK) {
    return false;
  }
  stream.resize(static_cast<std::size_t>(decoded_size));
  return DecodePacketStream(stream.data(), stream.size(), out);
}

}

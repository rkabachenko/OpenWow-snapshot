
#include "openwow/net/wotlk/grunt_xfer_data.h"

#include <cstring>

namespace openwow::net::wotlk {

namespace {

std::uint16_t ReadUInt16LE(const std::uint8_t* p) {
  std::uint16_t v;
  std::memcpy(&v, p, sizeof(v));
  return v;
}

}

PacketHandlerResult HandleXferData(
    const std::uint8_t* data,
    std::size_t size,
    std::size_t& read_pos,
    const XferDataReceiverFn& receiver) {

  if (read_pos + 2 > size) {
    return PacketHandlerResult::kConsumed;

  }

  const std::uint16_t chunk_length = ReadUInt16LE(data + read_pos);
  read_pos += 2;

  if (read_pos + chunk_length > size) {
    return PacketHandlerResult::kConsumed;

  }

  const std::uint8_t* chunk_ptr = data + read_pos;
  read_pos += chunk_length;

  if (read_pos > size) {
    return PacketHandlerResult::kCorrupt;

  }

  receiver(chunk_ptr, chunk_length);

  return PacketHandlerResult::kContinue;

}

}

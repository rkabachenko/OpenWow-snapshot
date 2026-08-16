
#include "openwow/net/wotlk/grunt_bnet_kick_handler.h"

#include <cstring>

namespace openwow::net::wotlk {

namespace {

std::uint32_t ReadUInt32LE(const std::uint8_t* p) {
  std::uint32_t v;
  std::memcpy(&v, p, sizeof(v));
  return v;
}

std::uint64_t ReadUInt64LE(const std::uint8_t* p) {
  std::uint64_t v;
  std::memcpy(&v, p, sizeof(v));
  return v;
}

}

PacketHandlerResult HandleBNetKickMessage(
    const std::uint8_t* data,
    std::size_t size,
    std::size_t& read_pos,
    const KickAccountFn& kick_fn) {

  if (read_pos > size || size - read_pos < kBNetKickMinBytes) {
    return PacketHandlerResult::kConsumed;

  }

  const std::uint32_t account_id = ReadUInt32LE(data + read_pos);
  read_pos += 4;

  const std::uint64_t account_guid = ReadUInt64LE(data + read_pos);
  read_pos += 8;

  const std::uint8_t reason = data[read_pos];
  read_pos += 1;

  if (read_pos > size) {
    return PacketHandlerResult::kCorrupt;

  }

  kick_fn(account_id, account_guid, reason);

  return PacketHandlerResult::kContinue;

}

}

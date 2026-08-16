
#include "openwow/net/wotlk/grunt_bnet_token_response.h"

#include <cstring>

namespace openwow::net::wotlk {

namespace {

std::uint32_t ReadUInt32LE(const std::uint8_t* p) {
  std::uint32_t v;
  std::memcpy(&v, p, sizeof(v));
  return v;
}

void PutUInt8(std::vector<std::uint8_t>& buf, std::uint8_t v) {
  buf.push_back(v);
}

void PutUInt32(std::vector<std::uint8_t>& buf, std::uint32_t v) {
  buf.push_back(static_cast<std::uint8_t>(v & 0xFFu));
  buf.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
  buf.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
  buf.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
}

}

PacketHandlerResult HandleBNetTokenResponse(
    const std::uint8_t* data,
    std::size_t size,
    std::size_t& read_pos,
    const AuthPacketSendFn& send_fn) {

  if (read_pos + 4 > size) {
    return PacketHandlerResult::kConsumed;

  }

  const std::uint32_t token = ReadUInt32LE(data + read_pos);
  read_pos += 4;

  if (read_pos > size) {
    return PacketHandlerResult::kCorrupt;

  }

  std::vector<std::uint8_t> packet;
  packet.reserve(1 + 4);

  PutUInt8(packet, kBNetTokenResponseOpcode);
  PutUInt32(packet, token);

  send_fn(packet);

  return PacketHandlerResult::kContinue;

}

PacketHandlerResult HandleBNetKeepAlive(
    [[maybe_unused]] const std::uint8_t* data,
    std::size_t size,
    std::size_t& read_pos,
    const StoreTickFn& store_tick,
    const TickCountFn& get_tick) {
  if (read_pos + 4 > size) {
    return PacketHandlerResult::kConsumed;

  }

  read_pos += 4;

  if (read_pos > size) {
    return PacketHandlerResult::kCorrupt;

  }

  store_tick(get_tick());

  return PacketHandlerResult::kContinue;

}

}

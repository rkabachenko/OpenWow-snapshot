
#include "openwow/net/wotlk/grunt_chunked_data_transfer.h"

#include <algorithm>
#include <cstring>

namespace openwow::net::wotlk {

namespace {

void PutUInt8(std::vector<std::uint8_t>& buf, std::uint8_t v) {
  buf.push_back(v);
}

void PutUInt32(std::vector<std::uint8_t>& buf, std::uint32_t v) {
  buf.push_back(static_cast<std::uint8_t>(v & 0xFFu));
  buf.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
  buf.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
  buf.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
}

std::uint32_t ReadUInt32LE(const std::uint8_t* p) {
  std::uint32_t v;
  std::memcpy(&v, p, sizeof(v));
  return v;
}

}

void SendChunkedAccountData(
    std::uint32_t request_id,
    std::uint32_t data_type,
    const AccountDataProvider& data_provider,
    const AuthPacketSendFn& chunk_send_fn,
    const AuthPacketSendFn& checked_send_fn) {

  const std::vector<std::uint8_t> blob = data_provider(request_id, data_type);
  const auto total_size = static_cast<std::uint32_t>(blob.size());

  std::uint32_t offset = 0;
  while (offset < total_size) {
    auto chunk_size = static_cast<std::size_t>(total_size - offset);
    if (chunk_size > kMaxAccountDataChunkSize) {
      chunk_size = kMaxAccountDataChunkSize;
    }

    std::vector<std::uint8_t> packet;
    packet.reserve(1 + 5 * 4 + chunk_size);

    PutUInt8(packet, kAccountDataChunkOpcode);
    PutUInt32(packet, request_id);
    PutUInt32(packet, data_type);
    PutUInt32(packet, offset);
    PutUInt32(packet, total_size);
    PutUInt32(packet, static_cast<std::uint32_t>(chunk_size));
    packet.insert(packet.end(),
                  blob.begin() + offset,
                  blob.begin() + offset + chunk_size);

    chunk_send_fn(packet);

    offset += static_cast<std::uint32_t>(chunk_size);
  }

  std::vector<std::uint8_t> terminator;
  terminator.reserve(1 + 5 * 4);

  PutUInt8(terminator, kAccountDataChunkOpcode);
  PutUInt32(terminator, request_id);
  PutUInt32(terminator, data_type);
  PutUInt32(terminator, 0xFFFFFFFFu);
  PutUInt32(terminator, total_size);
  PutUInt32(terminator, 0u);

  checked_send_fn(terminator);
}

PacketHandlerResult HandleAccountDataRequest(
    const std::uint8_t* data,
    std::size_t size,
    std::size_t& read_pos,
    const AccountDataProvider& data_provider,
    const AuthPacketSendFn& chunk_send_fn,
    const AuthPacketSendFn& checked_send_fn) {

  if (read_pos + 4 > size) {
    return PacketHandlerResult::kConsumed;

  }

  const std::uint32_t request_id = ReadUInt32LE(data + read_pos);
  read_pos += 4;

  if (read_pos + 4 > size) {
    return PacketHandlerResult::kConsumed;
  }
  const std::uint32_t data_type = ReadUInt32LE(data + read_pos);
  read_pos += 4;

  if (read_pos > size) {
    return PacketHandlerResult::kCorrupt;

  }

  if (data_type != 0) {

    SendChunkedAccountData(request_id, data_type,
                           data_provider, chunk_send_fn, checked_send_fn);
  } else {

    for (std::size_t i = 0; i < kDefaultAccountDataTypeCount; ++i) {
      SendChunkedAccountData(request_id, kDefaultAccountDataTypes[i],
                             data_provider, chunk_send_fn, checked_send_fn);
    }
  }

  return PacketHandlerResult::kContinue;

}

}

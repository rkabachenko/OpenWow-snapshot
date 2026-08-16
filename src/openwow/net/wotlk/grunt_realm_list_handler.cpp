
#include "openwow/net/wotlk/grunt_realm_list_handler.h"

#include <cstring>

namespace openwow::net::wotlk {

namespace {

std::uint16_t ReadUInt16LE(const std::uint8_t* p) {
  std::uint16_t v;
  std::memcpy(&v, p, sizeof(v));
  return v;
}

std::uint32_t ReadUInt32LE(const std::uint8_t* p) {
  std::uint32_t v;
  std::memcpy(&v, p, sizeof(v));
  return v;
}

float ReadFloatLE(const std::uint8_t* p) {
  float v;
  std::memcpy(&v, p, sizeof(v));
  return v;
}

bool SkipCString(const std::uint8_t* data, std::size_t size,
                 std::size_t& pos, std::size_t max_len) {
  const std::size_t start = pos;
  while (pos < size) {
    if (data[pos++] == 0) {
      if (pos - start - 1 > max_len) {
        return false;
      }
      return true;
    }
  }
  return false;
}

}

PacketHandlerResult HandleRealmList(
    const std::uint8_t* data,
    std::size_t size,
    std::size_t& read_pos,
    const RealmListReceiverFn& receiver) {

  if (read_pos + 2 > size) {
    return PacketHandlerResult::kConsumed;

  }

  const std::uint16_t packet_size = ReadUInt16LE(data + read_pos);
  read_pos += 2;

  if (read_pos + packet_size > size) {
    return PacketHandlerResult::kConsumed;

  }

  const std::size_t start_pos = read_pos;

  if (read_pos + 4 > size) {
    return PacketHandlerResult::kCorrupt;
  }

  (void)ReadUInt32LE(data + read_pos);
  read_pos += 4;

  const std::size_t after_header_pos = read_pos;

  if (read_pos + 2 > size) {
    return PacketHandlerResult::kCorrupt;
  }
  const std::uint16_t realm_count = ReadUInt16LE(data + read_pos);
  read_pos += 2;

  for (std::uint16_t i = 0; i < realm_count; ++i) {
    if (read_pos + 1 > size) {
      return PacketHandlerResult::kCorrupt;
    }
    read_pos += 1;

    if (read_pos + 1 > size) {
      return PacketHandlerResult::kCorrupt;
    }
    read_pos += 1;

    if (read_pos + 1 > size) {
      return PacketHandlerResult::kCorrupt;
    }
    const std::uint8_t flags = data[read_pos];
    read_pos += 1;

    if (!SkipCString(data, size, read_pos, 0x100)) {
      return PacketHandlerResult::kCorrupt;
    }

    if (!SkipCString(data, size, read_pos, 0x100)) {
      return PacketHandlerResult::kCorrupt;
    }

    if (read_pos + 4 > size) {
      return PacketHandlerResult::kCorrupt;
    }

    (void)ReadFloatLE(data + read_pos);
    read_pos += 4;

    if (read_pos + 1 > size) {
      return PacketHandlerResult::kCorrupt;
    }
    read_pos += 1;

    if (read_pos + 1 > size) {
      return PacketHandlerResult::kCorrupt;
    }
    read_pos += 1;

    if (read_pos + 1 > size) {
      return PacketHandlerResult::kCorrupt;
    }
    read_pos += 1;

    if ((flags & 0x04) != 0) {
      if (read_pos + 3 > size) {
        return PacketHandlerResult::kCorrupt;
      }
      read_pos += 3;

      if (read_pos + 2 > size) {
        return PacketHandlerResult::kCorrupt;
      }
      read_pos += 2;
    }

    if (read_pos > size) {
      return PacketHandlerResult::kCorrupt;

    }
  }

  if (read_pos + 2 > size) {
    return PacketHandlerResult::kCorrupt;
  }
  read_pos += 2;

  const std::size_t end_pos = read_pos;
  if (end_pos > size) {
    return PacketHandlerResult::kCorrupt;

  }
  if (end_pos - start_pos != packet_size) {
    return PacketHandlerResult::kCorrupt;

  }

  std::size_t rewind_pos = after_header_pos;
  receiver(data, size, rewind_pos);

  read_pos = end_pos;

  return PacketHandlerResult::kContinue;

}

}

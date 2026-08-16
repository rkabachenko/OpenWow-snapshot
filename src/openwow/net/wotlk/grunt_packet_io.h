
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

#include "openwow/foundation/diagnostics/logging.h"

namespace openwow::net::wotlk {

enum class PacketHandlerResult : int {
  kConsumed = 0,
  kCorrupt  = 1,
  kContinue = 2,
};

using AuthPacketHandler =
    std::function<PacketHandlerResult(const std::uint8_t* data,
                                      std::size_t         size,
                                      std::size_t&        read_pos)>;

struct AuthCommandEntry {
  std::uint8_t opcode{0};
  std::string  debug_name;
  AuthPacketHandler handler;
};

struct AuthCommandTable {
  std::vector<AuthCommandEntry> entries;

  AuthCommandTable() {
    entries = {
        {0x00, "ClientLink::CMD_AUTH_LOGON_CHALLENGE", {}},
        {0x01, "ClientLink::CMD_AUTH_LOGON_PROOF", {}},
        {0x02, "ClientLink::CMD_AUTH_RECONNECT_CHALLENGE", {}},
        {0x03, "ClientLink::CMD_AUTH_RECONNECT_PROOF", {}},
        {0x10, "ClientLink::CMD_REALM_LIST", {}},
        {0x30, "ClientLink::CMD_XFER_INITIATE", {}},
        {0x31, "ClientLink::CMD_XFER_DATA", {}},
    };
  }

  void Bind(std::uint8_t opcode, AuthPacketHandler handler) {
    for (auto& e : entries) {
      if (e.opcode == opcode) {
        e.handler = std::move(handler);
        return;
      }
    }
  }
};

namespace detail {
inline std::string HexByte(std::uint8_t v) {
  static constexpr char hex[] = "0123456789ABCDEF";
  return std::string{hex[v >> 4], hex[v & 0xF]};
}
}

inline bool PacketIO(const std::uint8_t* data,
                     std::size_t size,
                     std::size_t& read_pos,
                     std::span<const AuthCommandEntry> commands,
                     std::size_t& out_resume) {
  while (true) {
    if (read_pos >= size) {
      return true;
    }

    const std::uint8_t opcode = data[read_pos];
    ++read_pos;

    const AuthCommandEntry* found = nullptr;
    for (const auto& entry : commands) {
      if (entry.opcode == opcode) {
        found = &entry;
        break;
      }
    }

    if (!found || !found->handler) {
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kWarn,
          "GruntLogin::PacketIO: unrecognised command 0x" +
              detail::HexByte(opcode));

      const std::size_t remaining =
          (size > read_pos) ? (size - read_pos) : 0;
      if (remaining > 0) {
        openwow::diagnostics::Log(
            openwow::diagnostics::LogLevel::kWarn,
            "GruntLogin::PacketIO: " + std::to_string(remaining) +
                " trailing bytes in buffer");
      }
      return false;
    }

    const PacketHandlerResult result =
        found->handler(data, size, read_pos);

    switch (result) {
      case PacketHandlerResult::kConsumed:
        return true;

      case PacketHandlerResult::kCorrupt:
        openwow::diagnostics::Log(
            openwow::diagnostics::LogLevel::kWarn,
            "GruntLogin::PacketIO: corrupt data for command 0x" +
                detail::HexByte(opcode));
        {
          const std::size_t rem =
              (size > read_pos) ? (size - read_pos) : 0;
          if (rem > 0) {
            openwow::diagnostics::Log(
                openwow::diagnostics::LogLevel::kWarn,
                "GruntLogin::PacketIO: " + std::to_string(rem) +
                    " trailing bytes after corrupt command");
          }
        }
        return false;

      case PacketHandlerResult::kContinue:
        out_resume = read_pos;
        break;

      default:
        break;
    }
  }
}

}

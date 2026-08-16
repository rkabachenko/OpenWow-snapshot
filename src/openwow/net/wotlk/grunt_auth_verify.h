#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "openwow/net/wotlk/grunt_packet_io.h"

namespace openwow::net::wotlk {

inline constexpr std::uint8_t kAuthVerifyMaxNameLength = 0x20;

inline constexpr std::size_t kAuthVerifyServerHashSize = 16;
inline constexpr std::size_t kAuthVerifyDigestSize     = 20;

inline constexpr std::size_t kAuthVerifyTrailingBytes =
    kAuthVerifyServerHashSize + kAuthVerifyDigestSize;

enum class AuthProtocolType : std::uint32_t {
  kGrunt     = 0,
  kBattleNet = 1,
};

inline const char* AuthProtocolTypeName(std::uint32_t type) {
  switch (type) {
    case 0: return "Grunt";
    case 1: return "Battlenet";
    default: return "None";
  }
}

struct AuthVerifyData {
  std::string server_name;
  std::uint8_t server_hash[kAuthVerifyServerHashSize]{};
  std::uint8_t verify_hash[kAuthVerifyDigestSize]{};
};

using AuthVerifyObserverFn =
    std::function<void(const AuthVerifyData& data)>;

PacketHandlerResult HandleAuthVerify(
    const std::uint8_t* data,
    std::size_t size,
    std::size_t& read_pos,
    std::uint32_t protocol_type,
    const char* server_name_log,
    const AuthVerifyObserverFn& observer = {});

}

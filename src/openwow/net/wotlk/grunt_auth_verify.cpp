
#include "openwow/net/wotlk/grunt_auth_verify.h"

#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <cstring>

namespace openwow::net::wotlk {

PacketHandlerResult HandleAuthVerify(
    const std::uint8_t* data,
    std::size_t size,
    std::size_t& read_pos,
    std::uint32_t protocol_type,
    const char* server_name_log,
    const AuthVerifyObserverFn& observer) {
  if (read_pos >= size) {
    return PacketHandlerResult::kConsumed;
  }

  const std::uint8_t name_len = data[read_pos];
  ++read_pos;

  if (name_len >= kAuthVerifyMaxNameLength) {
    return PacketHandlerResult::kCorrupt;
  }

  const std::size_t needed =
      static_cast<std::size_t>(name_len) + kAuthVerifyTrailingBytes;
  if (read_pos > size || size - read_pos < needed) {
    return PacketHandlerResult::kConsumed;
  }

  openwow::diagnostics::Log(
      openwow::diagnostics::LogLevel::kInfo,
      std::string(AuthProtocolTypeName(protocol_type)) +
          " Auth Verify. Server name=" +
          (server_name_log ? server_name_log : ""));

  AuthVerifyData parsed;
  parsed.server_name.assign(
      reinterpret_cast<const char*>(data + read_pos), name_len);
  read_pos += name_len;

  std::memcpy(parsed.server_hash, data + read_pos,
              kAuthVerifyServerHashSize);
  read_pos += kAuthVerifyServerHashSize;

  std::memcpy(parsed.verify_hash, data + read_pos,
              kAuthVerifyDigestSize);
  read_pos += kAuthVerifyDigestSize;

  if (read_pos > size) {
    return PacketHandlerResult::kCorrupt;
  }

  if (observer) {
    observer(parsed);
  }

  return PacketHandlerResult::kContinue;
}

}

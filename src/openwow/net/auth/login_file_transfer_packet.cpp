#include "openwow/net/auth/login_file_transfer_packet.h"

#include <cstddef>

namespace openwow::net::auth {

namespace {

constexpr std::uint8_t kTransferAcceptOpcode = 0x32;
constexpr std::uint8_t kTransferResumeOpcode = 0x33;
constexpr std::uint8_t kTransferCancelOpcode = 0x34;

}

std::vector<std::uint8_t> BuildLoginFileTransferResponsePacket(
    const bool transfer_needed,
    const std::uint64_t resume_offset) {
  if (!transfer_needed) {
    return {kTransferCancelOpcode};
  }

  if (resume_offset == 0) {
    return {kTransferAcceptOpcode};
  }

  std::vector<std::uint8_t> packet(9);
  packet[0] = kTransferResumeOpcode;
  for (std::size_t byte_index = 0; byte_index < sizeof(resume_offset);
       ++byte_index) {
    packet[byte_index + 1] = static_cast<std::uint8_t>(
        resume_offset >> (byte_index * 8));
  }
  return packet;
}

}

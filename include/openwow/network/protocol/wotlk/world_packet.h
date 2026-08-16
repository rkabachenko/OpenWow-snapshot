#pragma once

#include "openwow/network/protocol/wotlk/opcodes.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace openwow::net::wotlk {

enum class ServerFrameDecodeStatus : std::uint8_t {
  kComplete,
  kIncomplete,
  kInvalidBodySize,
};

struct ServerFrameDecodeResult;

struct WorldPacket {
  static constexpr std::size_t kTempPacketTotalCapacity = 0x300;
  static constexpr std::size_t kTempPacketPayloadReserve =
      kTempPacketTotalCapacity - sizeof(std::uint32_t);

  Opcode opcode{};
  std::vector<std::uint8_t> payload;

  WorldPacket() = default;
  explicit WorldPacket(Opcode packet_opcode) : opcode(packet_opcode) {
    payload.reserve(kTempPacketPayloadReserve);
  }
  WorldPacket(Opcode packet_opcode, std::vector<std::uint8_t> packet_payload)
      : opcode(packet_opcode), payload(std::move(packet_payload)) {}

  [[nodiscard]] bool IsOpcode(Opcode expected) const {
    return opcode == expected;
  }

  [[nodiscard]] Opcode GetOpcode() const { return opcode; }

  [[nodiscard]] std::vector<std::uint8_t> SerializeClientFrame() const;
  [[nodiscard]] static ServerFrameDecodeResult DecodeServerFrame(
      std::span<const std::uint8_t> bytes);

  void AppendU8(std::uint8_t value);
  void AppendU16(std::uint16_t value);
  void AppendU32(std::uint32_t value);
  void AppendU64(std::uint64_t value);
  void AppendFloat(float value);
  void AppendBytes(const std::uint8_t* data, std::size_t size);
  void AppendString(std::string_view value);

  [[nodiscard]] static std::size_t ClientSizeFieldLength(
      std::size_t payload_size);
  [[nodiscard]] static std::size_t ServerSizeFieldLength(
      std::uint8_t first_byte);
};

struct ServerFrameDecodeResult {
  ServerFrameDecodeStatus status{ServerFrameDecodeStatus::kIncomplete};
  std::size_t bytes_consumed{0};
  WorldPacket packet;

  [[nodiscard]] bool IsComplete() const {
    return status == ServerFrameDecodeStatus::kComplete;
  }
};

[[nodiscard]] inline std::size_t ServerFullHeaderLength(
    std::uint8_t first_decrypted_byte) {
  return WorldPacket::ServerSizeFieldLength(first_decrypted_byte) +
         sizeof(std::uint16_t);
}

[[nodiscard]] inline std::size_t ClientFullHeaderLength(
    std::size_t payload_size) {
  return WorldPacket::ClientSizeFieldLength(payload_size) +
         sizeof(std::uint32_t);
}

}

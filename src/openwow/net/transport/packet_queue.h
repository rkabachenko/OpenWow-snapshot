#pragma once

#include "openwow/network/protocol/wotlk/world_packet.h"

#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace openwow::net {

class PacketQueue {
 public:
  PacketQueue() = default;

  explicit PacketQueue(std::string name) : name_(std::move(name)) {}

  ~PacketQueue() = default;

  PacketQueue(const PacketQueue&) = delete;
  PacketQueue& operator=(const PacketQueue&) = delete;
  PacketQueue(PacketQueue&&) = delete;
  PacketQueue& operator=(PacketQueue&&) = delete;

  void Push(wotlk::WorldPacket pkt);

  [[nodiscard]] std::optional<wotlk::WorldPacket> Pop();

  [[nodiscard]] std::vector<wotlk::WorldPacket> PopAll();

  [[nodiscard]] std::uint32_t Size() const;

  [[nodiscard]] bool IsEmpty() const;

  void Clear();

  void SetMaxSize(std::uint32_t max_size);

  [[nodiscard]] std::uint32_t GetDropCount() const;

  [[nodiscard]] std::uint64_t GetTotalProcessed() const;

  [[nodiscard]] std::optional<wotlk::WorldPacket> Peek() const;

  [[nodiscard]] std::vector<wotlk::WorldPacket> PopN(std::uint32_t count);

  [[nodiscard]] std::vector<wotlk::WorldPacket> PopByOpcode(wotlk::Opcode opcode);

  [[nodiscard]] bool ContainsOpcode(wotlk::Opcode opcode) const;

  [[nodiscard]] std::optional<wotlk::Opcode> PeekOpcode() const;

  [[nodiscard]] std::uint32_t GetMaxSize() const;

 private:

  static constexpr std::uint32_t kDropLogThrottleInterval = 100;

  mutable std::mutex mutex_;
  std::deque<wotlk::WorldPacket> queue_;
  std::uint32_t max_size_{0};
  std::uint32_t drop_count_{0};
  std::uint64_t total_processed_{0};
  std::string name_{"PacketQueue"};
};

}

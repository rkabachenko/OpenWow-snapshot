#include "openwow/net/transport/packet_queue.h"

#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <string>

namespace openwow::net {

void PacketQueue::Push(wotlk::WorldPacket pkt) {
  std::lock_guard<std::mutex> lock(mutex_);
  queue_.push_back(std::move(pkt));
  total_processed_++;

  if (max_size_ > 0) {
    while (queue_.size() > max_size_) {
      const wotlk::Opcode dropped_opcode = queue_.front().opcode;
      queue_.pop_front();
      drop_count_++;

      if (drop_count_ == 1 || drop_count_ % kDropLogThrottleInterval == 0) {
        diagnostics::Log(
            diagnostics::LogLevel::kWarn,
            "PacketQueue[" + name_ +
                "]: overflow, dropped oldest packet (opcode=" +
                std::string(wotlk::OpcodeName(dropped_opcode)) + " 0x" +
                std::to_string(static_cast<unsigned>(dropped_opcode)) +
                "), total drops=" + std::to_string(drop_count_) +
                ", max_size=" + std::to_string(max_size_));
      }
    }
  }
}

std::optional<wotlk::WorldPacket> PacketQueue::Pop() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (queue_.empty()) {
    return std::nullopt;
  }
  auto pkt = std::move(queue_.front());
  queue_.pop_front();
  return pkt;
}

std::vector<wotlk::WorldPacket> PacketQueue::PopAll() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<wotlk::WorldPacket> result;
  result.reserve(queue_.size());
  for (auto& pkt : queue_) {
    result.push_back(std::move(pkt));
  }
  queue_.clear();
  return result;
}

std::vector<wotlk::WorldPacket> PacketQueue::PopN(std::uint32_t count) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<wotlk::WorldPacket> result;
  auto n = std::min(static_cast<std::size_t>(count), queue_.size());
  result.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    result.push_back(std::move(queue_.front()));
    queue_.pop_front();
  }
  return result;
}

std::vector<wotlk::WorldPacket> PacketQueue::PopByOpcode(wotlk::Opcode opcode) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<wotlk::WorldPacket> result;
  auto it = queue_.begin();
  while (it != queue_.end()) {
    if (it->opcode == opcode) {
      result.push_back(std::move(*it));
      it = queue_.erase(it);
    } else {
      ++it;
    }
  }
  return result;
}

std::uint32_t PacketQueue::Size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return static_cast<std::uint32_t>(queue_.size());
}

bool PacketQueue::IsEmpty() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return queue_.empty();
}

void PacketQueue::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  queue_.clear();
}

void PacketQueue::SetMaxSize(std::uint32_t max_size) {
  std::lock_guard<std::mutex> lock(mutex_);
  max_size_ = max_size;

  if (max_size_ > 0) {
    while (queue_.size() > max_size_) {
      const wotlk::Opcode dropped_opcode = queue_.front().opcode;
      queue_.pop_front();
      drop_count_++;

      if (drop_count_ == 1 || drop_count_ % kDropLogThrottleInterval == 0) {
        diagnostics::Log(
            diagnostics::LogLevel::kWarn,
            "PacketQueue[" + name_ +
                "]: SetMaxSize trim, dropped oldest packet (opcode=" +
                std::string(wotlk::OpcodeName(dropped_opcode)) + " 0x" +
                std::to_string(static_cast<unsigned>(dropped_opcode)) +
                "), total drops=" + std::to_string(drop_count_) +
                ", max_size=" + std::to_string(max_size_));
      }
    }
  }
}

std::uint32_t PacketQueue::GetMaxSize() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return max_size_;
}

std::uint32_t PacketQueue::GetDropCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return drop_count_;
}

std::uint64_t PacketQueue::GetTotalProcessed() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return total_processed_;
}

std::optional<wotlk::WorldPacket> PacketQueue::Peek() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (queue_.empty()) return std::nullopt;
  return queue_.front();
}

std::optional<wotlk::Opcode> PacketQueue::PeekOpcode() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (queue_.empty()) return std::nullopt;
  return queue_.front().opcode;
}

bool PacketQueue::ContainsOpcode(wotlk::Opcode opcode) const {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto& pkt : queue_) {
    if (pkt.opcode == opcode) return true;
  }
  return false;
}

}

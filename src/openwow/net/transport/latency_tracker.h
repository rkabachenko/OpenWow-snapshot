#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>

#include "openwow/network/protocol/wotlk/world_packet.h"

namespace openwow::net {

using PingSendFn = std::function<void(const wotlk::WorldPacket&)>;

using ClockFn = std::function<std::uint32_t()>;

class LatencyTracker {
 public:
  static constexpr std::uint32_t kWindowSize = 16;

  static constexpr std::uint32_t kHighLatencyThreshold = 300;

  LatencyTracker();
  ~LatencyTracker() = default;

  void SetSendFn(PingSendFn fn) { send_fn_ = std::move(fn); }

  void SetClockFn(ClockFn fn) { clock_fn_ = std::move(fn); }

  void SendPing(std::uint32_t sequence);

  void ObservePingSent(std::uint32_t sequence,
                       std::uint32_t send_timestamp_ms);

  void HandlePong(std::uint32_t sequence);

  [[nodiscard]] std::uint32_t GetLatency() const { return smoothed_rtt_; }

  [[nodiscard]] std::uint32_t GetWorldLatency() const {
    return world_latency_;
  }

  [[nodiscard]] std::uint32_t GetRawLatency() const { return raw_latency_; }

  [[nodiscard]] std::uint32_t GetMinLatency() const;
  [[nodiscard]] std::uint32_t GetMaxLatency() const;
  [[nodiscard]] std::uint32_t GetAverageLatency() const;
  [[nodiscard]] std::uint32_t GetJitter() const;

  [[nodiscard]] bool IsHighLatency() const {
    return smoothed_rtt_ > kHighLatencyThreshold;
  }

  [[nodiscard]] std::uint32_t GetPingSampleCount() const {
    return sample_count_;
  }

  void SetAutoInterval(float seconds) { auto_interval_ = seconds; }

  void Update(float dt);

  void Reset();

  void SetWorldLatency(std::uint32_t ms) { world_latency_ = ms; }

 private:
  [[nodiscard]] std::uint32_t Now() const;

  PingSendFn send_fn_;
  ClockFn clock_fn_;

  std::uint32_t pending_seq_{0};
  std::uint32_t pending_time_{0};
  bool has_pending_{false};

  std::array<std::uint32_t, kWindowSize> samples_{};
  std::uint32_t sample_index_{0};
  std::uint32_t sample_count_{0};

  std::uint32_t smoothed_rtt_{0};
  std::uint32_t raw_latency_{0};
  std::uint32_t world_latency_{0};

  float auto_interval_{0.0f};
  float auto_timer_{0.0f};
  std::uint32_t auto_sequence_{1};
};

}

#include "openwow/net/transport/latency_tracker.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace openwow::net {

LatencyTracker::LatencyTracker() {
  samples_.fill(0);
}

std::uint32_t LatencyTracker::Now() const {
  if (clock_fn_) {
    return clock_fn_();
  }
  auto now = std::chrono::steady_clock::now();
  return static_cast<std::uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          now.time_since_epoch())
          .count());
}

void LatencyTracker::SendPing(std::uint32_t sequence) {
  ObservePingSent(sequence, Now());

  if (send_fn_) {
    wotlk::WorldPacket pkt(wotlk::Opcode::CMSG_PING);
    pkt.AppendU32(sequence);
    pkt.AppendU32(smoothed_rtt_);
    send_fn_(pkt);
  }
}

void LatencyTracker::ObservePingSent(
    const std::uint32_t sequence,
    const std::uint32_t send_timestamp_ms) {
  pending_seq_ = sequence;
  pending_time_ = send_timestamp_ms;
  has_pending_ = true;
}

void LatencyTracker::HandlePong(std::uint32_t sequence) {
  if (!has_pending_ || sequence != pending_seq_) {
    return;
  }

  std::uint32_t now = Now();
  std::uint32_t rtt = (now >= pending_time_) ? (now - pending_time_) : 0;
  has_pending_ = false;
  raw_latency_ = rtt;

  samples_[sample_index_ % kWindowSize] = rtt;
  sample_index_++;
  if (sample_count_ < kWindowSize) {
    sample_count_++;
  }

  if (smoothed_rtt_ == 0) {
    smoothed_rtt_ = rtt;
  } else {
    smoothed_rtt_ = static_cast<std::uint32_t>(
        smoothed_rtt_ * 0.875f + rtt * 0.125f);
  }
}

std::uint32_t LatencyTracker::GetMinLatency() const {
  if (sample_count_ == 0) return 0;
  std::uint32_t n = std::min(sample_count_, kWindowSize);
  std::uint32_t min_val = samples_[0];
  for (std::uint32_t i = 1; i < n; ++i) {
    min_val = std::min(min_val, samples_[i]);
  }
  return min_val;
}

std::uint32_t LatencyTracker::GetMaxLatency() const {
  if (sample_count_ == 0) return 0;
  std::uint32_t n = std::min(sample_count_, kWindowSize);
  std::uint32_t max_val = samples_[0];
  for (std::uint32_t i = 1; i < n; ++i) {
    max_val = std::max(max_val, samples_[i]);
  }
  return max_val;
}

std::uint32_t LatencyTracker::GetAverageLatency() const {
  if (sample_count_ == 0) return 0;
  std::uint32_t n = std::min(sample_count_, kWindowSize);
  std::uint64_t sum = 0;
  for (std::uint32_t i = 0; i < n; ++i) {
    sum += samples_[i];
  }
  return static_cast<std::uint32_t>(sum / n);
}

std::uint32_t LatencyTracker::GetJitter() const {
  if (sample_count_ < 2) return 0;
  std::uint32_t n = std::min(sample_count_, kWindowSize);
  std::uint32_t avg = GetAverageLatency();
  std::uint64_t variance_sum = 0;
  for (std::uint32_t i = 0; i < n; ++i) {
    std::int64_t diff = static_cast<std::int64_t>(samples_[i]) -
                        static_cast<std::int64_t>(avg);
    variance_sum += static_cast<std::uint64_t>(diff * diff);
  }
  return static_cast<std::uint32_t>(
      std::sqrt(static_cast<double>(variance_sum) / n));
}

void LatencyTracker::Update(float dt) {
  if (auto_interval_ <= 0.0f) return;

  auto_timer_ += dt;
  if (auto_timer_ >= auto_interval_) {
    auto_timer_ -= auto_interval_;
    SendPing(auto_sequence_++);
  }
}

void LatencyTracker::Reset() {
  pending_seq_ = 0;
  pending_time_ = 0;
  has_pending_ = false;
  samples_.fill(0);
  sample_index_ = 0;
  sample_count_ = 0;
  smoothed_rtt_ = 0;
  raw_latency_ = 0;
  world_latency_ = 0;
  auto_timer_ = 0.0f;
  auto_sequence_ = 1;
}

}

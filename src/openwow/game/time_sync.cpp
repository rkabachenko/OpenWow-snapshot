#include "openwow/game/time_sync.h"

#include <algorithm>

#include "openwow/foundation/diagnostics/logging.h"

namespace openwow::game {

void TimeSyncManager::RecordServerRequest(const std::uint32_t counter,
                                          const std::uint32_t client_time_ms) {

  if (first_sync_) {
    expected_counter_ = counter;
    first_sync_ = false;
  } else if (counter < expected_counter_) {

    diagnostics::Log(diagnostics::LogLevel::kInfo,
              "TimeSync: server restarted the counter sequence, previous "
              "expected=" +
              std::to_string(expected_counter_) +
              " got=" + std::to_string(counter));
  } else if (counter > expected_counter_) {
    const std::uint32_t gap = counter - expected_counter_;
    missed_syncs_ += gap;
    diagnostics::Log(diagnostics::LogLevel::kWarn,
              "TimeSync: counter gap detected, expected=" +
              std::to_string(expected_counter_) +
              " got=" + std::to_string(counter) +
              " missed=" + std::to_string(gap));
  }
  expected_counter_ = counter + 1;
  last_counter_ = counter;

  last_client_time_ = client_time_ms;
  last_sync_client_time_ = client_time_ms;

  TimeSyncSample sample;
  sample.counter = counter;
  sample.server_counter = counter;
  sample.client_time_received = client_time_ms;
  sample.client_time_sent = client_time_ms;
  samples_.push_back(sample);
  if (samples_.size() > kMaxTimeSyncSamples) {
    samples_.pop_front();
  }

  ++sync_count_;

  diagnostics::Log(diagnostics::LogLevel::kDebug,
            "TimeSync: req counter=" + std::to_string(counter) +
            " clientTime=" + std::to_string(client_time_ms) +
            " syncCount=" + std::to_string(sync_count_));
}

void TimeSyncManager::RecordRoundTrip(std::uint32_t rtt_ms) {
  rtt_samples_.push_back(rtt_ms);
  if (rtt_samples_.size() > kMaxTimeSyncSamples) {
    rtt_samples_.pop_front();
  }
}

std::uint32_t TimeSyncManager::GetLatestRTT() const {
  if (rtt_samples_.empty()) return 0;
  return rtt_samples_.back();
}

std::uint32_t TimeSyncManager::GetAverageRTT() const {
  if (rtt_samples_.empty()) return 0;
  std::uint64_t sum = 0;
  for (auto v : rtt_samples_) sum += v;
  return static_cast<std::uint32_t>(sum / rtt_samples_.size());
}

std::uint32_t TimeSyncManager::GetMinRTT() const {
  if (rtt_samples_.empty()) return 0;
  return *std::min_element(rtt_samples_.begin(), rtt_samples_.end());
}

std::uint32_t TimeSyncManager::GetMaxRTT() const {
  if (rtt_samples_.empty()) return 0;
  return *std::max_element(rtt_samples_.begin(), rtt_samples_.end());
}

std::uint32_t TimeSyncManager::GetEstimatedLatency() const {
  return GetAverageRTT() / 2;
}

void TimeSyncManager::SetClockOffset(std::int64_t offset_ms) {
  clock_offset_ = offset_ms;
}

std::int64_t TimeSyncManager::GetClockOffset() const {
  return clock_offset_;
}

std::uint32_t TimeSyncManager::EstimateServerTime(
    std::uint32_t client_time) const {
  return static_cast<std::uint32_t>(
      static_cast<std::int64_t>(client_time) + clock_offset_);
}

std::uint32_t TimeSyncManager::EstimateCurrentServerTime() const {
  if (!client_time_fn_) return 0;
  return EstimateServerTime(client_time_fn_());
}

bool TimeSyncManager::IsCounterSequential() const {
  return missed_syncs_ == 0;
}

std::uint32_t TimeSyncManager::GetMissedSyncs() const {
  return missed_syncs_;
}

bool TimeSyncManager::IsTimedOut() const {
  if (!client_time_fn_ || sync_count_ == 0) return false;
  std::uint32_t now = client_time_fn_();

  std::uint32_t elapsed = now - last_sync_client_time_;
  return elapsed > timeout_ms_;
}

void TimeSyncManager::SetTimeoutMs(std::uint32_t ms) {
  timeout_ms_ = ms;
}

std::uint32_t TimeSyncManager::GetTimeoutMs() const {
  return timeout_ms_;
}

std::size_t TimeSyncManager::GetSampleCount() const {
  return samples_.size();
}

const std::deque<TimeSyncSample>& TimeSyncManager::GetSamples() const {
  return samples_;
}

void TimeSyncManager::ResetStatistics() {
  rtt_samples_.clear();
  samples_.clear();
  missed_syncs_ = 0;
  sync_count_ = 0;
  first_sync_ = true;
  expected_counter_ = 0;
  last_sync_client_time_ = 0;
}

void TimeSyncManager::Clear() {
  client_time_fn_ = nullptr;
  last_counter_ = 0;
  last_client_time_ = 0;
  clock_offset_ = 0;
  timeout_ms_ = kTimeSyncTimeoutMs;
  ResetStatistics();
}

}

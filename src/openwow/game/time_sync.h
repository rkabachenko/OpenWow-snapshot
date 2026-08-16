#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <utility>

namespace openwow::game {

using ClientTimeFn = std::function<std::uint32_t()>;

struct TimeSyncSample {
  std::uint32_t counter{0};
  std::uint32_t client_time_sent{0};
  std::uint32_t client_time_received{0};
  std::uint32_t server_counter{0};
};

inline constexpr std::size_t kMaxTimeSyncSamples = 16;

inline constexpr std::uint32_t kTimeSyncTimeoutMs = 60000;

class TimeSyncManager {
 public:

  void SetClientTimeFn(ClientTimeFn fn) { client_time_fn_ = std::move(fn); }

  void RecordServerRequest(std::uint32_t counter, std::uint32_t client_time_ms);

  [[nodiscard]] std::uint32_t last_counter() const { return last_counter_; }

  [[nodiscard]] std::uint32_t sync_count() const { return sync_count_; }

  [[nodiscard]] std::uint32_t last_client_time() const { return last_client_time_; }

  void RecordRoundTrip(std::uint32_t rtt_ms);

  [[nodiscard]] std::uint32_t GetLatestRTT() const;

  [[nodiscard]] std::uint32_t GetAverageRTT() const;

  [[nodiscard]] std::uint32_t GetMinRTT() const;

  [[nodiscard]] std::uint32_t GetMaxRTT() const;

  [[nodiscard]] std::uint32_t GetEstimatedLatency() const;

  void SetClockOffset(std::int64_t offset_ms);
  [[nodiscard]] std::int64_t GetClockOffset() const;

  [[nodiscard]] std::uint32_t EstimateServerTime(std::uint32_t client_time) const;

  [[nodiscard]] std::uint32_t EstimateCurrentServerTime() const;

  [[nodiscard]] bool IsCounterSequential() const;

  [[nodiscard]] std::uint32_t GetMissedSyncs() const;

  [[nodiscard]] bool IsTimedOut() const;

  void SetTimeoutMs(std::uint32_t ms);
  [[nodiscard]] std::uint32_t GetTimeoutMs() const;

  [[nodiscard]] std::size_t GetSampleCount() const;

  [[nodiscard]] const std::deque<TimeSyncSample>& GetSamples() const;

  void ResetStatistics();

 void Clear();

 private:
  ClientTimeFn client_time_fn_;

  std::uint32_t last_counter_{0};
  std::uint32_t last_client_time_{0};
  std::uint32_t sync_count_{0};

  std::uint32_t expected_counter_{0};
  std::uint32_t missed_syncs_{0};
  bool first_sync_{true};

  std::deque<std::uint32_t> rtt_samples_;

  std::deque<TimeSyncSample> samples_;

  std::int64_t clock_offset_{0};

  std::uint32_t timeout_ms_{kTimeSyncTimeoutMs};
  std::uint32_t last_sync_client_time_{0};
};

}

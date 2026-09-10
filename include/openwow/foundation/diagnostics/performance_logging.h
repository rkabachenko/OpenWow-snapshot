#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string_view>

namespace openwow::diagnostics {

// Release diagnostics, independent of Lua instruction hooks and debugger state.
void SetPerformanceLoggingEnabled(bool enabled);
[[nodiscard]] bool IsPerformanceLoggingEnabled() noexcept;

class PerformanceTimer {
 public:
  PerformanceTimer() noexcept;
  [[nodiscard]] bool enabled() const noexcept { return enabled_; }
  [[nodiscard]] double ElapsedMs() const noexcept;
  [[nodiscard]] std::int64_t StartUs() const noexcept;

 private:
  bool enabled_;
  std::chrono::steady_clock::time_point start_;
};

struct PerformanceLogSite {
  std::atomic<std::int64_t> next_log_us{0};
  std::atomic<std::uint64_t> suppressed{0};
  bool retain_severe{true};
};

// First slow call per site and every >=250 ms call are retained. Other repeats
// are limited to one per second; the next record reports the suppressed count.
void LogPerformanceDuration(PerformanceLogSite& site, std::string_view operation,
                            const PerformanceTimer& timer, std::string_view context,
                            double threshold_ms = 20.0);
void LogPerformanceEvent(std::string_view operation, std::string_view context);

class ScopedPerformanceLog {
 public:
  ScopedPerformanceLog(PerformanceLogSite& site, std::string_view operation,
                       std::string_view context = {}, double threshold_ms = 20.0);
  ~ScopedPerformanceLog();
  ScopedPerformanceLog(const ScopedPerformanceLog&) = delete;
  ScopedPerformanceLog& operator=(const ScopedPerformanceLog&) = delete;

 private:
  PerformanceLogSite& site_;
  std::string_view operation_;
  std::string_view context_;
  double threshold_ms_;
  PerformanceTimer timer_;
};

}

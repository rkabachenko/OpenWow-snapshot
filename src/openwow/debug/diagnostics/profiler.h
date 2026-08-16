#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace openwow::debug {

struct ProfilerStats {
  float min_ms = 0.0f;
  float max_ms = 0.0f;
  float avg_ms = 0.0f;
  float median_ms = 0.0f;
  float p95_ms = 0.0f;
  float p99_ms = 0.0f;
  std::uint32_t sample_count = 0;
};

struct ProfileScope {
  std::string name;
  std::chrono::steady_clock::time_point start_time;
  float duration_ms = 0.0f;
  std::string parent_scope;
  std::uint32_t depth = 0;
  std::thread::id thread_id;
};

struct GCStats {
  std::uint32_t count = 0;
  std::uint32_t threshold = 0;
  std::uint32_t step_size = 0;
};

class Profiler {
 public:
  static Profiler& Get();

  bool BeginScope(const std::string& name);
  void EndScope();

  [[nodiscard]] ProfilerStats GetScopeStats(const std::string& name) const;
  [[nodiscard]] std::vector<std::string> GetScopeNames() const;
  [[nodiscard]] std::vector<ProfileScope> GetScopeTree() const;
  [[nodiscard]] std::vector<ProfileScope> GetTopScopes(std::uint32_t n) const;

  void BeginFrame();
  void EndFrame();
  [[nodiscard]] float GetFrameTime() const;
  [[nodiscard]] ProfilerStats GetFrameTimeStats() const;

  [[nodiscard]] float GetGPUTime() const;
  void SetGPUTime(float ms);

  void SetEnabled(bool enabled);
  [[nodiscard]] bool IsEnabled() const;

  [[nodiscard]] std::uint32_t GetHistoryLength() const;
  void SetHistoryLength(std::uint32_t frames);

  [[nodiscard]] std::size_t GetMemoryUsage() const;

  [[nodiscard]] GCStats GetGCStats() const;
  void SetGCStats(const GCStats& stats);

  void Reset();

 private:
  using Duration = std::chrono::nanoseconds;

  struct ScopeData {
    std::deque<Duration> samples;
  };

  struct ActiveScope {
    std::string name;
    std::string parent;
    std::chrono::steady_clock::time_point start;
    std::uint64_t generation = 0;
    std::uint64_t frame = 0;
    std::uint32_t depth = 0;
    bool recording = false;
  };

  Profiler() = default;

  static std::vector<ActiveScope>& ActiveScopes();
  static ProfilerStats ComputeStats(const std::deque<Duration>& samples);

  static constexpr std::uint32_t kDefaultHistory = 300;
  static constexpr std::uint32_t kMaximumHistory = 10000;
  static constexpr std::size_t kMaximumScopeNames = 4096;
  static constexpr std::size_t kMaximumFrameScopes = 16384;

  mutable std::mutex mutex_;
  std::atomic<bool> enabled_{true};
  std::atomic<std::uint64_t> generation_{1};
  std::atomic<std::uint64_t> frame_sequence_{0};
  std::atomic<bool> frame_active_{false};
  std::unordered_map<std::string, ScopeData> scope_data_;
  std::vector<ProfileScope> pending_frame_scopes_;
  std::vector<ProfileScope> completed_frame_scopes_;
  std::chrono::steady_clock::time_point frame_start_{};
  float last_frame_time_ms_ = 0.0f;
  std::deque<Duration> frame_time_history_;
  std::uint32_t max_history_ = kDefaultHistory;
  float gpu_time_ms_ = 0.0f;
  GCStats gc_stats_{};
};

class ScopedProfile {
 public:
  explicit ScopedProfile(const std::string& name)
      : active_(Profiler::Get().BeginScope(name)) {}
  ~ScopedProfile() {
    if (active_) {
      Profiler::Get().EndScope();
    }
  }

  ScopedProfile(const ScopedProfile&) = delete;
  ScopedProfile& operator=(const ScopedProfile&) = delete;

 private:
  bool active_;
};

#if defined(OPENWOW_DISABLE_PROFILING)
#define OPENWOW_PROFILE_SCOPE(name) ((void)0)
#else
#define OPENWOW_PROFILE_SCOPE(name) \
  ::openwow::debug::ScopedProfile _ow_profile_##__LINE__(name)
#endif

}

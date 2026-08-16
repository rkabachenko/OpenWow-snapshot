#include "openwow/debug/diagnostics/profiler.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#if defined(__linux__)
#include <fstream>
#elif defined(__APPLE__)
#include <mach/mach.h>
#elif defined(_WIN32)

#include <windows.h>

#include <psapi.h>
#endif

namespace openwow::debug {
namespace {

float ToMilliseconds(const std::chrono::nanoseconds duration) {
  const auto milliseconds =
      std::chrono::duration<double, std::milli>(duration).count();
  return static_cast<float>(std::min(
      milliseconds, static_cast<double>(std::numeric_limits<float>::max())));
}

}

Profiler& Profiler::Get() {
  static Profiler instance;
  return instance;
}

std::vector<Profiler::ActiveScope>& Profiler::ActiveScopes() {
  thread_local std::vector<ActiveScope> scopes;
  return scopes;
}

ProfilerStats Profiler::ComputeStats(
    const std::deque<Duration>& samples) {
  if (samples.empty()) {
    return {};
  }

  std::vector<Duration> sorted(samples.begin(), samples.end());
  std::sort(sorted.begin(), sorted.end());

  long double sum = 0.0L;
  for (const auto sample : sorted) {
    sum += static_cast<long double>(sample.count());
  }

  const auto percentile = [&sorted](const long double p) {
    if (sorted.size() == 1) {
      return sorted.front();
    }
    const long double index = p * static_cast<long double>(sorted.size() - 1);
    const auto lower = static_cast<std::size_t>(std::floor(index));
    const auto upper = static_cast<std::size_t>(std::ceil(index));
    const long double fraction = index - static_cast<long double>(lower);
    const long double count =
        static_cast<long double>(sorted[lower].count()) * (1.0L - fraction) +
        static_cast<long double>(sorted[upper].count()) * fraction;
    return Duration{static_cast<Duration::rep>(count)};
  };

  ProfilerStats stats;
  stats.min_ms = ToMilliseconds(sorted.front());
  stats.max_ms = ToMilliseconds(sorted.back());
  stats.avg_ms = ToMilliseconds(
      Duration{static_cast<Duration::rep>(sum / sorted.size())});
  stats.median_ms = ToMilliseconds(percentile(0.5L));
  stats.p95_ms = ToMilliseconds(percentile(0.95L));
  stats.p99_ms = ToMilliseconds(percentile(0.99L));
  stats.sample_count = static_cast<std::uint32_t>(sorted.size());
  return stats;
}

bool Profiler::BeginScope(const std::string& name) {
  auto& stack = ActiveScopes();
  const auto generation = generation_.load(std::memory_order_relaxed);
  if (!stack.empty() && stack.back().generation != generation) {
    stack.clear();
  }

  if (!enabled_.load(std::memory_order_relaxed)) {
    if (stack.empty()) {
      return false;
    }
    stack.push_back(ActiveScope{.generation = generation});
    return true;
  }

  const auto start = std::chrono::steady_clock::now();
  const auto depth = static_cast<std::uint32_t>(std::min<std::size_t>(
      stack.size(), std::numeric_limits<std::uint32_t>::max()));
  const std::string parent = stack.empty() ? std::string{} : stack.back().name;
  stack.push_back(ActiveScope{
      .name = name,
      .parent = parent,
      .start = start,
      .generation = generation,
      .frame = frame_active_.load(std::memory_order_relaxed)
                   ? frame_sequence_.load(std::memory_order_relaxed)
                   : 0,
      .depth = depth,
      .recording = true,
  });
  return true;
}

void Profiler::EndScope() {
  auto& stack = ActiveScopes();
  if (stack.empty()) {
    return;
  }

  ActiveScope scope = std::move(stack.back());
  stack.pop_back();
  if (!scope.recording ||
      scope.generation != generation_.load(std::memory_order_relaxed) ||
      !enabled_.load(std::memory_order_relaxed)) {
    return;
  }

  const auto end = std::chrono::steady_clock::now();
  const auto duration =
      std::chrono::duration_cast<Duration>(end - scope.start);
  std::lock_guard lock(mutex_);

  if (scope.generation != generation_.load(std::memory_order_relaxed) ||
      !enabled_.load(std::memory_order_relaxed)) {
    return;
  }

  auto found = scope_data_.find(scope.name);
  if (found == scope_data_.end()) {
    if (scope_data_.size() < kMaximumScopeNames && max_history_ != 0) {
      found = scope_data_.try_emplace(scope.name).first;
    }
  }
  if (found != scope_data_.end()) {
    auto& samples = found->second.samples;
    samples.push_back(duration);
    while (samples.size() > max_history_) {
      samples.pop_front();
    }
  }

  if (scope.frame != 0 && frame_active_.load(std::memory_order_relaxed) &&
      scope.frame == frame_sequence_.load(std::memory_order_relaxed) &&
      pending_frame_scopes_.size() < kMaximumFrameScopes) {
    pending_frame_scopes_.push_back(ProfileScope{
        .name = std::move(scope.name),
        .start_time = scope.start,
        .duration_ms = ToMilliseconds(duration),
        .parent_scope = std::move(scope.parent),
        .depth = scope.depth,
        .thread_id = std::this_thread::get_id(),
    });
  }
}

ProfilerStats Profiler::GetScopeStats(const std::string& name) const {
  std::lock_guard lock(mutex_);
  const auto found = scope_data_.find(name);
  return found == scope_data_.end() ? ProfilerStats{}
                                    : ComputeStats(found->second.samples);
}

std::vector<std::string> Profiler::GetScopeNames() const {
  std::lock_guard lock(mutex_);
  std::vector<std::string> names;
  names.reserve(scope_data_.size());
  for (const auto& [name, data] : scope_data_) {
    (void)data;
    names.push_back(name);
  }
  std::sort(names.begin(), names.end());
  return names;
}

std::vector<ProfileScope> Profiler::GetScopeTree() const {
  std::lock_guard lock(mutex_);
  return completed_frame_scopes_;
}

std::vector<ProfileScope> Profiler::GetTopScopes(const std::uint32_t n) const {
  std::lock_guard lock(mutex_);
  std::vector<ProfileScope> result;
  result.reserve(std::min<std::size_t>(n, scope_data_.size()));
  for (const auto& [name, data] : scope_data_) {
    result.push_back(ProfileScope{
        .name = name,
        .duration_ms = ComputeStats(data.samples).avg_ms,
    });
  }
  std::sort(result.begin(), result.end(),
            [](const ProfileScope& left, const ProfileScope& right) {
              if (left.duration_ms != right.duration_ms) {
                return left.duration_ms > right.duration_ms;
              }
              return left.name < right.name;
            });
  if (result.size() > n) {
    result.resize(n);
  }
  return result;
}

void Profiler::BeginFrame() {
  if (!enabled_.load(std::memory_order_relaxed)) {
    return;
  }
  const auto start = std::chrono::steady_clock::now();
  std::lock_guard lock(mutex_);
  if (!enabled_.load(std::memory_order_relaxed)) {
    return;
  }
  frame_start_ = start;
  pending_frame_scopes_.clear();
  auto next_frame = frame_sequence_.load(std::memory_order_relaxed) + 1;
  if (next_frame == 0) {
    next_frame = 1;
  }
  frame_sequence_.store(next_frame, std::memory_order_relaxed);
  frame_active_.store(true, std::memory_order_release);
}

void Profiler::EndFrame() {
  const auto end = std::chrono::steady_clock::now();
  std::lock_guard lock(mutex_);
  if (!frame_active_.exchange(false, std::memory_order_acq_rel)) {
    return;
  }
  const auto duration =
      std::chrono::duration_cast<Duration>(end - frame_start_);
  last_frame_time_ms_ = ToMilliseconds(duration);
  if (max_history_ != 0) {
    frame_time_history_.push_back(duration);
    while (frame_time_history_.size() > max_history_) {
      frame_time_history_.pop_front();
    }
  }
  std::sort(pending_frame_scopes_.begin(), pending_frame_scopes_.end(),
            [](const ProfileScope& left, const ProfileScope& right) {
              if (left.start_time != right.start_time) {
                return left.start_time < right.start_time;
              }
              if (left.depth != right.depth) {
                return left.depth < right.depth;
              }
              return left.name < right.name;
            });
  completed_frame_scopes_ = std::move(pending_frame_scopes_);
  pending_frame_scopes_.clear();
}

float Profiler::GetFrameTime() const {
  std::lock_guard lock(mutex_);
  return last_frame_time_ms_;
}

ProfilerStats Profiler::GetFrameTimeStats() const {
  std::lock_guard lock(mutex_);
  return ComputeStats(frame_time_history_);
}

float Profiler::GetGPUTime() const {
  std::lock_guard lock(mutex_);
  return gpu_time_ms_;
}

void Profiler::SetGPUTime(const float ms) {
  std::lock_guard lock(mutex_);
  gpu_time_ms_ = std::isfinite(ms) && ms >= 0.0f ? ms : 0.0f;
}

void Profiler::SetEnabled(const bool enabled) {
  if (enabled_.exchange(enabled, std::memory_order_acq_rel) != enabled) {
    std::lock_guard lock(mutex_);
    auto next_generation = generation_.load(std::memory_order_relaxed) + 1;
    if (next_generation == 0) {
      next_generation = 1;
    }
    generation_.store(next_generation, std::memory_order_relaxed);
    if (!enabled) {
      frame_active_.store(false, std::memory_order_release);
      pending_frame_scopes_.clear();
    }
  }
}

bool Profiler::IsEnabled() const {
  return enabled_.load(std::memory_order_relaxed);
}

std::uint32_t Profiler::GetHistoryLength() const {
  std::lock_guard lock(mutex_);
  return static_cast<std::uint32_t>(frame_time_history_.size());
}

void Profiler::SetHistoryLength(const std::uint32_t frames) {
  std::lock_guard lock(mutex_);
  max_history_ = std::min(frames, kMaximumHistory);
  while (frame_time_history_.size() > max_history_) {
    frame_time_history_.pop_front();
  }
  for (auto& [name, data] : scope_data_) {
    (void)name;
    while (data.samples.size() > max_history_) {
      data.samples.pop_front();
    }
  }
  if (max_history_ == 0) {
    scope_data_.clear();
  }
}

std::size_t Profiler::GetMemoryUsage() const {
#if defined(__linux__)
  std::ifstream status("/proc/self/status");
  std::string key;
  std::size_t kilobytes = 0;
  std::string unit;
  while (status >> key) {
    if (key == "VmRSS:") {
      if (status >> kilobytes >> unit &&
          kilobytes <= std::numeric_limits<std::size_t>::max() / 1024) {
        return kilobytes * 1024;
      }
      return 0;
    }
    status.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
  }
  return 0;
#elif defined(__APPLE__)
  mach_task_basic_info info{};
  mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
  return task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                   reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS
             ? static_cast<std::size_t>(info.resident_size)
             : 0;
#elif defined(_WIN32)
  PROCESS_MEMORY_COUNTERS counters{};
  return GetProcessMemoryInfo(GetCurrentProcess(), &counters,
                              sizeof(counters))
             ? static_cast<std::size_t>(counters.WorkingSetSize)
             : 0;
#else
  return 0;
#endif
}

GCStats Profiler::GetGCStats() const {
  std::lock_guard lock(mutex_);
  return gc_stats_;
}

void Profiler::SetGCStats(const GCStats& stats) {
  std::lock_guard lock(mutex_);
  gc_stats_ = stats;
}

void Profiler::Reset() {
  std::lock_guard lock(mutex_);
  auto next_generation = generation_.load(std::memory_order_relaxed) + 1;
  if (next_generation == 0) {
    next_generation = 1;
  }
  generation_.store(next_generation, std::memory_order_relaxed);
  frame_active_.store(false, std::memory_order_release);
  scope_data_.clear();
  pending_frame_scopes_.clear();
  completed_frame_scopes_.clear();
  frame_time_history_.clear();
  frame_start_ = {};
  last_frame_time_ms_ = 0.0f;
  gpu_time_ms_ = 0.0f;
  gc_stats_ = {};
  max_history_ = kDefaultHistory;
  enabled_.store(true, std::memory_order_release);
}

}

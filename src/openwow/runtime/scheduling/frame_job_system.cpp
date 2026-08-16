#include "openwow/runtime/scheduling/frame_job_system.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <utility>

#if defined(__APPLE__)
#include <pthread.h>
#include <sys/sysctl.h>
#elif defined(_WIN32)
#include <windows.h>
#include <processthreadsapi.h>
#elif defined(__linux__)
#include <cstdio>
#include <cstring>
#endif

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace openwow::core {

namespace {

thread_local bool t_is_frame_job_worker = false;

constexpr uint32_t kReservedCores = 2;
constexpr uint32_t kMaxWorkers = FrameJobSystem::kMaxWorkerCeiling;

constexpr std::chrono::steady_clock::duration kSpinBeforeParkDuration =
    std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double, std::micro>(kSpinBeforeParkMicroseconds));

constexpr unsigned kSpinWindowExpiriesToFloor = 4u;
constexpr std::chrono::steady_clock::duration kSpinFloorDuration =
    kSpinBeforeParkDuration / (1 << kSpinWindowExpiriesToFloor);

inline void CpuRelax() noexcept {
#if defined(_MSC_VER)
#if defined(_M_ARM64) || defined(_M_ARM)
  __yield();
#elif defined(_M_X64) || defined(_M_IX86)
  _mm_pause();
#else
  std::this_thread::yield();
#endif
#elif defined(__aarch64__) || defined(__arm64__)
  asm volatile("yield" ::: "memory");
#elif defined(__x86_64__) || defined(__i386__)
  asm volatile("pause" ::: "memory");
#else
  std::this_thread::yield();
#endif
}

#if defined(_MSC_VER)
#if defined(_M_ARM64) || defined(_M_ARM) || defined(_M_X64) || defined(_M_IX86)
constexpr bool kCpuRelaxIsHintInstruction = true;
#else
constexpr bool kCpuRelaxIsHintInstruction = false;
#endif
#elif defined(__aarch64__) || defined(__arm64__) || defined(__x86_64__) || \
    defined(__i386__)
constexpr bool kCpuRelaxIsHintInstruction = true;
#else
constexpr bool kCpuRelaxIsHintInstruction = false;
#endif

constexpr unsigned kSpinIterationsPerHintClockSample = 32u;

constexpr unsigned kSpinIterationsPerClockSample =
    kCpuRelaxIsHintInstruction ? kSpinIterationsPerHintClockSample : 1u;

class SpinWindow {
 public:
  explicit SpinWindow(const std::chrono::steady_clock::duration window) noexcept
      : deadline_(std::chrono::steady_clock::now() + window) {}

  [[nodiscard]] bool Relax() noexcept {
    CpuRelax();
    if (--iterations_until_clock_sample_ != 0u) {
      return true;
    }
    iterations_until_clock_sample_ = kSpinIterationsPerClockSample;
    return std::chrono::steady_clock::now() < deadline_;
  }

 private:
  std::chrono::steady_clock::time_point deadline_;
  unsigned iterations_until_clock_sample_ = kSpinIterationsPerClockSample;
};

class AdaptiveSpinBudget {
 public:
  [[nodiscard]] std::chrono::steady_clock::duration window() const noexcept {
    return window_;
  }

  void NoteWaitEndedInsideWindow() noexcept { window_ = kSpinBeforeParkDuration; }

  void NoteWaitOutlastedWindow() noexcept {
    window_ = std::max(kSpinFloorDuration, window_ / 2);
  }

 private:
  std::chrono::steady_clock::duration window_ = kSpinBeforeParkDuration;
};

thread_local AdaptiveSpinBudget t_wait_group_spin_budget;

constexpr std::size_t kTargetGrainsPerParticipant = 16;

[[nodiscard]] uint32_t PerformanceCoreCount() noexcept {
#if defined(__APPLE__)

  uint32_t cores = 0;
  std::size_t size = sizeof(cores);
  if (sysctlbyname("hw.perflevel0.logicalcpu", &cores, &size, nullptr, 0) == 0) {
    return cores;
  }
#elif defined(_WIN32)

  DWORD bytes = 0;
  const HANDLE process = GetCurrentProcess();
  if (GetSystemCpuSetInformation(nullptr, 0, &bytes, process, 0) == FALSE &&
      GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
    return 0u;
  }
  if (bytes == 0) {
    return 0u;
  }
  std::vector<std::uint8_t> buffer(bytes);
  if (GetSystemCpuSetInformation(
          reinterpret_cast<SYSTEM_CPU_SET_INFORMATION*>(buffer.data()), bytes,
          &bytes, process, 0) == FALSE) {
    return 0u;
  }
  BYTE best_class = 0;
  uint32_t best_count = 0;
  for (DWORD offset = 0; offset + sizeof(SYSTEM_CPU_SET_INFORMATION) <= bytes;) {
    const auto* const info =
        reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(buffer.data() + offset);
    if (info->Size == 0) {
      break;
    }
    if (info->Type == CpuSetInformation) {
      const BYTE efficiency_class = info->CpuSet.EfficiencyClass;
      if (efficiency_class > best_class) {
        best_class = efficiency_class;
        best_count = 1u;
      } else if (efficiency_class == best_class) {
        ++best_count;
      }
    }
    offset += info->Size;
  }
  if (best_count != 0u) {
    return best_count;
  }
#elif defined(__linux__)

  const auto count_cpu_list = [](const char* path) -> uint32_t {
    std::FILE* const file = std::fopen(path, "re");
    if (file == nullptr) {
      return 0u;
    }
    char text[512] = {};
    const std::size_t read = std::fread(text, 1, sizeof(text) - 1, file);
    std::fclose(file);
    if (read == 0) {
      return 0u;
    }

    uint32_t total = 0;
    const char* cursor = text;
    while (*cursor != '\0') {
      char* end = nullptr;
      const long first = std::strtol(cursor, &end, 10);
      if (end == cursor) {
        break;
      }
      long last = first;
      if (*end == '-') {
        cursor = end + 1;
        last = std::strtol(cursor, &end, 10);
      }
      if (last >= first) {
        total += static_cast<uint32_t>(last - first + 1);
      }
      if (*end != ',') {
        break;
      }
      cursor = end + 1;
    }
    return total;
  };

  if (const uint32_t intel_hybrid = count_cpu_list("/sys/devices/cpu_core/cpus");
      intel_hybrid != 0u) {
    return intel_hybrid;
  }

  unsigned long best_capacity = 0;
  uint32_t best_count = 0;
  for (uint32_t cpu = 0; cpu < 512u; ++cpu) {
    char path[128];
    std::snprintf(path, sizeof(path),
                  "/sys/devices/system/cpu/cpu%u/cpu_capacity", cpu);
    std::FILE* const file = std::fopen(path, "re");
    if (file == nullptr) {

      if (cpu == 0) {
        return 0u;
      }
      break;
    }
    unsigned long capacity = 0;
    const int scanned = std::fscanf(file, "%lu", &capacity);
    std::fclose(file);
    if (scanned != 1) {
      continue;
    }
    if (capacity > best_capacity) {
      best_capacity = capacity;
      best_count = 1u;
    } else if (capacity == best_capacity) {
      ++best_count;
    }
  }
  return best_count;
#endif

  return 0u;
}

void RequestPerformanceCoreScheduling() noexcept {
#if defined(__APPLE__)

  pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#elif defined(_WIN32)

  THREAD_POWER_THROTTLING_STATE state = {};
  state.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
  state.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
  state.StateMask = 0;
  (void)SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &state,
                             sizeof(state));
#else

#endif
}
}

void FrameWaitGroup::Wait() {

  if (remaining_.load(std::memory_order_acquire) == 0) {
    return;
  }
  SpinWindow spin_window(t_wait_group_spin_budget.window());
  while (spin_window.Relax()) {
    if (remaining_.load(std::memory_order_acquire) == 0) {
      t_wait_group_spin_budget.NoteWaitEndedInsideWindow();
      return;
    }
  }
  t_wait_group_spin_budget.NoteWaitOutlastedWindow();
  std::unique_lock<std::mutex> lock(mutex_);
  cv_.wait(lock, [this] { return remaining_.load(std::memory_order_acquire) == 0; });
}

bool FrameJobSystem::IsCurrentThreadWorker() noexcept {
  return t_is_frame_job_worker;
}

uint32_t FrameJobSystem::ResolveDefaultWorkerCount() {
  const uint32_t performance_cores = PerformanceCoreCount();
  const uint32_t hw = performance_cores != 0u
                          ? performance_cores
                          : std::thread::hardware_concurrency();
  const uint32_t available = hw > kReservedCores ? hw - kReservedCores : 1u;
  return std::min(available, kMaxWorkers);
}

void FrameJobSystem::Initialize(uint32_t worker_count) {
  if (initialized_) {
    return;
  }
  if (worker_count == 0) {
    worker_count = ResolveDefaultWorkerCount();
  }
  stopping_.store(false, std::memory_order_relaxed);
  queue_size_.store(0, std::memory_order_relaxed);
  workers_.reserve(worker_count);
  for (uint32_t i = 0; i < worker_count; ++i) {
    workers_.emplace_back([this] {
      t_is_frame_job_worker = true;
      RequestPerformanceCoreScheduling();
      WorkerLoop();
    });
  }
  initialized_ = true;
}

void FrameJobSystem::Shutdown() {
  if (!initialized_) {
    return;
  }
  {

    std::lock_guard<std::mutex> lock(queue_mutex_);
    stopping_.store(true, std::memory_order_relaxed);
  }
  queue_cv_.notify_all();
  for (auto& t : workers_) {
    if (t.joinable()) {
      t.join();
    }
  }
  workers_.clear();
  initialized_ = false;
}

void FrameJobSystem::WorkerLoop() {

  AdaptiveSpinBudget spin_budget;
  for (;;) {
    Job job;

    enum class PollResult : std::uint8_t { kTookJob, kEmpty, kStopping };
    const auto poll = [&]() -> PollResult {
      if (queue_size_.load(std::memory_order_acquire) == 0 &&
          !stopping_.load(std::memory_order_acquire)) {
        return PollResult::kEmpty;
      }
      std::unique_lock<std::mutex> lock(queue_mutex_, std::try_to_lock);
      if (!lock.owns_lock()) {
        return PollResult::kEmpty;
      }
      if (!queue_.empty()) {
        job = std::move(queue_.front());
        queue_.pop_front();
        queue_size_.store(queue_.size(), std::memory_order_release);
        return PollResult::kTookJob;
      }

      return stopping_.load(std::memory_order_relaxed) ? PollResult::kStopping
                                                       : PollResult::kEmpty;
    };

    PollResult result = poll();

    if (result == PollResult::kEmpty) {
      SpinWindow spin_window(spin_budget.window());
      while (spin_window.Relax()) {
        result = poll();
        if (result != PollResult::kEmpty) {
          break;
        }
      }
      if (result == PollResult::kEmpty) {
        spin_budget.NoteWaitOutlastedWindow();
      } else {
        spin_budget.NoteWaitEndedInsideWindow();
      }
    }

    if (result == PollResult::kStopping) {
      return;
    }
    if (result == PollResult::kEmpty) {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      queue_cv_.wait(lock, [this] {
        return stopping_.load(std::memory_order_relaxed) || !queue_.empty();
      });
      if (queue_.empty()) {

        return;
      }
      job = std::move(queue_.front());
      queue_.pop_front();
      queue_size_.store(queue_.size(), std::memory_order_release);
    }

    job.batch->wait_group.MarkDone(DrainBatch(*job.batch));
  }
}

std::size_t FrameJobSystem::DrainBatch(RangeBatch& batch) {
  std::size_t executed = 0;
  for (;;) {
    const std::size_t begin =
        batch.cursor.fetch_add(batch.grain, std::memory_order_relaxed);
    if (begin >= batch.count) {
      break;
    }
    batch.fn(begin, std::min(begin + batch.grain, batch.count));
    ++executed;
  }
  return executed;
}

std::shared_ptr<FrameJobSystem::RangeBatch> FrameJobSystem::DispatchRange(
    std::size_t count, std::function<void(std::size_t begin, std::size_t end)> fn,
    bool caller_participates, std::shared_ptr<FrameWaitGroup>* out_wait_group) {

  auto batch = std::make_shared<RangeBatch>();
  *out_wait_group = std::shared_ptr<FrameWaitGroup>(batch, &batch->wait_group);
  if (count == 0) {
    return nullptr;
  }

  const uint32_t worker_count = WorkerCount();
  if (!initialized_ || worker_count == 0) {

    fn(0, count);
    return nullptr;
  }

  const std::size_t participants =
      static_cast<std::size_t>(worker_count) + (caller_participates ? 1u : 0u);
  batch->fn = std::move(fn);
  batch->count = count;
  batch->grain = std::max<std::size_t>(
      1, count / (participants * kTargetGrainsPerParticipant));

  const std::size_t grains = (count + batch->grain - 1) / batch->grain;
  const std::size_t job_count = std::min<std::size_t>(worker_count, grains);

  batch->wait_group.Add(grains);
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    for (std::size_t i = 0; i < job_count; ++i) {
      queue_.push_back(Job{batch});
    }
    queue_size_.store(queue_.size(), std::memory_order_release);
  }

  if (job_count >= worker_count) {
    queue_cv_.notify_all();
  } else {
    for (std::size_t i = 0; i < job_count; ++i) {
      queue_cv_.notify_one();
    }
  }
  return batch;
}

std::shared_ptr<FrameWaitGroup> FrameJobSystem::SubmitRange(
    std::size_t count, std::function<void(std::size_t begin, std::size_t end)> fn) {
  std::shared_ptr<FrameWaitGroup> wait_group;
  (void)DispatchRange(count, std::move(fn), false,
                      &wait_group);
  return wait_group;
}

void FrameJobSystem::ParallelFor(
    std::size_t count, std::function<void(std::size_t begin, std::size_t end)> fn) {
  std::shared_ptr<FrameWaitGroup> wait_group;
  const auto batch =
      DispatchRange(count, std::move(fn), true, &wait_group);

  if (batch != nullptr) {
    batch->wait_group.MarkDone(DrainBatch(*batch));
  }
  wait_group->Wait();
}

}

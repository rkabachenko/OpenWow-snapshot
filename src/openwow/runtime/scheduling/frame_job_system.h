#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace openwow::core {

inline constexpr double kSpinBeforeParkMicroseconds = 50.0;

inline constexpr double kParallelDispatchOverheadMicroseconds = 50.0;

[[nodiscard]] constexpr std::size_t ParallelDispatchBreakEven(
    const std::uint32_t participants,
    const double per_item_microseconds) noexcept {

  if (participants <= 1u || !(per_item_microseconds > 0.0)) {
    return std::numeric_limits<std::size_t>::max();
  }
  const double parallel_fraction =
      1.0 - 1.0 / static_cast<double>(participants);

  return static_cast<std::size_t>(
             kParallelDispatchOverheadMicroseconds /
             (per_item_microseconds * parallel_fraction)) +
         1u;
}

class FrameWaitGroup {
 public:

  void Wait();

 private:
  friend class FrameJobSystem;

  void Add(std::size_t count) {
    remaining_.fetch_add(count, std::memory_order_relaxed);
  }

  void MarkDone(std::size_t count) {
    if (count == 0) {

      return;
    }
    if (remaining_.fetch_sub(count, std::memory_order_acq_rel) == count) {
      std::lock_guard<std::mutex> lock(mutex_);
      cv_.notify_all();
    }
  }

  std::atomic<std::size_t> remaining_{0};
  std::mutex mutex_;
  std::condition_variable cv_;
};

class FrameJobSystem {
 public:
  FrameJobSystem() = default;
  ~FrameJobSystem() { Shutdown(); }

  FrameJobSystem(const FrameJobSystem&) = delete;
  FrameJobSystem& operator=(const FrameJobSystem&) = delete;

  static constexpr uint32_t kMaxWorkerCeiling = 16u;

  [[nodiscard]] static uint32_t ResolveDefaultWorkerCount();

  [[nodiscard]] static bool IsCurrentThreadWorker() noexcept;

  void Initialize(uint32_t worker_count = 0);

  void Shutdown();

  [[nodiscard]] bool IsInitialized() const { return initialized_; }
  [[nodiscard]] uint32_t WorkerCount() const {
    return static_cast<uint32_t>(workers_.size());
  }

  [[nodiscard]] std::shared_ptr<FrameWaitGroup> SubmitRange(
      std::size_t count, std::function<void(std::size_t begin, std::size_t end)> fn);

  void ParallelFor(std::size_t count,
                    std::function<void(std::size_t begin, std::size_t end)> fn);

 private:

  struct RangeBatch {
    FrameWaitGroup wait_group;
    std::function<void(std::size_t begin, std::size_t end)> fn;
    std::size_t count{0};
    std::size_t grain{1};
    std::atomic<std::size_t> cursor{0};
  };

  struct Job {
    std::shared_ptr<RangeBatch> batch;
  };

  [[nodiscard]] static std::size_t DrainBatch(RangeBatch& batch);

  std::shared_ptr<RangeBatch> DispatchRange(
      std::size_t count, std::function<void(std::size_t begin, std::size_t end)> fn,
      bool caller_participates, std::shared_ptr<FrameWaitGroup>* out_wait_group);

  void WorkerLoop();

  std::mutex queue_mutex_;
  std::condition_variable queue_cv_;
  std::deque<Job> queue_;
  std::vector<std::thread> workers_;

  std::atomic<std::size_t> queue_size_{0};
  bool initialized_ = false;

  std::atomic<bool> stopping_{false};
};

}

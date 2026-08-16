#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <mutex>
#include <thread>

namespace openwow::foundation {

class RecursiveMutex {
 public:
  RecursiveMutex() noexcept = default;
  ~RecursiveMutex() = default;

  RecursiveMutex(const RecursiveMutex&) = delete;
  RecursiveMutex& operator=(const RecursiveMutex&) = delete;

  void lock() {
    const std::thread::id self = std::this_thread::get_id();
    if (owner_.load(std::memory_order_relaxed) == self) {
      ++depth_;
      return;
    }

    gate_.lock();
    owner_.store(self, std::memory_order_relaxed);
    depth_ = 1u;
  }

  [[nodiscard]] bool try_lock() {
    const std::thread::id self = std::this_thread::get_id();
    if (owner_.load(std::memory_order_relaxed) == self) {

      ++depth_;
      return true;
    }
    if (!gate_.try_lock()) {
      return false;
    }
    owner_.store(self, std::memory_order_relaxed);
    depth_ = 1u;
    return true;
  }

  void unlock() {
    assert(owner_.load(std::memory_order_relaxed) ==
               std::this_thread::get_id() &&
           "RecursiveMutex::unlock from a thread that does not own it");
    assert(depth_ != 0u && "RecursiveMutex::unlock without a matching lock");
    if (--depth_ != 0u) {
      return;
    }
    owner_.store(std::thread::id{}, std::memory_order_relaxed);
    gate_.unlock();
  }

 private:
  std::mutex gate_;

  std::atomic<std::thread::id> owner_{};

  std::size_t depth_{0u};
};

static_assert(std::atomic<std::thread::id>::is_always_lock_free,
              "RecursiveMutex assumes a lock-free std::atomic<thread::id>; a "
              "platform where it is not would put a mutex inside the mutex. "
              "Substitute a thread_local-anchor address for the owner tag "
              "there rather than paying that.");

}

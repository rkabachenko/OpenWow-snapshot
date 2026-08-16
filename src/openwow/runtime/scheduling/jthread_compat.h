#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>

namespace openwow::core {

class stop_token;
class stop_source;

namespace detail {
struct StopState {
  std::atomic<bool> requested_{false};
  std::mutex mutex_;
  std::condition_variable cv_;
  std::atomic<int> ref_count_{2};

  void request() {
    if (!requested_.exchange(true)) {
      std::lock_guard<std::mutex> lk(mutex_);
      cv_.notify_all();
    }
  }

  void add_ref() { ref_count_.fetch_add(1, std::memory_order_relaxed); }
  void release() {
    if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      delete this;
    }
  }
};
}

class stop_token {
 public:
  stop_token() noexcept = default;

  stop_token(const stop_token& other) noexcept : state_(other.state_) {
    if (state_) state_->add_ref();
  }

  stop_token(stop_token&& other) noexcept : state_(std::exchange(other.state_, nullptr)) {}

  stop_token& operator=(const stop_token& other) noexcept {
    if (this != &other) {
      if (state_) state_->release();
      state_ = other.state_;
      if (state_) state_->add_ref();
    }
    return *this;
  }

  stop_token& operator=(stop_token&& other) noexcept {
    if (state_) state_->release();
    state_ = std::exchange(other.state_, nullptr);
    return *this;
  }

  ~stop_token() {
    if (state_) state_->release();
  }

  [[nodiscard]] bool stop_requested() const noexcept {
    return state_ && state_->requested_.load(std::memory_order_acquire);
  }

  [[nodiscard]] bool stop_possible() const noexcept { return state_ != nullptr; }

  void swap(stop_token& other) noexcept { std::swap(state_, other.state_); }

  friend class stop_source;

 private:
  explicit stop_token(detail::StopState* s) : state_(s) {}
  detail::StopState* state_{nullptr};
};

class stop_source {
 public:
  stop_source() : state_(new detail::StopState) {}
  ~stop_source() { if (state_) state_->release(); }

  stop_source(const stop_source& other) noexcept : state_(other.state_) {
    if (state_) state_->add_ref();
  }

  stop_source(stop_source&& other) noexcept : state_(std::exchange(other.state_, nullptr)) {}

  stop_source& operator=(const stop_source& other) noexcept {
    if (this != &other) {
      if (state_) state_->release();
      state_ = other.state_;
      if (state_) state_->add_ref();
    }
    return *this;
  }

  stop_source& operator=(stop_source&& other) noexcept {
    if (state_) state_->release();
    state_ = std::exchange(other.state_, nullptr);
    return *this;
  }

  [[nodiscard]] stop_token get_token() const noexcept { return stop_token(state_); }
  [[nodiscard]] bool stop_requested() const noexcept {
    return state_ && state_->requested_.load(std::memory_order_acquire);
  }
  bool request_stop() noexcept {
    if (!state_) return false;
    bool was = state_->requested_.exchange(true);
    if (!was) {
      std::lock_guard<std::mutex> lk(state_->mutex_);
      state_->cv_.notify_all();
    }
    return !was;
  }

 private:
  detail::StopState* state_{nullptr};
};

inline void swap(stop_token& a, stop_token& b) noexcept { a.swap(b); }

class JthreadCompat {
 public:
  JthreadCompat() noexcept = default;

  ~JthreadCompat() {
    if (joinable_) {
      request_stop();
      join();
    }
  }

  template <typename Callable, typename... Args,
           typename = std::enable_if_t<
               std::is_constructible_v<std::thread, Callable, Args...>>>
   JthreadCompat(Callable&& f, Args&&... args)
      : stop_source_(std::make_shared<stop_source>()) {
    auto src = stop_source_;
    thread_ = std::thread(
        [src](auto fn, auto... a) {
          auto tok = src->get_token();
          if constexpr (std::is_invocable_v<decltype(fn), stop_token, decltype(a)...>) {
            std::invoke(std::move(fn), std::move(tok), std::move(a)...);
          } else {
            std::invoke(std::move(fn), std::move(a)...);
          }
        },
        std::forward<Callable>(f), std::forward<Args>(args)...);
    joinable_ = true;
  }

  JthreadCompat(const JthreadCompat&) = delete;
  JthreadCompat& operator=(const JthreadCompat&) = delete;

  JthreadCompat(JthreadCompat&& other) noexcept
      : thread_(std::exchange(other.thread_, {})),
        stop_source_(std::move(other.stop_source_)),
        joinable_(std::exchange(other.joinable_, false)) {}

  JthreadCompat& operator=(JthreadCompat&& other) noexcept {
    if (joinable_) {
      request_stop();
      join();
    }
    thread_ = std::exchange(other.thread_, {});
    stop_source_ = std::move(other.stop_source_);
    joinable_ = std::exchange(other.joinable_, false);
    return *this;
  }

  void request_stop() {
    if (stop_source_) stop_source_->request_stop();
  }

  [[nodiscard]] bool joinable() const { return joinable_; }

  void join() {
    if (joinable_) {
      thread_.join();
      joinable_ = false;
    }
  }

  void detach() {
    thread_.detach();
    joinable_ = false;
  }

  [[nodiscard]] stop_token get_stop_token() const {
    return stop_source_ ? stop_source_->get_token() : stop_token{};
  }

  [[nodiscard]] stop_source get_stop_source() const {
    return stop_source_ ? *stop_source_ : stop_source{};
  }

 private:
  std::thread thread_{};
  std::shared_ptr<stop_source> stop_source_{};
  bool joinable_{false};
};

}

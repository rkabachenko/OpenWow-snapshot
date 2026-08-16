#pragma once

#include <chrono>
#include <future>
#include <functional>
#include <optional>
#include <utility>

namespace openwow::audio {

class StartupAudioDevicePrepare {
 public:
  struct Result {
    bool succeeded{false};
    std::chrono::steady_clock::duration elapsed{};
  };

  StartupAudioDevicePrepare() = default;
  ~StartupAudioDevicePrepare() { (void)Finish(); }

  StartupAudioDevicePrepare(const StartupAudioDevicePrepare&) = delete;
  StartupAudioDevicePrepare& operator=(const StartupAudioDevicePrepare&) = delete;

  [[nodiscard]] bool Begin(std::function<bool()> prepare) {
    if (!prepare || future_.valid() || completed_.has_value()) {
      return false;
    }

    started_at_ = std::chrono::steady_clock::now();
    try {
      future_ = std::async(std::launch::async,
                           [prepare = std::move(prepare)]() mutable {
                             try {
                               return prepare();
                             } catch (...) {
                               return false;
                             }
                           });
    } catch (...) {
      return false;
    }
    return true;
  }

  [[nodiscard]] std::optional<Result> Finish() {
    if (completed_.has_value()) {
      return completed_;
    }
    if (!future_.valid()) {
      return std::nullopt;
    }

    const bool succeeded = future_.get();
    completed_ = Result{
        .succeeded = succeeded,
        .elapsed = std::chrono::steady_clock::now() - started_at_,
    };
    return completed_;
  }

  [[nodiscard]] bool pending() const noexcept {
    return future_.valid() && !completed_.has_value();
  }

 private:
  std::future<bool> future_;
  std::optional<Result> completed_;
  std::chrono::steady_clock::time_point started_at_{};
};

}

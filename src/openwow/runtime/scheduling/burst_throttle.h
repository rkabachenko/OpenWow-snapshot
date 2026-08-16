#pragma once

#include <cstdint>

namespace openwow::core {

class IdaBurstThrottle {
 public:
  [[nodiscard]] constexpr bool TryConsume(const double now_seconds,
                                          const std::uint32_t free_attempts,
                                          const double reset_window_seconds) noexcept {
    ++attempt_count_;
    if (attempt_count_ <= free_attempts) {
      return true;
    }

    if (now_seconds - window_anchor_seconds_ >= reset_window_seconds) {
      window_anchor_seconds_ = now_seconds;
      attempt_count_ = 0;
      return true;
    }

    return false;
  }

  constexpr void Reset() noexcept {
    attempt_count_ = 0;
    window_anchor_seconds_ = 0.0;
  }

 private:
  std::uint32_t attempt_count_ = 0;
  double window_anchor_seconds_ = 0.0;
};

}

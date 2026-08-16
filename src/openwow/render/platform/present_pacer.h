#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>

namespace openwow::render {

struct PresentPacerDecision {
  std::optional<std::uint32_t> target_fps;
  std::uint32_t delay_ms{0};
  std::chrono::steady_clock::time_point deadline{};
};

class PresentPacer {
 public:
  using Clock = std::chrono::steady_clock;

  [[nodiscard]] PresentPacerDecision Schedule(std::int32_t max_fps,
                                              std::int32_t max_background_fps,
                                              bool window_focused,
                                              Clock::time_point now) {
    const auto target_fps =
        SelectCap(NormalizeCap(max_fps), NormalizeCap(max_background_fps), window_focused);
    if (!target_fps.has_value()) {

      return {};
    }

    const auto target_frame = std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(1.0 / static_cast<double>(*target_fps)));

    constexpr Clock::duration kMaxFutureDeadline = std::chrono::seconds(1);
    Clock::duration remaining = Clock::duration::zero();
    if (next_present_deadline_ > now) {
      const Clock::duration gap = next_present_deadline_ - now;
      if (gap < kMaxFutureDeadline) {
        remaining = gap;
      }
    }
    const Clock::time_point deadline = now + remaining;

    constexpr std::chrono::milliseconds kSdlDelayOvershootMargin{1};
    const auto remaining_ms = std::chrono::duration_cast<std::chrono::milliseconds>(remaining);
    const std::uint32_t delay_ms =
        remaining_ms > kSdlDelayOvershootMargin
            ? static_cast<std::uint32_t>((remaining_ms - kSdlDelayOvershootMargin).count())
            : 0u;

    next_present_deadline_ = deadline + target_frame;
    return {.target_fps = target_fps, .delay_ms = delay_ms, .deadline = deadline};
  }

  void Reset() { next_present_deadline_ = Clock::time_point{}; }

  [[nodiscard]] static std::optional<std::uint32_t> NormalizeCap(std::int32_t fps) {
    if (fps <= 0) {
      return std::nullopt;
    }
    return std::max<std::uint32_t>(8u, static_cast<std::uint32_t>(fps));
  }

  [[nodiscard]] static std::optional<std::uint32_t> SelectCap(
      std::optional<std::uint32_t> max_fps,
      std::optional<std::uint32_t> max_background_fps,
      bool window_focused) {
    if (window_focused) {
      return max_fps;
    }
    if (max_fps.has_value() && max_background_fps.has_value()) {
      return std::min(*max_fps, *max_background_fps);
    }
    return max_fps.has_value() ? max_fps : max_background_fps;
  }

 private:
  Clock::time_point next_present_deadline_{};
};

}

#pragma once

#include <cstdint>

namespace openwow::ui::widgets {

enum class StatusBarRangeError : std::uint8_t {
  None,
  EndpointOutOfRange,
  SpanTooLarge,
};

inline constexpr float kStatusBarRangeLimit = 1.0e12F;
[[nodiscard]] constexpr StatusBarRangeError ValidateStatusBarRange(
    const float minimum, const float maximum) noexcept {
  if (minimum < -kStatusBarRangeLimit || minimum > kStatusBarRangeLimit ||
      maximum < -kStatusBarRangeLimit || maximum > kStatusBarRangeLimit) {
    return StatusBarRangeError::EndpointOutOfRange;
  }
  return maximum - minimum > kStatusBarRangeLimit
             ? StatusBarRangeError::SpanTooLarge
             : StatusBarRangeError::None;
}

struct StatusBarRangeChange {
  bool range_changed{false};
  bool reapply_value{false};
};

struct StatusBarValueSnapshot {
  float minimum{0.0F};
  float maximum{0.0F};
  float value{0.0F};
  bool has_range{false};
  bool has_value{false};
};

class StatusBarValueState final {
 public:
  [[nodiscard]] StatusBarRangeChange SetRange(float minimum,
                                               float maximum) noexcept;
  [[nodiscard]] bool SetValue(float value) noexcept;
  [[nodiscard]] StatusBarValueSnapshot Snapshot() const noexcept;

 private:
  [[nodiscard]] float ClampToRange(float value) const noexcept;

  float minimum_{0.0F};
  float maximum_{0.0F};
  float value_{0.0F};
  bool has_range_{false};
  bool has_value_{false};
};

}

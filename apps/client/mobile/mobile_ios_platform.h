#pragma once

#include <cstdint>

namespace openwow::client::mobile {

struct SafeAreaInsetsPoints {
  float left{0.0F};
  float top{0.0F};
  float right{0.0F};
  float bottom{0.0F};
};

enum class HapticFeedback {
  kSelection,
  kLightImpact,
};

// native_window is the UIWindow returned by WindowManager on iOS. This bridge
// deliberately exposes platform metrics, not UIKit views, to the game UI.
[[nodiscard]] SafeAreaInsetsPoints QuerySafeAreaInsetsPoints(
    void* native_window) noexcept;
void PerformHapticFeedback(HapticFeedback feedback) noexcept;

// Main-thread UI operations. The export owns its temporary copy until the
// system share sheet completes; it never retains a game UI or Lua reference.
void PresentLogExport(void* native_window);
[[nodiscard]] bool IsLogExportActive() noexcept;

struct ProcessPerformanceMetrics {
  std::int64_t physical_footprint_bytes{-1};
  int memory_query_status{0};
  int thermal_state{0};
  bool low_power_mode{false};
};

[[nodiscard]] ProcessPerformanceMetrics QueryProcessPerformanceMetrics() noexcept;

}

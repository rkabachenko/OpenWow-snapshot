#include "mobile_ios_platform.h"

#import <UIKit/UIKit.h>
#include <mach/mach.h>

namespace openwow::client::mobile {

ProcessPerformanceMetrics QueryProcessPerformanceMetrics() noexcept {
  ProcessPerformanceMetrics result;
  task_vm_info_data_t memory{};
  mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
  result.memory_query_status = task_info(
      mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&memory), &count);
  if (result.memory_query_status == KERN_SUCCESS) {
    result.physical_footprint_bytes = static_cast<std::int64_t>(memory.phys_footprint);
  }
  @autoreleasepool {
    result.thermal_state = static_cast<int>(NSProcessInfo.processInfo.thermalState);
    result.low_power_mode = NSProcessInfo.processInfo.lowPowerModeEnabled;
  }
  return result;
}

SafeAreaInsetsPoints QuerySafeAreaInsetsPoints(void* native_window) noexcept {
  if (native_window == nullptr) {
    return {};
  }

  @autoreleasepool {
    UIWindow* window = (__bridge UIWindow*)native_window;
    UIView* safe_area_view = window.rootViewController.view;
    if (safe_area_view == nil) {
      safe_area_view = window;
    }
    [safe_area_view layoutIfNeeded];
    const UIEdgeInsets insets = safe_area_view.safeAreaInsets;
    return {
        .left = static_cast<float>(insets.left),
        .top = static_cast<float>(insets.top),
        .right = static_cast<float>(insets.right),
        .bottom = static_cast<float>(insets.bottom),
    };
  }
}

void PerformHapticFeedback(const HapticFeedback feedback) noexcept {
  @autoreleasepool {
    switch (feedback) {
      case HapticFeedback::kSelection: {
        static UISelectionFeedbackGenerator* generator =
            [[UISelectionFeedbackGenerator alloc] init];
        [generator selectionChanged];
        [generator prepare];
        break;
      }
      case HapticFeedback::kLightImpact: {
        static UIImpactFeedbackGenerator* generator =
            [[UIImpactFeedbackGenerator alloc]
                initWithStyle:UIImpactFeedbackStyleLight];
        [generator impactOccurred];
        [generator prepare];
        break;
      }
    }
  }
}

}


#pragma once

#include <cmath>

namespace openwow::ui {

enum class UiCoordinateSpace {

  kUiUnits,

  kDevicePixels,
};

inline constexpr float kUiScriptScreenUnitHeight = 768.0F;

struct DevicePixelsPerUiUnit {
  float value{1.0F};
};

[[nodiscard]] inline DevicePixelsPerUiUnit ResolveDevicePixelsPerUiUnit(
    const float viewport_height, const float effective_scale) noexcept {
  const float viewport_term =
      viewport_height > 0.0F ? viewport_height / kUiScriptScreenUnitHeight
                             : 1.0F;
  const float scale_term = effective_scale > 0.0F ? effective_scale : 1.0F;
  const float product = viewport_term * scale_term;
  return DevicePixelsPerUiUnit{
      std::isfinite(product) && product > 0.0F ? product : 1.0F};
}

[[nodiscard]] inline float UiUnitsToDevicePixels(
    const float ui_units, const DevicePixelsPerUiUnit scale) noexcept {
  return ui_units * scale.value;
}

[[nodiscard]] inline float DevicePixelsToUiUnits(
    const float device_pixels, const DevicePixelsPerUiUnit scale) noexcept {
  return device_pixels / scale.value;
}

template <UiCoordinateSpace Space>
struct UiEdgeRect {
  float left{0.0F};
  float top{0.0F};
  float right{0.0F};
  float bottom{0.0F};
};

using DevicePixelEdgeRect = UiEdgeRect<UiCoordinateSpace::kDevicePixels>;

template <UiCoordinateSpace Space>
struct UiPoint {
  float x{0.0F};
  float y{0.0F};
};

using DevicePixelPoint = UiPoint<UiCoordinateSpace::kDevicePixels>;

struct UiUnitHitInsets {
  float top{0.0F};
  float bottom{0.0F};
  float left{0.0F};
  float right{0.0F};
};

[[nodiscard]] inline bool IsCursorInsideHitRect(
    const DevicePixelEdgeRect& rect, const DevicePixelPoint cursor,
    const UiUnitHitInsets& insets, const DevicePixelsPerUiUnit scale) noexcept {
  const float left = rect.left + UiUnitsToDevicePixels(insets.left, scale);
  const float right = rect.right + UiUnitsToDevicePixels(insets.right, scale);
  const float top = rect.top - UiUnitsToDevicePixels(insets.top, scale);
  const float bottom =
      rect.bottom - UiUnitsToDevicePixels(insets.bottom, scale);
  return left < cursor.x && cursor.x < right && top < cursor.y &&
         cursor.y < bottom;
}

}

#pragma once

#include "openwow/foundation/math/client_rounding.h"
#include "openwow/ui/ui_aspect_scales.h"

#include <cmath>

namespace openwow::ui {

inline constexpr float kLegacyUiPixelSnapEpsilon = 0.00000023841858f;

namespace detail {

inline float GetCurrentViewportHeightForPixelSnap() {
  return 1.0f;
}

template <typename RemoveStretch, typename ApplyStretch>
inline float LegacyPixelSnapUiCoordinate(const float value, RemoveStretch&& remove_stretch,
                                         ApplyStretch&& apply_stretch) {
  if (std::fabs(value) < kLegacyUiPixelSnapEpsilon) {
    return value;
  }

  const float viewport_height = GetCurrentViewportHeightForPixelSnap();
  if (std::fabs(viewport_height) < kLegacyUiPixelSnapEpsilon) {
    return value;
  }

  const float unstretched = remove_stretch(value);
  const float snapped_pixels = static_cast<float>(
      openwow::math::LegacyPixelSnapToInt(unstretched * viewport_height));
  return apply_stretch(snapped_pixels / viewport_height);
}

}

inline float LegacyPixelSnapUiHorizontalCoordinate(const float value) {
  return detail::LegacyPixelSnapUiCoordinate(value, RemoveCachedUiHorizontalStretch,
                                             ApplyCachedUiHorizontalStretch);
}

inline float LegacyPixelSnapUiVerticalCoordinate(const float value) {
  return detail::LegacyPixelSnapUiCoordinate(value, RemoveCachedUiVerticalStretch,
                                             ApplyCachedUiVerticalScale);
}

}

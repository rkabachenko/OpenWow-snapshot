
#pragma once

#include "openwow/core/display_settings.h"
#include "openwow/ui/ui_aspect_scales.h"
#include "openwow/ui/game/game_ui_manager.h"

namespace openwow::ui::anim {

constexpr float kAnimationStoredOffsetEpsilon = 0.00000023841858f;

inline openwow::ui::UiAspectScaleState ResolveAnimationCoordinateScaleState() {
  const auto cached_state = openwow::ui::GetCachedUiAspectScaleState();
  if (cached_state.initialized) {
    return cached_state;
  }

  if (const auto* manager = openwow::ui::game::runtime::WorldUiRuntimeContext::FromActiveLua()) {
    const float width = manager->screen_width();
    const float height = manager->screen_height();
    if (width > 0.0f && height > 0.0f) {
      return openwow::ui::ComputeUiAspectScaleStateFromViewport(width, height);
    }
  }

  const auto [width, height] =
      openwow::core::DisplaySettingsController::Instance().GetResolution();
  if (width > 0U && height > 0U) {
    return openwow::ui::ComputeUiAspectScaleStateFromViewport(
        static_cast<float>(width), static_cast<float>(height));
  }

  return openwow::ui::ComputeUiAspectScaleState(4.0f / 3.0f);
}

inline float ResolveAnimationCoordinateAspectRatio() {
  return ResolveAnimationCoordinateScaleState().aspect_ratio;
}

inline float AnimationCoordinateScaleKx(const float aspect_ratio) {
  return openwow::ui::ComputeUiAspectScaleState(aspect_ratio).kx;
}

inline float AnimationHorizontalStretchFactor(const float aspect_ratio) {
  return openwow::ui::ComputeUiAspectScaleState(aspect_ratio).horizontal_scale;
}

inline float PixelAnimationOffsetToStored(const float pixels,
                                          const float aspect_ratio) {
  return openwow::ui::PixelUiHorizontalCoordinateToStored(pixels, aspect_ratio);
}

inline float PixelAnimationOffsetToStored(const float pixels) {
  const auto state = ResolveAnimationCoordinateScaleState();
  return PixelAnimationOffsetToStored(pixels, state.aspect_ratio);
}

inline float StoredAnimationOffsetToPixels(const float stored,
                                           const float aspect_ratio) {
  return openwow::ui::StoredUiHorizontalCoordinateToPixels(stored, aspect_ratio);
}

inline float StoredAnimationOffsetToPixels(const float stored) {
  const auto state = ResolveAnimationCoordinateScaleState();
  return StoredAnimationOffsetToPixels(stored, state.aspect_ratio);
}

}

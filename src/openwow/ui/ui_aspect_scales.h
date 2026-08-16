
#pragma once

#include "openwow/ui/ui_coordinate_space.h"

#include <cmath>
#include <utility>

namespace openwow::ui {

constexpr float kDefaultUiAspectRatio = 4.0f / 3.0f;
constexpr float kUiScriptScreenHeight = kUiScriptScreenUnitHeight;

constexpr float kFallbackUiViewportWidth = 640.0f;
constexpr float kFallbackUiViewportHeight = 480.0f;

struct UiViewportDimensions {
  float width{kFallbackUiViewportWidth};
  float height{kFallbackUiViewportHeight};
};

struct UiAspectScaleState {
  float aspect_ratio{kDefaultUiAspectRatio};
  float kx{1.0f};
  float horizontal_scale{0.8f};
  float vertical_scale{0.6f};
  bool initialized{false};
};

inline UiAspectScaleState ComputeUiAspectScaleState(float aspect_ratio) {
  const double sanitized_aspect_ratio =
      aspect_ratio > 0.0f ? static_cast<double>(aspect_ratio)
                           : static_cast<double>(kDefaultUiAspectRatio);
  const double inv_length =
      1.0 / std::sqrt(sanitized_aspect_ratio * sanitized_aspect_ratio + 1.0);
  return {
      .aspect_ratio = static_cast<float>(sanitized_aspect_ratio),
      .kx = static_cast<float>(0.75 * sanitized_aspect_ratio),
      .horizontal_scale = static_cast<float>(sanitized_aspect_ratio * inv_length),
      .vertical_scale = static_cast<float>(inv_length),
      .initialized = true,
  };
}

inline UiViewportDimensions ResolveUiViewportDimensions(
    float viewport_width, float viewport_height) {
  if (viewport_width <= 0.0f || viewport_height <= 0.0f) {
    return {};
  }

  return {.width = viewport_width, .height = viewport_height};
}

inline UiAspectScaleState ComputeUiAspectScaleStateFromViewport(
    float viewport_width, float viewport_height) {
  const UiViewportDimensions viewport =
      ResolveUiViewportDimensions(viewport_width, viewport_height);

  return ComputeUiAspectScaleState(viewport.width / viewport.height);
}

inline UiAspectScaleState& MutableCachedUiAspectScaleState() {
  static UiAspectScaleState state{};
  return state;
}

inline UiAspectScaleState GetCachedUiAspectScaleState() {
  return MutableCachedUiAspectScaleState();
}

inline const UiAspectScaleState& DefaultUiAspectScaleState() {
  static const UiAspectScaleState state =
      ComputeUiAspectScaleState(kDefaultUiAspectRatio);
  return state;
}

inline UiAspectScaleState GetEffectiveUiAspectScaleState() {
  const UiAspectScaleState state = GetCachedUiAspectScaleState();
  return state.initialized ? state : DefaultUiAspectScaleState();
}

inline float GetCachedUiAspectRatio() {
  return GetEffectiveUiAspectScaleState().aspect_ratio;
}

inline float GetCachedUiAspectScaleKx() {
  return GetEffectiveUiAspectScaleState().kx;
}

inline float GetCachedUiAspectVerticalScale() {
  return GetEffectiveUiAspectScaleState().vertical_scale;
}

inline float ApplyCachedUiVerticalScale(const float value) {
  return GetCachedUiAspectVerticalScale() * value;
}

constexpr float kUiScriptCoordinateScale = 1024.0f;

inline float GetUiScriptScreenWidth(const UiAspectScaleState& state) {
  return state.kx * kUiScriptCoordinateScale;
}

inline float GetUiScriptScreenWidth(const float viewport_width,
                                    const float viewport_height) {
  return GetUiScriptScreenWidth(
      ComputeUiAspectScaleStateFromViewport(viewport_width, viewport_height));
}

inline float GetUiScriptScreenWidth() {
  return GetUiScriptScreenWidth(GetEffectiveUiAspectScaleState());
}

inline float GetUiScriptScreenHeight() {
  return kUiScriptScreenHeight;
}

inline std::pair<float, float> ProjectBottomLeftPixelCursorToUiScript(
    const float x_pixels, const float y_pixels, const float viewport_height) {
  if (viewport_height <= 0.0f) {
    return {0.0f, 0.0f};
  }
  const float pixels_to_script = kUiScriptScreenHeight / viewport_height;
  return {x_pixels * pixels_to_script, y_pixels * pixels_to_script};
}

inline std::pair<float, float> ProjectTopLeftPixelCursorToUiScript(
    const float x_pixels, const float y_pixels, const float viewport_height) {
  return ProjectBottomLeftPixelCursorToUiScript(
      x_pixels, viewport_height - y_pixels, viewport_height);
}

inline float PixelUiHorizontalCoordinateToStored(
    const float pixels, const UiAspectScaleState& state) {
  if (state.kx <= 0.0f || state.horizontal_scale <= 0.0f) {
    return 0.0f;
  }

  return state.horizontal_scale * (pixels / (state.kx * kUiScriptCoordinateScale));
}

inline float PixelUiHorizontalCoordinateToStored(const float pixels,
                                                 const float aspect_ratio) {
  return PixelUiHorizontalCoordinateToStored(
      pixels, ComputeUiAspectScaleState(aspect_ratio));
}

inline float PixelUiHorizontalCoordinateToStored(const float pixels) {
  return PixelUiHorizontalCoordinateToStored(
      pixels, GetEffectiveUiAspectScaleState());
}

inline float RemoveCachedUiHorizontalStretch(const float value) {
  return value / GetEffectiveUiAspectScaleState().horizontal_scale;
}

inline float ApplyCachedUiHorizontalStretch(const float value) {
  return GetEffectiveUiAspectScaleState().horizontal_scale * value;
}

inline void ApplyCachedUiStretch(const float x, const float y, float* out_x,
                                 float* out_y) {
  if (out_x != nullptr) {
    *out_x = ApplyCachedUiHorizontalStretch(x);
  }
  if (out_y != nullptr) {
    *out_y = ApplyCachedUiVerticalScale(y);
  }
}

inline float StoredUiHorizontalCoordinateToPixels(
    const float stored, const UiAspectScaleState& state) {
  if (state.kx <= 0.0f || state.horizontal_scale <= 0.0f) {
    return 0.0f;
  }

  return (state.kx * kUiScriptCoordinateScale * stored) / state.horizontal_scale;
}

inline float StoredUiHorizontalCoordinateToPixels(const float stored,
                                                  const float aspect_ratio) {
  return StoredUiHorizontalCoordinateToPixels(
      stored, ComputeUiAspectScaleState(aspect_ratio));
}

inline float StoredUiHorizontalCoordinateToPixels(const float stored) {
  return StoredUiHorizontalCoordinateToPixels(
      stored, GetEffectiveUiAspectScaleState());
}

inline float RemoveCachedUiVerticalStretch(const float value) {
  return value / GetEffectiveUiAspectScaleState().vertical_scale;
}

inline void RemoveCachedUiStretch(const float x, const float y, float* out_x,
                                  float* out_y) {
  if (out_x != nullptr) {
    *out_x = RemoveCachedUiHorizontalStretch(x);
  }
  if (out_y != nullptr) {
    *out_y = RemoveCachedUiVerticalStretch(y);
  }
}

inline void SetCachedUiAspectScaleState(float aspect_ratio) {
  MutableCachedUiAspectScaleState() = ComputeUiAspectScaleState(aspect_ratio);
}

inline void ResetCachedUiAspectScaleState() {
  MutableCachedUiAspectScaleState() = UiAspectScaleState{};
}

}

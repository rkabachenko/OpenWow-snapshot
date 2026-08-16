#pragma once

#include "openwow/ui/ui_aspect_scales.h"

#include <algorithm>

namespace openwow::render {

inline constexpr float kDamageTextFontViewportRatio = 0.0183333326f;

struct WorldOverlayMetrics {
  struct FramebufferPoint {
    float x{0.0f};
    float y{0.0f};
  };

  float framebuffer_width{1.0f};
  float framebuffer_height{1.0f};
  float aspect_ratio{1.0f};
  float ui_parent_effective_pixel_scale{1.0f};

  [[nodiscard]] static WorldOverlayMetrics FromFramebuffer(
      const float width, const float height,
      const float ui_parent_effective_pixel_scale) noexcept {
    const float resolved_width = std::max(width, 1.0f);
    const float resolved_height = std::max(height, 1.0f);
    return {
        .framebuffer_width = resolved_width,
        .framebuffer_height = resolved_height,
        .aspect_ratio = resolved_width / resolved_height,
        .ui_parent_effective_pixel_scale =
            ui_parent_effective_pixel_scale > 0.0f
                ? ui_parent_effective_pixel_scale
                : 1.0f,
    };
  }

  [[nodiscard]] float DamageTextFontPixelHeight() const noexcept {

    const auto aspect = openwow::ui::ComputeUiAspectScaleState(aspect_ratio);
    return kDamageTextFontViewportRatio * framebuffer_height /
           aspect.vertical_scale;
  }

  [[nodiscard]] float ScaleUiPixels(const float value) const noexcept {
    return value * ui_parent_effective_pixel_scale;
  }

  [[nodiscard]] FramebufferPoint NdcToFramebuffer(
      const float ndc_x, const float ndc_y) const noexcept {
    return {
        .x = (ndc_x * 0.5f + 0.5f) * framebuffer_width,
        .y = (1.0f - (ndc_y * 0.5f + 0.5f)) * framebuffer_height,
    };
  }
};

}

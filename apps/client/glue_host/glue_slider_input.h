#pragma once

#include "openwow/ui/glue/glue_widget_runtime.h"
#include "openwow/foundation/text/ascii.h"

#include <algorithm>
#include <optional>
#include <string>

namespace openwow::client::detail {

inline std::optional<openwow::ui::glue::GlueWidgetState> FindSliderThumb(
    const openwow::ui::glue::GlueWidgetRuntime& widgets,
    const std::string& slider_name) {
  for (const char* suffix : {"ThumbTexture", "Thumb"}) {
    const auto thumb = widgets.GetResolvedWidget(slider_name + suffix);
    if (thumb.has_value() &&
        openwow::text::EqualsIgnoreCaseAscii(thumb->kind, "Texture")) {
      return thumb;
    }
  }
  return std::nullopt;
}

inline std::optional<double> SliderValueAtPoint(
    const openwow::ui::glue::GlueWidgetRuntime& widgets,
    const openwow::ui::glue::GlueWidgetState& slider, const int mouse_x,
    const int mouse_y) {
  const auto thumb = FindSliderThumb(widgets, slider.name);
  if (!thumb.has_value()) {
    return std::nullopt;
  }

  const bool vertical = slider.height > slider.width;
  const int track_extent = vertical ? slider.height : slider.width;
  const int thumb_extent = vertical ? thumb->height : thumb->width;
  const int usable_extent = track_extent - thumb_extent;
  if (usable_extent <= 0) {
    return std::nullopt;
  }

  const int track_start =
      vertical ? slider.y + thumb_extent / 2
               : slider.x + thumb_extent / 2;
  const int cursor = vertical ? mouse_y : mouse_x;
  const double ratio = std::clamp(
      static_cast<double>(cursor - track_start) / usable_extent, 0.0, 1.0);
  const auto [minimum, maximum] = widgets.GetMinMaxValues(slider.name);
  return minimum + (maximum - minimum) * ratio;
}

}

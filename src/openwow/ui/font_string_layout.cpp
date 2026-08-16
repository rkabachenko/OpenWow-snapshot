#include "openwow/ui/font_string_layout.h"

#include "openwow/ui/framexml/layout_anchor_resolution.h"
#include "openwow/ui/framexml/ui_frame.h"

#include <algorithm>
#include <array>

namespace openwow::ui {
namespace {

bool HasAnchor(const framexml::UiFrame& frame,
               const std::array<int, 3>& slots) {
  const auto anchors = framexml::detail::BuildAnchorSlots(frame.anchors);
  return std::any_of(slots.begin(), slots.end(), [&](const int slot) {
    if (slot < 0 || slot >= static_cast<int>(anchors.size())) return false;
    const auto* anchor = anchors[static_cast<std::size_t>(slot)];
    return anchor != nullptr &&
           !framexml::detail::AnchorHasFlag(
               *anchor, framexml::detail::kAnchorHiddenBit);
  });
}

int HorizontalConstraints(const framexml::UiFrame& frame) {
  return static_cast<int>(
             HasAnchor(frame, framexml::detail::kLeftPointSlots)) +
         static_cast<int>(
             HasAnchor(frame, framexml::detail::kHorizontalCenterPointSlots)) +
         static_cast<int>(
             HasAnchor(frame, framexml::detail::kRightPointSlots));
}

int VerticalConstraints(const framexml::UiFrame& frame) {
  return static_cast<int>(
             HasAnchor(frame, framexml::detail::kTopPointSlots)) +
         static_cast<int>(
             HasAnchor(frame, framexml::detail::kVerticalCenterPointSlots)) +
         static_cast<int>(
             HasAnchor(frame, framexml::detail::kBottomPointSlots));
}

}

bool FontStringWidthComesFromLayout(
    const framexml::UiFrame& frame) noexcept {
  return frame.set_all_points || frame.rel_width.has_value() ||
         (frame.width.has_value() && *frame.width > 0.0f &&
          !frame.font_intrinsic_width) ||
         HorizontalConstraints(frame) >= 2;
}

bool FontStringHeightComesFromLayout(
    const framexml::UiFrame& frame) noexcept {
  return frame.set_all_points || frame.rel_height.has_value() ||
         (frame.height.has_value() && *frame.height > 0.0f &&
          !frame.font_intrinsic_height) ||
         VerticalConstraints(frame) >= 2;
}

int ResolveFontStringRenderWrapWidth(
    const framexml::UiFrame& frame, const bool word_wrap,
    const bool non_space_wrap, const int rendered_width) noexcept {
  if ((!word_wrap && !non_space_wrap) ||
      !FontStringWidthComesFromLayout(frame)) {
    return 0;
  }
  return std::max(0, rendered_width);
}

}

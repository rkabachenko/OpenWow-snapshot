#include "openwow/ui/game/runtime/layout_persistence.h"

#include "openwow/ui/framexml/layout_anchor_resolution.h"
#include "openwow/ui/framexml/layout_resolver.h"

#include <array>
#include <cmath>
#include <limits>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace openwow::ui::game::runtime {
namespace {

using openwow::ui::framexml::FrameRect;
using openwow::ui::framexml::UiAnchor;
using openwow::ui::framexml::UiFrame;

constexpr std::array<std::string_view, 9> kFramePoints = {
    "TOPLEFT",
    "TOP",
    "TOPRIGHT",
    "LEFT",
    "CENTER",
    "RIGHT",
    "BOTTOMLEFT",
    "BOTTOM",
    "BOTTOMRIGHT",
};

struct Point {
  double x{0.0};
  double y{0.0};
};

struct RectPixels {
  double left{0.0};
  double top{0.0};
  double right{0.0};
  double bottom{0.0};
};

RectPixels ToPixelRect(const FrameRect& rect) {
  return RectPixels{
      .left = static_cast<double>(rect.x),
      .top = static_cast<double>(rect.y),
      .right = static_cast<double>(rect.x + rect.width),
      .bottom = static_cast<double>(rect.y + rect.height),
  };
}

Point PointInRect(const RectPixels& rect, std::string_view point) {
  const double left = rect.left;
  const double right = rect.right;
  const double top = rect.top;
  const double bottom = rect.bottom;
  const double center_x = (left + right) * 0.5;
  const double center_y = (top + bottom) * 0.5;

  if (point == "TOPLEFT") {
    return {left, top};
  }
  if (point == "TOP") {
    return {center_x, top};
  }
  if (point == "TOPRIGHT") {
    return {right, top};
  }
  if (point == "LEFT") {
    return {left, center_y};
  }
  if (point == "RIGHT") {
    return {right, center_y};
  }
  if (point == "BOTTOMLEFT") {
    return {left, bottom};
  }
  if (point == "BOTTOM") {
    return {center_x, bottom};
  }
  if (point == "BOTTOMRIGHT") {
    return {right, bottom};
  }
  return {center_x, center_y};
}

std::array<Point, 9> ExpandAnchorPoints(const RectPixels& rect) {
  std::array<Point, 9> points{};
  for (std::size_t i = 0; i < kFramePoints.size(); ++i) {
    points[i] = PointInRect(rect, kFramePoints[i]);
  }
  return points;
}

float ComputeOwnScale(const UiFrame& frame) {
  return frame.scale > 0.0f ? frame.scale : 1.0f;
}

float ComputeEffectiveScale(
    const UiFrame& frame,
    const std::unordered_map<std::string, const UiFrame*>& frames_by_name) {
  float effective = 1.0f;
  const UiFrame* current = &frame;
  for (int depth = 0; current != nullptr && depth < 64; ++depth) {
    effective *= ComputeOwnScale(*current);
    if (current->parent.empty()) {
      break;
    }
    const auto it = frames_by_name.find(current->parent);
    current = it != frames_by_name.end() ? it->second : nullptr;
  }
  return effective;
}

std::string ResolveRelativeFrameName(const UiFrame& frame) {
  for (const auto& anchor : frame.anchors) {
    if (!anchor.relative_to.empty()) {
      return anchor.relative_to;
    }
  }
  if (!frame.parent.empty()) {
    return frame.parent;
  }
  return "UIParent";
}

int RoundToInt(const double value) {

  return static_cast<int>(std::lround(value));
}

template <std::size_t N>
bool HasVisibleAnchorInSlots(
    const std::array<const UiAnchor*, 9>& anchors,
    const std::array<int, N>& slots) {
  for (const int slot : slots) {
    if (slot < 0 || slot >= static_cast<int>(anchors.size())) {
      continue;
    }
    const UiAnchor* const anchor = anchors[static_cast<std::size_t>(slot)];
    if (anchor != nullptr &&
        !openwow::ui::framexml::detail::AnchorHasFlag(
            *anchor, openwow::ui::framexml::detail::kAnchorHiddenBit)) {
      return true;
    }
  }
  return false;
}

bool IsDimensionConstrainedByAnchors(const UiFrame& frame,
                                     const bool horizontal) {
  if (frame.set_all_points) {
    return true;
  }
  const auto anchors =
      openwow::ui::framexml::detail::BuildAnchorSlots(frame.anchors);
  if (horizontal) {
    return static_cast<int>(HasVisibleAnchorInSlots(
               anchors, openwow::ui::framexml::detail::kLeftPointSlots)) +
               static_cast<int>(HasVisibleAnchorInSlots(
                   anchors,
                   openwow::ui::framexml::detail::kHorizontalCenterPointSlots)) +
               static_cast<int>(HasVisibleAnchorInSlots(
                   anchors, openwow::ui::framexml::detail::kRightPointSlots)) >=
           2;
  }
  return static_cast<int>(HasVisibleAnchorInSlots(
             anchors, openwow::ui::framexml::detail::kTopPointSlots)) +
             static_cast<int>(HasVisibleAnchorInSlots(
                 anchors,
                 openwow::ui::framexml::detail::kVerticalCenterPointSlots)) +
             static_cast<int>(HasVisibleAnchorInSlots(
                 anchors, openwow::ui::framexml::detail::kBottomPointSlots)) >=
         2;
}

}

std::string_view LayoutCacheFramePointName(const std::size_t point_index) {
  if (point_index >= kFramePoints.size()) {
    return kFramePoints[static_cast<std::size_t>(4)];
  }
  return kFramePoints[point_index];
}

NearestMatchingFramePointPlacement
ComputeNearestMatchingFramePointPlacement(
    const openwow::ui::framexml::FrameRect& frame_rect,
    const openwow::ui::framexml::FrameRect& relative_rect) {
  const auto frame_points = ExpandAnchorPoints(ToPixelRect(frame_rect));
  const auto relative_points = ExpandAnchorPoints(ToPixelRect(relative_rect));

  NearestMatchingFramePointPlacement best{};
  double best_distance = std::numeric_limits<double>::max();
  for (std::size_t i = 0; i < frame_points.size(); ++i) {
    const double delta_x = frame_points[i].x - relative_points[i].x;
    const double delta_y = frame_points[i].y - relative_points[i].y;
    const double distance = delta_x * delta_x + delta_y * delta_y;
    if (distance < best_distance) {
      best_distance = distance;
      best.point_index = i;
      best.pixel_offset_x = static_cast<float>(delta_x);
      best.pixel_offset_y = static_cast<float>(-delta_y);
    }
  }

  return best;
}

std::string SerializeLayoutCache(
    const std::vector<openwow::ui::framexml::UiFrame>& frames,
    int viewport_width,
    int viewport_height) {
  std::ostringstream out;
  out << "Version: 1\n";

  if (viewport_width <= 0 || viewport_height <= 0) {
    return out.str();
  }

  const float ui_scale = static_cast<float>(viewport_height) / 768.0f;
  const auto layout = openwow::ui::framexml::ResolveLayout(
      frames, viewport_width, viewport_height, ui_scale);

  std::unordered_map<std::string, const UiFrame*> frames_by_name;
  frames_by_name.reserve(frames.size());
  for (const auto& frame : frames) {
    if (!frame.name.empty()) {
      frames_by_name.emplace(frame.name, &frame);
    }
  }

  for (const auto& frame : frames) {
    if (frame.name.empty() || !frame.user_placed || frame.dont_save_position) {
      continue;
    }

    const auto rect_it = layout.find(frame.name);
    if (rect_it == layout.end()) {
      continue;
    }

    const std::string relative_name = ResolveRelativeFrameName(frame);
    const auto relative_rect_it = layout.find(relative_name);
    if (relative_rect_it == layout.end()) {
      continue;
    }

    const auto snapped = ComputeNearestMatchingFramePointPlacement(
        rect_it->second,
        relative_rect_it->second);

    const float frame_effective_scale =
        ui_scale * ComputeEffectiveScale(frame, frames_by_name);
    if (frame_effective_scale <= 0.0f) {
      continue;
    }

    out << "Frame: " << frame.name << '\n';
    out << "FrameLevel: " << frame.frame_level << '\n';
    out << "Anchor: " << LayoutCacheFramePointName(snapped.point_index) << '\n';
    out << "X: " << RoundToInt(
        static_cast<double>(snapped.pixel_offset_x) / frame_effective_scale) << '\n';
    out << "Y: " << RoundToInt(
        static_cast<double>(snapped.pixel_offset_y) / frame_effective_scale) << '\n';

    const double script_width =
        frame.width.has_value() && *frame.width > 0.0f &&
                !IsDimensionConstrainedByAnchors(frame, true)
            ? static_cast<double>(*frame.width)
            : static_cast<double>(rect_it->second.width) / frame_effective_scale;
    const double script_height =
        frame.height.has_value() && *frame.height > 0.0f &&
                !IsDimensionConstrainedByAnchors(frame, false)
            ? static_cast<double>(*frame.height)
            : static_cast<double>(rect_it->second.height) / frame_effective_scale;
    out << "W: " << RoundToInt(script_width) << '\n';
    out << "H: " << RoundToInt(script_height) << '\n';
  }

  return out.str();
}

}

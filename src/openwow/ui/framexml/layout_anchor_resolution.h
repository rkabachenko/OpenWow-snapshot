#pragma once

#include "openwow/ui/framexml/anchor_semantics.h"
#include "openwow/ui/ui_enum_helpers.h"

#include <array>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::ui::framexml::detail {

struct AnchorRect {
  float min_x{0.0f};
  float min_y{0.0f};
  float max_x{0.0f};
  float max_y{0.0f};
};

inline constexpr float kAnchorUnresolvedCoordinate = std::numeric_limits<float>::infinity();
inline constexpr std::uint32_t kAnchorDeferredBit = 0x400u;
inline constexpr std::uint32_t kAnchorHiddenBit = 0x800u;
inline constexpr int kAnchorRetryBudget = 10;

inline constexpr std::array<int, 3> kLeftPointSlots{{0, 3, 6}};
inline constexpr std::array<int, 3> kHorizontalCenterPointSlots{{1, 4, 7}};
inline constexpr std::array<int, 3> kRightPointSlots{{2, 5, 8}};
inline constexpr std::array<int, 3> kTopPointSlots{{0, 1, 2}};
inline constexpr std::array<int, 3> kVerticalCenterPointSlots{{3, 4, 5}};
inline constexpr std::array<int, 3> kBottomPointSlots{{6, 7, 8}};

inline bool AnchorHasFlag(const UiAnchor &anchor, const std::uint32_t flag) {
  return (anchor.flags & flag) != 0u;
}

struct FramePointName {
  std::string_view name;
  int slot;
};
inline constexpr FramePointName kTopLeftPoint{"TOPLEFT", 0};
inline constexpr FramePointName kTopPoint{"TOP", 1};
inline constexpr FramePointName kTopRightPoint{"TOPRIGHT", 2};
inline constexpr FramePointName kLeftPoint{"LEFT", 3};
inline constexpr FramePointName kCenterPoint{"CENTER", 4};
inline constexpr FramePointName kRightPoint{"RIGHT", 5};
inline constexpr FramePointName kBottomLeftPoint{"BOTTOMLEFT", 6};
inline constexpr FramePointName kBottomPoint{"BOTTOM", 7};
inline constexpr FramePointName kBottomRightPoint{"BOTTOMRIGHT", 8};

inline bool EqualsFramePointName(const std::string_view text,
                                 const FramePointName candidate) noexcept {
  for (std::size_t index = 0; index < candidate.name.size(); ++index) {
    const auto lhs = static_cast<unsigned char>(text[index]);
    const auto rhs = static_cast<unsigned char>(candidate.name[index]);
    if ((lhs | 0x20U) != (rhs | 0x20U)) {
      return false;
    }
  }
  return true;
}

inline int FramePointSlotOrInvalidExact(const std::string_view point_name) {
  switch (point_name.size()) {
  case 3:
    return EqualsFramePointName(point_name, kTopPoint) ? kTopPoint.slot : -1;
  case 4:
    return EqualsFramePointName(point_name, kLeftPoint) ? kLeftPoint.slot : -1;
  case 5:
    return EqualsFramePointName(point_name, kRightPoint) ? kRightPoint.slot : -1;
  case 6:
    if (EqualsFramePointName(point_name, kCenterPoint)) return kCenterPoint.slot;
    return EqualsFramePointName(point_name, kBottomPoint) ? kBottomPoint.slot : -1;
  case 7:
    return EqualsFramePointName(point_name, kTopLeftPoint) ? kTopLeftPoint.slot : -1;
  case 8:
    return EqualsFramePointName(point_name, kTopRightPoint) ? kTopRightPoint.slot
                                                            : -1;
  case 10:
    return EqualsFramePointName(point_name, kBottomLeftPoint)
               ? kBottomLeftPoint.slot
               : -1;
  case 11:
    return EqualsFramePointName(point_name, kBottomRightPoint)
               ? kBottomRightPoint.slot
               : -1;
  default:
    return -1;
  }
}

inline int FramePointSlotOrInvalid(const std::string_view point_name) {
  if (point_name.empty()) {
    return kCenterPoint.slot;
  }

  switch (point_name.size()) {
  case 3:
    if (EqualsFramePointName(point_name, kTopPoint)) return kTopPoint.slot;
    break;
  case 4:
    if (EqualsFramePointName(point_name, kLeftPoint)) return kLeftPoint.slot;
    break;
  case 5:
    if (EqualsFramePointName(point_name, kRightPoint)) return kRightPoint.slot;
    break;
  case 6:
    if (EqualsFramePointName(point_name, kCenterPoint)) return kCenterPoint.slot;
    if (EqualsFramePointName(point_name, kBottomPoint)) return kBottomPoint.slot;
    break;
  case 7:
    if (EqualsFramePointName(point_name, kTopLeftPoint))
      return kTopLeftPoint.slot;
    break;
  case 8:
    if (EqualsFramePointName(point_name, kTopRightPoint))
      return kTopRightPoint.slot;
    break;
  case 10:
    if (EqualsFramePointName(point_name, kBottomLeftPoint))
      return kBottomLeftPoint.slot;
    break;
  case 11:
    if (EqualsFramePointName(point_name, kBottomRightPoint))
      return kBottomRightPoint.slot;
    break;
  default:
    break;
  }
  const auto nul = point_name.find('\0');
  if (nul == std::string_view::npos || nul == 0) {
    return -1;
  }
  return FramePointSlotOrInvalidExact(point_name.substr(0, nul));
}

inline std::string_view ResolveRelativeTargetName(const UiAnchor &anchor,
                                                  const std::string_view owner_parent) {
  if (!anchor.relative_to.empty()) {
    return anchor.relative_to;
  }
  if (!owner_parent.empty()) {
    return owner_parent;
  }
  return "UIParent";
}

inline std::string_view SetAllPointsTargetName(const UiFrame &frame) {
  for (const UiAnchor &anchor : frame.anchors) {
    if (!anchor.relative_to.empty()) {
      return anchor.relative_to;
    }
  }
  return frame.parent.empty() ? std::string_view{"UIParent"}
                              : std::string_view{frame.parent};
}

inline std::array<const UiAnchor *, 9> BuildAnchorSlots(const std::vector<UiAnchor> &anchors) {
  std::array<const UiAnchor *, 9> anchor_slots{};
  anchor_slots.fill(nullptr);

  for (const UiAnchor &anchor : anchors) {
    const int point_slot = FramePointSlotOrInvalid(anchor.point);
    if (point_slot < 0 || point_slot >= static_cast<int>(anchor_slots.size())) {
      continue;
    }
    anchor_slots[static_cast<std::size_t>(point_slot)] = &anchor;
  }

  return anchor_slots;
}

inline float ResolveRelativeX(const UiAnchor &anchor, const AnchorRect &rect,
                              const float offset_scale) {
  const std::string_view relative_point = EffectiveAnchorRelativePoint(anchor);
  const float scaled_offset = anchor.x * offset_scale;

  switch (FramePointSlotOrInvalid(relative_point)) {
  case 0:
  case 3:
  case 6:
    return rect.min_x + scaled_offset;
  case 1:
  case 4:
  case 7:
    return (rect.min_x + rect.max_x) * 0.5f + scaled_offset;
  case 2:
  case 5:
  case 8:
    return rect.max_x + scaled_offset;
  default:
    return kAnchorUnresolvedCoordinate;
  }
}

inline float ResolveRelativeY(const UiAnchor &anchor, const AnchorRect &rect,
                              const float offset_scale) {
  const std::string_view relative_point = EffectiveAnchorRelativePoint(anchor);
  const float scaled_offset = anchor.y * offset_scale;

  switch (FramePointSlotOrInvalid(relative_point)) {
  case 0:
  case 1:
  case 2:
    return rect.max_y + scaled_offset;
  case 3:
  case 4:
  case 5:
    return (rect.min_y + rect.max_y) * 0.5f + scaled_offset;
  case 6:
  case 7:
  case 8:
    return rect.min_y + scaled_offset;
  default:
    return kAnchorUnresolvedCoordinate;
  }
}

template <std::size_t N, typename LookupRectFn, typename ResolveCoordinateFn>
inline bool
TryResolvePointSetPass(const std::array<const UiAnchor *, 9> &anchor_slots,
                       const std::array<int, N> &point_slots, const std::string_view owner_parent,
                       const float offset_scale, LookupRectFn &&lookup_rect,
                       ResolveCoordinateFn &&resolve_coordinate, float *const out_value) {
  for (const int point_slot : point_slots) {
    if (point_slot < 0 || point_slot >= static_cast<int>(anchor_slots.size())) {
      continue;
    }

    const UiAnchor *const anchor = anchor_slots[static_cast<std::size_t>(point_slot)];
    if (anchor == nullptr || AnchorHasFlag(*anchor, kAnchorHiddenBit)) {
      continue;
    }

    float value = kAnchorUnresolvedCoordinate;
    if (const auto relative_rect = lookup_rect(ResolveRelativeTargetName(*anchor, owner_parent));
        relative_rect.has_value()) {
      value = resolve_coordinate(*anchor, *relative_rect, offset_scale);
    }

    if (out_value != nullptr) {
      *out_value = value;
    }

    if (AnchorHasFlag(*anchor, kAnchorDeferredBit)) {
      if (out_value != nullptr) {
        *out_value = kAnchorUnresolvedCoordinate;
      }
      return false;
    }

    if (value != kAnchorUnresolvedCoordinate) {
      return true;
    }
  }

  if (out_value != nullptr) {
    *out_value = kAnchorUnresolvedCoordinate;
  }
  return true;
}

template <std::size_t N, typename LookupRectFn, typename ResolveCoordinateFn>
inline float ResolvePointSetWithRetries(const std::array<const UiAnchor *, 9> &anchor_slots,
                                        const std::array<int, N> &point_slots,
                                        const std::string_view owner_parent,
                                        const float offset_scale, LookupRectFn &&lookup_rect,
                                        ResolveCoordinateFn &&resolve_coordinate) {
  float value = kAnchorUnresolvedCoordinate;
  int attempts = 0;

  do {
    if (attempts >= kAnchorRetryBudget) {
      break;
    }
    ++attempts;
  } while (!TryResolvePointSetPass(anchor_slots, point_slots, owner_parent, offset_scale,
                                   lookup_rect, resolve_coordinate, &value));

  return value;
}

}

#pragma once

#include <array>
#include <cstdint>

namespace openwow::ui::game {

enum class TooltipAnchorType : int {
  kLeft = 0,
  kRight = 1,
  kBottomLeft = 2,
  kBottom = 3,
  kBottomRight = 4,
  kTopLeft = 5,
  kTop = 6,
  kTopRight = 7,
  kCursor = 8,
  kNone = 9,
  kPreserve = 10,
  kCursorRight = 11,
};

struct ItemComparisonContext {
  std::uint32_t expected_item_class = 0;
  std::array<std::uint32_t, 17> equipped_entries{};
  std::array<std::uint32_t, 17> equipped_inv_types{};
  std::array<std::uint32_t, 17> resolved_ids{};
  std::array<std::uint8_t, 17> slot_resolved{};
};

struct FrameStackInfo {
  char name[1024];
  int strata;
  int level;
  bool mouse_enabled;
  char padding[3];
  int visible;
};
static_assert(sizeof(FrameStackInfo) == 0x410);

}

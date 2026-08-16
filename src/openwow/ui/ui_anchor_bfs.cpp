
#include "openwow/ui/ui_anchor_bfs.h"

#include "openwow/core/storm_alloc.h"
#include "openwow/core/storm_string.h"
#include "openwow/ui/ui_aspect_scales.h"

#include <array>
#include <cstring>
#include <deque>
#include <memory>
#include <vector>

namespace openwow::ui {
namespace {

constexpr char kCRectStormTypeTag[] = ".?AVCRect@NTempest@@";
constexpr int kAnchorGridCount = 2;

constexpr float kViewportMinLeft = 0.0f;
constexpr float kViewportMinBottom = 0.018750001f;
constexpr float kViewportTopInset = 0.037500001f;

constexpr std::uint32_t kOverflowLeft = 0x1;
constexpr std::uint32_t kOverflowRight = 0x2;
constexpr std::uint32_t kOverflowTop = 0x4;
constexpr std::uint32_t kOverflowBottom = 0x8;

std::array<AnchorGrid, kAnchorGridCount> s_anchor_grids{};
std::vector<std::unique_ptr<BFSNode>> s_owned_nodes{};
std::vector<BFSNode*> s_recycled_nodes{};
std::vector<BFSNode*> s_live_nodes{};
std::deque<BFSNode*> s_pending_nodes{};

constexpr std::array<std::array<int, 4>, 9> kDirectionTable{{
    {{0, 2, 3, 1}},
    {{1, 0, 2, 0}},
    {{1, 3, 2, 3}},
    {{0, 1, 3, 1}},
    {{0, 2, 3, 2}},
    {{0, 1, 0, 1}},
    {{0, 2, 0, 2}},
    {{3, 1, 3, 1}},
    {{3, 2, 3, 2}},
}};

using ShiftFunction = void (*)(CRect*, int, const CRect*);
constexpr std::array<ShiftFunction, 4> kShiftFunctions{
    &ShiftRectUp,
    &ShiftRectLeft,
    &ShiftRectRight,
    &ShiftRectDown,
};

void ResetNode(BFSNode* node) {
  if (node == nullptr) {
    return;
  }

  node->next = nullptr;
  node->prev = nullptr;
  node->rect_top = 0.0f;
  node->rect_left = 0.0f;
  node->rect_bottom = 0.0f;
  node->rect_right = 0.0f;
  node->category = -1;
}

void FreeAnchorGridRects(AnchorGrid& grid) {
  if (grid.rects != nullptr) {
    (void)openwow::core::SMemFree(grid.rects, kCRectStormTypeTag, -2, 0);
  }

  grid.capacity = 0;
  grid.count = 0;
  grid.rects = nullptr;
}

std::uint32_t ClampRectToViewportAndGetOverflowFlags(
    CRect* out, const CRect& input, const bool flags_only,
    const float horizontal_extent, const float vertical_extent) {
  float top = input.top;
  float left = input.left;
  float bottom = input.bottom;
  float right = input.right;

  const float height = top - bottom;
  const float width = right - left;

  std::uint32_t flags = 0;
  if (!(left > kViewportMinLeft)) {
    flags |= kOverflowLeft;
  }
  if (horizontal_extent < right) {
    flags |= kOverflowRight;
  }
  if (bottom < kViewportMinBottom) {
    flags |= kOverflowBottom;
  }
  if (top > vertical_extent) {
    flags |= kOverflowTop;
  }

  if (flags_only) {
    return flags;
  }

  if (flags == 0u) {
    if (out != nullptr) {
      *out = input;
    }
    return 0;
  }

  if ((flags & (kOverflowTop | kOverflowBottom)) != 0u) {
    if ((flags & kOverflowTop) != 0u) {
      top = vertical_extent;
      bottom = vertical_extent - height;
    } else if ((flags & kOverflowBottom) != 0u) {
      bottom = kViewportMinBottom;
      top = kViewportMinBottom + height;
    }
  }

  if ((flags & (kOverflowLeft | kOverflowRight)) != 0u) {
    if ((flags & kOverflowLeft) != 0u) {
      left = kViewportMinLeft;
      right = kViewportMinLeft + width;
    } else if ((flags & kOverflowRight) != 0u) {
      right = horizontal_extent;
      left = horizontal_extent - width;
    }
  }

  if (out != nullptr) {
    out->top = top;
    out->left = left;
    out->bottom = bottom;
    out->right = right;
  }
  return flags;
}

int ClassifyOverflowFlags(const std::uint32_t flags) {
  if ((flags & kOverflowLeft) != 0u) {
    if ((flags & kOverflowTop) != 0u) {
      return 8;
    }
    return static_cast<int>(((flags & kOverflowBottom) | 0x10u) >> 2);
  }

  if ((flags & kOverflowTop) != 0u) {
    return (flags & kOverflowRight) != 0u ? 7 : 2;
  }

  if ((flags & kOverflowRight) != 0u) {
    return (flags & kOverflowBottom) != 0u ? 5 : 3;
  }

  return static_cast<int>((flags >> 3) & 0x1u);
}

CRect RectFromNode(const BFSNode& node) {
  return {
      .top = node.rect_top,
      .left = node.rect_left,
      .bottom = node.rect_bottom,
      .right = node.rect_right,
  };
}

void WriteRectToNode(BFSNode* node, const CRect& rect) {
  if (node == nullptr) {
    return;
  }

  node->rect_top = rect.top;
  node->rect_left = rect.left;
  node->rect_bottom = rect.bottom;
  node->rect_right = rect.right;
}

bool RectsEqual(const CRect& left, const CRect& right) {
  return left.top == right.top && left.left == right.left &&
         left.bottom == right.bottom && left.right == right.right;
}

void RecycleScratchNodes() {
  s_pending_nodes.clear();
  for (BFSNode* const node : s_live_nodes) {
    ResetNode(node);
    s_recycled_nodes.push_back(node);
  }
  s_live_nodes.clear();
}

}

int ComputeGridCellCount(const CRect* rect) {
  if (rect == nullptr) {
    return 1;
  }

  const float height = rect->top - rect->bottom;
  const float width = rect->right - rect->left;
  if (height == 0.0f || width == 0.0f) {
    return 1;
  }

  return (static_cast<int>(ApplyCachedUiVerticalScale(1.0f) / height) + 1) *
         (static_cast<int>(ApplyCachedUiHorizontalStretch(1.0f) / width) + 1);
}

void* CRect_NTempest_Realloc(void* array_obj, std::uint32_t new_capacity) {
  return openwow::core::ResizeLegacyArrayStoragePreservingPrefix(
      openwow::core::LegacyResizableBufferView(array_obj), new_capacity,
      sizeof(CRect), kCRectStormTypeTag);
}

bool FindOverlapShift(int grid_index, const CRect* test_rect, int direction,
                      float* out_amount) {
  if (grid_index < 0 || grid_index >= kAnchorGridCount || test_rect == nullptr ||
      out_amount == nullptr) {
    return false;
  }

  const AnchorGrid& grid = s_anchor_grids[static_cast<std::size_t>(grid_index)];
  if (grid.rects == nullptr || grid.count == 0u) {
    return false;
  }

  for (std::uint32_t index = 0; index < grid.count; ++index) {
    const CRect& rect = grid.rects[index];
    if (rect.left < test_rect->right && rect.right > test_rect->left &&
        rect.bottom < test_rect->top && rect.top > test_rect->bottom) {
      switch (direction) {
        case 0:
          *out_amount = rect.top - test_rect->bottom;
          return true;
        case 1:
          *out_amount = test_rect->right - rect.left;
          return true;
        case 2:
          *out_amount = rect.right - test_rect->left;
          return true;
        case 3:
          *out_amount = test_rect->top - rect.bottom;
          return true;
        default:
          break;
      }
    }
  }

  return false;
}

void ShiftRectUp(CRect* out, int grid_index, const CRect* in_rect) {
  if (out == nullptr || in_rect == nullptr) {
    return;
  }

  *out = *in_rect;
  float overlap_shift = 0.0f;
  if (FindOverlapShift(grid_index, in_rect, 0, &overlap_shift)) {
    out->top += overlap_shift;
    out->bottom += overlap_shift;
  }
}

void ShiftRectLeft(CRect* out, int grid_index, const CRect* in_rect) {
  if (out == nullptr || in_rect == nullptr) {
    return;
  }

  *out = *in_rect;
  float overlap_shift = 0.0f;
  if (FindOverlapShift(grid_index, in_rect, 1, &overlap_shift)) {
    out->left -= overlap_shift;
    out->right -= overlap_shift;
  }
}

void ShiftRectRight(CRect* out, int grid_index, const CRect* in_rect) {
  if (out == nullptr || in_rect == nullptr) {
    return;
  }

  *out = *in_rect;
  float overlap_shift = 0.0f;
  if (FindOverlapShift(grid_index, in_rect, 2, &overlap_shift)) {
    out->left += overlap_shift;
    out->right += overlap_shift;
  }
}

void ShiftRectDown(CRect* out, int grid_index, const CRect* in_rect) {
  if (out == nullptr || in_rect == nullptr) {
    return;
  }

  *out = *in_rect;
  float overlap_shift = 0.0f;
  if (FindOverlapShift(grid_index, in_rect, 3, &overlap_shift)) {
    out->top -= overlap_shift;
    out->bottom -= overlap_shift;
  }
}

void ClearAnchorGrids() {
  for (AnchorGrid& grid : s_anchor_grids) {
    grid.count = 0;
  }
}

void ClearAnchorGrid(const int grid_index) {
  if (grid_index < 0 || grid_index >= kAnchorGridCount) {
    return;
  }
  s_anchor_grids[static_cast<std::size_t>(grid_index)].count = 0;
}

void SetAnchorGridRects(int grid_index, std::span<const CRect> rects) {
  if (grid_index < 0 || grid_index >= kAnchorGridCount) {
    return;
  }

  AnchorGrid& grid = s_anchor_grids[static_cast<std::size_t>(grid_index)];
  const auto count = static_cast<std::uint32_t>(rects.size());
  if (count == 0u) {
    FreeAnchorGridRects(grid);
    return;
  }

  if (grid.capacity != count || grid.rects == nullptr) {
    grid.rects = static_cast<CRect*>(CRect_NTempest_Realloc(&grid, count));
  }

  if (grid.rects != nullptr) {
    std::memcpy(grid.rects, rects.data(),
                sizeof(CRect) * static_cast<std::size_t>(count));
  }
  grid.count = count;
}

CRect* AppendAnchorGridRect(int grid_index) {
  if (grid_index < 0 || grid_index >= kAnchorGridCount) {
    return nullptr;
  }

  AnchorGrid& grid = s_anchor_grids[static_cast<std::size_t>(grid_index)];
  const std::uint32_t new_count = grid.count + 1;

  if (new_count > grid.capacity) {
    std::uint32_t new_capacity = new_count;
    const std::uint32_t quantum = grid.grow_size;
    if (quantum > 0u && (new_capacity % quantum) != 0u) {
      new_capacity += quantum - (new_capacity % quantum);
    }
    grid.rects =
        static_cast<CRect*>(CRect_NTempest_Realloc(&grid, new_capacity));
    if (grid.rects == nullptr) {
      return nullptr;
    }
    grid.capacity = new_capacity;
  }

  CRect* slot = &grid.rects[grid.count];
  ++grid.count;
  *slot = CRect{};
  return slot;
}

void DestroyAnchorResolverState() {
  s_pending_nodes.clear();
  s_live_nodes.clear();
  s_recycled_nodes.clear();
  s_owned_nodes.clear();

  for (AnchorGrid& grid : s_anchor_grids) {
    FreeAnchorGridRects(grid);
  }
}

BFSNode* AllocBFSNode() {
  BFSNode* node = nullptr;
  if (!s_recycled_nodes.empty()) {
    node = s_recycled_nodes.back();
    s_recycled_nodes.pop_back();
  } else {
    s_owned_nodes.push_back(std::make_unique<BFSNode>());
    node = s_owned_nodes.back().get();
  }

  ResetNode(node);
  s_live_nodes.push_back(node);
  return node;
}

void ResolveAnchorChain(CRect* out, int grid_index, const CRect* input) {
  ResolveAnchorChain(out, grid_index, input,
                     ApplyCachedUiHorizontalStretch(1.0f),
                     ApplyCachedUiVerticalScale(1.0f));
}

void ResolveAnchorChain(CRect* out, int grid_index, const CRect* input,
                        const float viewport_width,
                        const float viewport_height) {
  if (out == nullptr || input == nullptr) {
    return;
  }

  out->top = 0.0f;
  out->left = 0.0f;
  out->bottom = 0.0f;
  out->right = 0.0f;

  const float clamped_viewport_height =
      viewport_height - kViewportTopInset;
  const std::uint32_t overflow_flags =
      ClampRectToViewportAndGetOverflowFlags(out, *input, false,
                                             viewport_width,
                                             clamped_viewport_height);
  const int category = ClassifyOverflowFlags(overflow_flags);

  s_pending_nodes.clear();
  BFSNode* const start = AllocBFSNode();
  WriteRectToNode(start, *out);
  s_pending_nodes.push_back(start);

  const float input_height = input->top - input->bottom;
  const float input_width = input->right - input->left;
  const int max_iterations =
      input_height == 0.0f || input_width == 0.0f
          ? 1
          : (static_cast<int>(viewport_height / input_height) + 1) *
                (static_cast<int>(viewport_width / input_width) + 1);
  for (int iteration = 0; iteration < max_iterations; ++iteration) {
    if (s_pending_nodes.empty()) {
      break;
    }

    BFSNode* const current = s_pending_nodes.front();
    s_pending_nodes.pop_front();
    const CRect current_rect = RectFromNode(*current);

    for (int slot = 0; slot < 4; ++slot) {
      const int direction = kDirectionTable[static_cast<std::size_t>(category)]
                                           [static_cast<std::size_t>(slot)];
      if (current->category != -1 && current->category == direction) {
        continue;
      }
      if (ClampRectToViewportAndGetOverflowFlags(nullptr, current_rect, true,
                                                 viewport_width,
                                                 clamped_viewport_height) !=
          0u) {
        continue;
      }

      CRect adjusted{};
      kShiftFunctions[static_cast<std::size_t>(direction)](&adjusted, grid_index,
                                                           &current_rect);
      if (RectsEqual(current_rect, adjusted)) {
        *out = adjusted;
        RecycleScratchNodes();
        return;
      }

      BFSNode* const next = AllocBFSNode();
      WriteRectToNode(next, adjusted);
      s_pending_nodes.push_back(next);
    }
  }

  *out = *input;
  RecycleScratchNodes();
}

}

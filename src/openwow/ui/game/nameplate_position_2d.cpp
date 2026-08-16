
#include "openwow/ui/game/nameplate_position_2d.h"

#include <algorithm>
#include <cmath>

namespace openwow::ui::game {

namespace {

void ClampRectToAspectViewport(CRect& rect,
                               float aspect_width,
                               float aspect_height) {
  const float height = rect.top - rect.bottom;
  const float width = rect.right - rect.left;

  if (aspect_height <= rect.top) {
    rect.top = aspect_height;
    rect.bottom = aspect_height - height;
  }
  if (rect.bottom < 0.0f) {
    rect.bottom = 0.0f;
    rect.top = height;
  }
  if (rect.left < 0.0f) {
    rect.left = 0.0f;
    rect.right = width;
  }
  if (aspect_width < rect.right) {
    rect.right = aspect_width;
    rect.left = aspect_width - width;
  }
}

struct ResolvedPosition {
  float center_x = 0.0f;
  float top_y = 0.0f;
  bool changed = false;
};

ResolvedPosition ResolveOverlapPosition(int grid_index,
                                         const CRect& input_rect,
                                         const float aspect_width,
                                         const float aspect_height) {
  CRect resolved{};
  ResolveAnchorChain(&resolved, grid_index, &input_rect, aspect_width,
                     aspect_height);

  ResolvedPosition result;
  result.center_x = (resolved.right + resolved.left) * 0.5f;
  result.top_y = resolved.top;
  result.changed = (resolved.top != input_rect.top ||
                    resolved.left != input_rect.left ||
                    resolved.bottom != input_rect.bottom ||
                    resolved.right != input_rect.right);
  return result;
}

}

NameplatePosition2DResult ComputeNameplatePosition2D(
    const NameplatePosition2DInput& input,
    const float aspect_width,
    const float aspect_height) {

  const float half_width = input.frame_width * 0.5f;
  const float half_height = input.frame_height * 0.5f;
  const float full_height = input.frame_height;

  CRect rect{};
  rect.top = input.screen_y;

  rect.left = input.screen_x - half_width;

  rect.bottom = input.screen_y - full_height;

  rect.right = input.screen_x + half_width;

  ClampRectToAspectViewport(rect, aspect_width, aspect_height);

  const ResolvedPosition resolved = ResolveOverlapPosition(
      input.grid_index, rect, aspect_width, aspect_height);

  float center_x = resolved.center_x;
  float top_y = resolved.top_y;

  center_x = std::clamp(center_x, half_width, aspect_width - half_width);

  top_y = std::clamp(top_y, half_height, aspect_height - half_height);

  CRect final_rect{};
  final_rect.top = top_y;

  final_rect.left = center_x - half_width;

  final_rect.bottom = top_y - full_height;

  final_rect.right = center_x + half_width;

  ClampRectToAspectViewport(final_rect, aspect_width, aspect_height);

  CRect* const grid_slot = AppendAnchorGridRect(input.grid_index);
  if (grid_slot != nullptr) {
    *grid_slot = final_rect;
  }

  NameplatePosition2DResult result;
  result.anchor_rect = final_rect;
  result.offset_x = center_x;
  result.offset_y = top_y;
  result.depth = input.screen_z - input.parent_depth;
  result.point_index = input.use_center_anchor
                           ? kFramePointCenter
                           : kFramePointTopLeft;
  return result;
}

}

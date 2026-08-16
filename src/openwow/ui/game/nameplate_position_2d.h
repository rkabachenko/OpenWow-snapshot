
#pragma once

#include "openwow/ui/ui_anchor_bfs.h"
#include "openwow/ui/ui_aspect_scales.h"

namespace openwow::ui::game {

inline constexpr int kFramePointTopLeft = 1;
inline constexpr int kFramePointCenter  = 4;

struct NameplatePosition2DInput {
  float screen_x = 0.0f;
  float screen_y = 0.0f;
  float screen_z = 0.0f;
  float frame_width = 0.0f;
  float frame_height = 0.0f;
  float parent_depth = 0.0f;
  int grid_index = 0;
  bool use_center_anchor = false;
};

struct NameplatePosition2DResult {
  CRect anchor_rect{};
  float offset_x = 0.0f;
  float offset_y = 0.0f;
  float depth = 0.0f;
  int point_index = kFramePointTopLeft;
};

NameplatePosition2DResult ComputeNameplatePosition2D(
    const NameplatePosition2DInput& input,
    float aspect_width, float aspect_height);

inline NameplatePosition2DResult ComputeNameplatePosition2D(
    const NameplatePosition2DInput& input) {
  return ComputeNameplatePosition2D(
      input,
      ApplyCachedUiHorizontalStretch(1.0f),
      ApplyCachedUiVerticalScale(1.0f));
}

}

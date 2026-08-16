#pragma once

#include "openwow/ui/transparent_string_map.h"

#include "openwow/ui/framexml/ui_frame.h"

#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::ui::framexml {

struct FrameRect {
  int x{0};
  int y{0};
  int width{0};
  int height{0};
};

openwow::ui::TransparentStringMap<FrameRect> ResolveLayout(const std::vector<UiFrame>& frames,
                                                         int viewport_width,
                                                         int viewport_height,
                                                         float ui_scale = 1.0f);

openwow::ui::TransparentStringMap<FrameRect>
ResolveExpandedLayout(std::span<const UiFrame *const> frames,
                      int viewport_width, int viewport_height,
                      float ui_scale = 1.0f);

void ResolveExpandedLayoutInto(std::span<const UiFrame *const> frames,
                               int viewport_width, int viewport_height,
                               float ui_scale,
                               std::vector<std::optional<FrameRect>> *out_rects);

std::vector<UiFrame> SortByRenderOrder(const std::vector<UiFrame>& frames);

}

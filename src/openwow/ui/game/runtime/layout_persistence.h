#pragma once

#include "openwow/ui/framexml/layout_resolver.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::ui::game::runtime {

struct NearestMatchingFramePointPlacement {
  std::size_t point_index{0};
  float pixel_offset_x{0.0f};
  float pixel_offset_y{0.0f};
};

[[nodiscard]] std::string_view LayoutCacheFramePointName(std::size_t point_index);

[[nodiscard]] NearestMatchingFramePointPlacement
ComputeNearestMatchingFramePointPlacement(
    const openwow::ui::framexml::FrameRect& frame_rect,
    const openwow::ui::framexml::FrameRect& relative_rect);

std::string SerializeLayoutCache(
    const std::vector<openwow::ui::framexml::UiFrame>& frames,
    int viewport_width,
    int viewport_height);

}

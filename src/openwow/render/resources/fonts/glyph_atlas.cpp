#include "openwow/render/resources/fonts/glyph_atlas.h"

#include <algorithm>

namespace openwow::render {

GlyphAtlas::GlyphAtlas(const std::uint16_t width,
                       const std::uint16_t height)
    : pixels_(static_cast<std::size_t>(width) * height),
      width_(width),
      height_(height) {
}

AtlasRegion GlyphAtlas::Insert(
    const std::span<const std::uint8_t> coverage,
    const std::uint16_t width, const std::uint16_t height,
    const std::uint16_t pitch) {
  if (width == 0 || height == 0 || pitch < width ||
      coverage.size() < static_cast<std::size_t>(pitch) * height) {
    return {};
  }
  if (static_cast<std::uint32_t>(cursor_x_) + width + 1u > width_) {
    cursor_x_ = 1;
    cursor_y_ = static_cast<std::uint16_t>(
        static_cast<std::uint32_t>(cursor_y_) + row_height_ + 1u);
    row_height_ = 0;
  }
  if (static_cast<std::uint32_t>(cursor_y_) + height + 1u > height_) {
    return {};
  }

  for (std::uint16_t row = 0; row < height; ++row) {
    std::copy_n(coverage.data() + static_cast<std::size_t>(row) * pitch,
                width,
                pixels_.data() +
                    static_cast<std::size_t>(cursor_y_ + row) * width_ +
                    cursor_x_);
  }

  const AtlasRegion region{cursor_x_, cursor_y_, width, height};
  cursor_x_ = static_cast<std::uint16_t>(
      static_cast<std::uint32_t>(cursor_x_) + width + 1u);
  row_height_ = std::max(row_height_, height);
  return region;
}

}

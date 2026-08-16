#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace openwow::render {

struct AtlasRegion {
  std::uint16_t x{};
  std::uint16_t y{};
  std::uint16_t width{};
  std::uint16_t height{};

  [[nodiscard]] explicit operator bool() const noexcept {
    return width != 0 && height != 0;
  }
};

class GlyphAtlas {
 public:
  GlyphAtlas(std::uint16_t width, std::uint16_t height);

  [[nodiscard]] AtlasRegion Insert(std::span<const std::uint8_t> coverage,
                                   std::uint16_t width,
                                   std::uint16_t height,
                                   std::uint16_t pitch);

  [[nodiscard]] std::span<const std::uint8_t> pixels() const noexcept {
    return pixels_;
  }

 private:
  std::vector<std::uint8_t> pixels_;
  std::uint16_t width_{};
  std::uint16_t height_{};
  std::uint16_t cursor_x_{1};
  std::uint16_t cursor_y_{1};
  std::uint16_t row_height_{};
};

}

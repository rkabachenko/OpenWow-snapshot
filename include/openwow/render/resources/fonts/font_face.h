#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace openwow::render::text {

enum class FontOutline : std::uint8_t {
  None,
  Normal,
  Thick,
};

struct FontStyle {
  FontOutline outline{FontOutline::None};
  bool monochrome{false};
};

[[nodiscard]] constexpr float OutlineCellGrowthPixels(
    const FontOutline outline) noexcept {
  switch (outline) {
    case FontOutline::Normal:
      return 2.0f;
    case FontOutline::Thick:
      return 4.0f;
    case FontOutline::None:
      break;
  }
  return 0.0f;
}

struct GlyphMetrics {
  std::uint32_t codepoint{};
  std::uint32_t glyph_index{};
  float advance{};
  float bearing_x{};
  float bearing_y{};
  float width{};
  float height{};

  [[nodiscard]] explicit operator bool() const noexcept {
    return glyph_index != 0;
  }
};

struct RasterizedGlyph {
  GlyphMetrics metrics;
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t pitch{};
  std::vector<std::uint8_t> alpha;
};

class FontFace final {
 public:
  static std::shared_ptr<FontFace> LoadFile(std::string path,
                                            int pixel_height,
                                            FontStyle style = {});
  static std::shared_ptr<FontFace> LoadMemory(std::string path,
                                              std::vector<std::uint8_t> bytes,
                                              int pixel_height,
                                              FontStyle style = {});

  ~FontFace();
  FontFace(const FontFace&) = delete;
  FontFace& operator=(const FontFace&) = delete;

  [[nodiscard]] const std::string& path() const noexcept;
  [[nodiscard]] int pixel_height() const noexcept;
  [[nodiscard]] float line_height() const noexcept;
  [[nodiscard]] float ascent() const noexcept;
  [[nodiscard]] FontStyle style() const noexcept;

  [[nodiscard]] GlyphMetrics Glyph(std::uint32_t codepoint) const;
  [[nodiscard]] float Advance(std::uint32_t previous_glyph,
                              std::uint32_t glyph) const;
  [[nodiscard]] RasterizedGlyph Rasterize(std::uint32_t codepoint) const;

 private:
  struct Impl;
  explicit FontFace(std::unique_ptr<Impl> impl);

  std::unique_ptr<Impl> impl_;
};

}

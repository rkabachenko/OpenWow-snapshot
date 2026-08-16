#pragma once

#include "openwow/render/resources/fonts/font_face.h"

#include <bgfx/bgfx.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace openwow::render {

struct BgfxAtlasGlyph {
  std::size_t page{};
  float u0{};
  float v0{};
  float u1{};
  float v1{};
  float width{};
  float height{};
  float bearing_x{};
  float bearing_y{};
};

struct BgfxAtlasPage {
  bgfx::TextureHandle body = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle outline = BGFX_INVALID_HANDLE;
};

class BgfxGlyphAtlas {
 public:
  BgfxGlyphAtlas(std::shared_ptr<text::FontFace> face, int outline_pixels);
  ~BgfxGlyphAtlas();
  BgfxGlyphAtlas(const BgfxGlyphAtlas&) = delete;
  BgfxGlyphAtlas& operator=(const BgfxGlyphAtlas&) = delete;

  [[nodiscard]] const BgfxAtlasGlyph* Get(std::uint32_t codepoint);
  [[nodiscard]] const BgfxAtlasPage& page(std::size_t index) const;
  [[nodiscard]] const text::FontFace& face() const noexcept { return *face_; }

 private:
  struct Page;

  std::shared_ptr<text::FontFace> face_;
  int outline_pixels_{};
  std::vector<std::unique_ptr<Page>> pages_;
  std::unordered_map<std::uint32_t, BgfxAtlasGlyph> glyphs_;
};

}

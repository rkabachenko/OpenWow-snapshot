#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "openwow/render/resources/fonts/font_face.h"
#include "openwow/vfs/virtual_file_system.h"

namespace openwow::render {

class BgfxGlyphAtlas;

struct BgfxTextKey {
  std::string font_path;
  int height_px{};
  std::string text;
  std::uint32_t abgr{0xffffffffu};
  int wrap_px{};
  int max_height_px{};
  int line_spacing_px{};
  int line_height_override_px{};
  int outline_px{};
  std::uint32_t max_lines{};
  bool word_wrap{true};
  bool non_space_wrap{};
  bool indented_word_wrap{};
  bool monochrome{};

  bool operator==(const BgfxTextKey&) const = default;
};

struct BgfxTextGeometryKey {
  std::string font_path;
  int height_px{};
  std::string text;
  int wrap_px{};
  int max_height_px{};
  int line_spacing_px{};
  int line_height_override_px{};
  int outline_px{};
  std::uint32_t max_lines{};
  bool word_wrap{true};
  bool non_space_wrap{};
  bool indented_word_wrap{};
  bool monochrome{};

  bool operator==(const BgfxTextGeometryKey&) const = default;
};

[[nodiscard]] BgfxTextGeometryKey MakeBgfxTextGeometryKey(
    const BgfxTextKey& key);

struct BgfxTextColorToken {
  std::uint32_t inline_abgr{0xffffffffu};
  bool inherits_base{true};
};

[[nodiscard]] std::uint32_t ResolveBgfxTextVertexColor(
    std::uint32_t base_straight_abgr, const BgfxTextColorToken& token);
[[nodiscard]] std::uint32_t ResolveBgfxTextOutlineVertexColor(
    std::uint32_t base_straight_abgr, const BgfxTextColorToken& token);

[[nodiscard]] std::uint32_t ResolveBgfxTextShadowVertexColor(
    std::uint32_t base_straight_abgr, const BgfxTextColorToken& token);

struct BgfxTextGlyphQuad {
  float x{};
  float y{};
  float width{};
  float height{};
  float u0{};
  float v0{};
  float u1{};
  float v1{};
  std::uint32_t sequence_index{};
  BgfxTextColorToken color;
};

struct BgfxTextAtlasBatch {
  bgfx::TextureHandle body_texture = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle outline_texture = BGFX_INVALID_HANDLE;
  std::vector<BgfxTextGlyphQuad> glyphs;
};

struct BgfxTextHyperlinkBounds {
  float x{};
  float y{};
  float width{};
  float height{};
};

struct BgfxTextHyperlinkSpan {
  std::string link;
  std::string text;
  std::vector<BgfxTextHyperlinkBounds> bounds;
};

struct BgfxTextLayout {
  int width{};
  int height{};
  std::vector<BgfxTextAtlasBatch> batches;
  std::vector<BgfxTextHyperlinkSpan> hyperlinks;
};

class BgfxTextCache {
 public:
  explicit BgfxTextCache(const openwow::vfs::VirtualFileSystem* vfs);
  ~BgfxTextCache();
  BgfxTextCache(const BgfxTextCache&) = delete;
  BgfxTextCache& operator=(const BgfxTextCache&) = delete;

  void Shutdown();
  const BgfxTextLayout* Layout(const BgfxTextKey& key);
  int MeasureLineWidthPx(const std::string& font_path, int height_px,
                         std::string_view text);

 private:
  struct CachedLayout;

  std::shared_ptr<openwow::render::text::FontFace> LoadFace(
      const std::string& font_path, int height_px,
      openwow::render::text::FontStyle style);
  BgfxGlyphAtlas* LoadAtlas(const BgfxTextKey& key);
  std::unique_ptr<CachedLayout> BuildLayout(const BgfxTextKey& key,
                                            BgfxGlyphAtlas& atlas);

  [[nodiscard]] const std::string& NormalizedFontPath(
      const std::string& font_path);

  const openwow::vfs::VirtualFileSystem* vfs_{};
  std::unordered_map<std::string,
                     std::shared_ptr<openwow::render::text::FontFace>>
      faces_;
  std::unordered_map<std::string, std::unique_ptr<BgfxGlyphAtlas>> atlases_;
  std::unordered_map<std::size_t, std::unique_ptr<CachedLayout>> layouts_;
  std::unordered_map<std::string, std::string> normalized_font_path_cache_;
};

}

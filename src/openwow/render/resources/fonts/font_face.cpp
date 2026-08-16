#include "openwow/render/resources/fonts/font_face.h"

#include "openwow/ui/font_asset_path.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace openwow::render::text {
namespace {

constexpr std::uint32_t kFallbackCodepoint = '?';

std::vector<std::uint8_t> ReadFile(std::string path) {
  std::replace(path.begin(), path.end(), '\\', '/');
  if (const std::string built_in = ui::ResolveBuiltInFontAssetPath(path);
      !built_in.empty()) {
    path = built_in;
  }

  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  if (!stream) return {};
  const auto size = stream.tellg();
  if (size <= 0) return {};
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  stream.seekg(0);
  stream.read(reinterpret_cast<char*>(bytes.data()),
              static_cast<std::streamsize>(size));
  return stream ? std::move(bytes) : std::vector<std::uint8_t>{};
}

}

struct FontFace::Impl {
  ~Impl() {
    if (face != nullptr) FT_Done_Face(face);
    if (library != nullptr) FT_Done_FreeType(library);
  }

  std::string path;
  std::vector<std::uint8_t> bytes;
  FontStyle style;
  FT_Library library{};
  FT_Face face{};
  int pixel_height{};
  float line_height{};
  float ascent{};
  mutable std::mutex mutex;
  mutable std::unordered_map<std::uint32_t, GlyphMetrics> glyphs;
  mutable std::unordered_map<std::uint64_t, float> advances;
};

FontFace::FontFace(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
FontFace::~FontFace() = default;

std::shared_ptr<FontFace> FontFace::LoadFile(std::string path,
                                             const int pixel_height,
                                             const FontStyle style) {
  auto bytes = ReadFile(path);
  return LoadMemory(std::move(path), std::move(bytes), pixel_height, style);
}

std::shared_ptr<FontFace> FontFace::LoadMemory(
    std::string path, std::vector<std::uint8_t> bytes,
    const int pixel_height, const FontStyle style) {
  if (bytes.empty() || pixel_height <= 0) return {};

  auto impl = std::make_unique<Impl>();
  impl->path = std::move(path);
  impl->bytes = std::move(bytes);
  impl->style = style;
  impl->pixel_height = pixel_height;
  if (FT_Init_FreeType(&impl->library) != 0 ||
      FT_New_Memory_Face(
          impl->library, reinterpret_cast<const FT_Byte*>(impl->bytes.data()),
          static_cast<FT_Long>(impl->bytes.size()), 0, &impl->face) != 0 ||
      FT_Set_Pixel_Sizes(impl->face, 0,
                         static_cast<FT_UInt>(pixel_height)) != 0) {
    return {};
  }

  const float outline = OutlineCellGrowthPixels(style.outline);
  impl->line_height =
      impl->face->size != nullptr
          ? static_cast<float>(impl->face->size->metrics.height) / 64.0f
          : static_cast<float>(pixel_height);
  impl->ascent =
      impl->face->size != nullptr
          ? static_cast<float>(impl->face->size->metrics.ascender) / 64.0f
          : static_cast<float>(pixel_height);
  impl->line_height = std::max(impl->line_height, 1.0f) + outline;

  impl->ascent += outline > 0.0f ? 1.0f : 0.0f;
  return std::shared_ptr<FontFace>(new FontFace(std::move(impl)));
}

const std::string& FontFace::path() const noexcept { return impl_->path; }
int FontFace::pixel_height() const noexcept { return impl_->pixel_height; }
float FontFace::line_height() const noexcept { return impl_->line_height; }
float FontFace::ascent() const noexcept { return impl_->ascent; }
FontStyle FontFace::style() const noexcept { return impl_->style; }

GlyphMetrics FontFace::Glyph(std::uint32_t codepoint) const {
  std::lock_guard lock(impl_->mutex);
  if (const auto found = impl_->glyphs.find(codepoint);
      found != impl_->glyphs.end()) {
    return found->second;
  }

  FT_UInt glyph = FT_Get_Char_Index(impl_->face, codepoint);
  if (glyph == 0 && codepoint != kFallbackCodepoint) {
    codepoint = kFallbackCodepoint;
    glyph = FT_Get_Char_Index(impl_->face, codepoint);
  }

  GlyphMetrics metrics{.codepoint = codepoint, .glyph_index = glyph};
  if (glyph != 0 && FT_Load_Glyph(impl_->face, glyph, FT_LOAD_DEFAULT) == 0) {
    const auto& slot = *impl_->face->glyph;
    metrics.advance = static_cast<float>(slot.advance.x) / 64.0f;
    metrics.bearing_x = static_cast<float>(slot.metrics.horiBearingX) / 64.0f;
    metrics.bearing_y = static_cast<float>(slot.metrics.horiBearingY) / 64.0f;
    metrics.width = static_cast<float>(slot.metrics.width) / 64.0f;
    metrics.height = static_cast<float>(slot.metrics.height) / 64.0f;
  }
  impl_->glyphs.insert_or_assign(codepoint, metrics);
  return metrics;
}

float FontFace::Advance(const std::uint32_t previous_glyph,
                        const std::uint32_t glyph) const {
  if (glyph == 0) return 0.0f;
  const std::uint64_t key =
      (static_cast<std::uint64_t>(previous_glyph) << 32u) | glyph;
  {
    std::lock_guard lock(impl_->mutex);
    if (const auto found = impl_->advances.find(key);
        found != impl_->advances.end()) {
      return found->second;
    }
  }

  float advance{};
  {
    std::lock_guard lock(impl_->mutex);
    if (FT_Load_Glyph(impl_->face, glyph, FT_LOAD_DEFAULT) == 0) {
      advance = static_cast<float>(impl_->face->glyph->advance.x) / 64.0f;
    }
    if (previous_glyph != 0 && FT_HAS_KERNING(impl_->face)) {
      FT_Vector kerning{};
      if (FT_Get_Kerning(impl_->face, previous_glyph, glyph,
                         FT_KERNING_DEFAULT, &kerning) == 0) {
        advance += static_cast<float>(kerning.x) / 64.0f;
      }
    }
    impl_->advances.emplace(key, advance);
  }
  return advance;
}

RasterizedGlyph FontFace::Rasterize(const std::uint32_t codepoint) const {
  const GlyphMetrics metrics = Glyph(codepoint);
  RasterizedGlyph result{.metrics = metrics};
  if (!metrics) return result;

  std::lock_guard lock(impl_->mutex);
  const FT_Int32 flags =
      impl_->style.monochrome ? FT_LOAD_TARGET_MONO : FT_LOAD_TARGET_NORMAL;
  if (FT_Load_Glyph(impl_->face, metrics.glyph_index, flags) != 0 ||
      FT_Render_Glyph(impl_->face->glyph,
                      impl_->style.monochrome ? FT_RENDER_MODE_MONO
                                              : FT_RENDER_MODE_NORMAL) != 0) {
    return result;
  }

  const FT_Bitmap& bitmap = impl_->face->glyph->bitmap;
  result.width = bitmap.width;
  result.height = bitmap.rows;
  result.pitch = bitmap.width;
  result.alpha.resize(static_cast<std::size_t>(result.width) * result.height);
  for (std::uint32_t y = 0; y < result.height; ++y) {
    const auto* source = bitmap.buffer + y * std::abs(bitmap.pitch);
    auto* destination = result.alpha.data() +
                        static_cast<std::size_t>(y) * result.width;
    if (bitmap.pixel_mode == FT_PIXEL_MODE_MONO) {
      for (std::uint32_t x = 0; x < result.width; ++x) {
        destination[x] =
            (source[x / 8u] & (0x80u >> (x % 8u))) != 0 ? 255u : 0u;
      }
    } else {
      std::copy_n(source, result.width, destination);
    }
  }
  return result;
}

}

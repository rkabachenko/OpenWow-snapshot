#include "openwow/render/backend/bgfx/bgfx_glyph_atlas.h"

#include "openwow/render/resources/fonts/glyph_atlas.h"

#include <algorithm>
#include <span>
#include <utility>

namespace openwow::render {
namespace {

constexpr std::uint16_t kAtlasSize = 1024;

std::vector<std::uint8_t> ToRgba(
    const std::span<const std::uint8_t> coverage) {
  std::vector<std::uint8_t> rgba(coverage.size() * 4u);
  for (std::size_t index = 0; index < coverage.size(); ++index) {
    const std::uint8_t alpha = coverage[index];
    rgba[index * 4u] = alpha;
    rgba[index * 4u + 1u] = alpha;
    rgba[index * 4u + 2u] = alpha;
    rgba[index * 4u + 3u] = alpha;
  }
  return rgba;
}

void Upload(bgfx::TextureHandle& texture,
            const std::span<const std::uint8_t> coverage) {
  texture = bgfx::createTexture2D(
      kAtlasSize, kAtlasSize, false, 1, bgfx::TextureFormat::RGBA8,
      BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
  if (!bgfx::isValid(texture) || coverage.empty()) return;
  const auto rgba = ToRgba(coverage);
  bgfx::updateTexture2D(
      texture, 0, 0, 0, 0, kAtlasSize, kAtlasSize,
      bgfx::copy(rgba.data(), static_cast<std::uint32_t>(rgba.size())));
}

void Update(const bgfx::TextureHandle texture, const AtlasRegion region,
            const std::span<const std::uint8_t> coverage) {
  if (!bgfx::isValid(texture) || coverage.empty()) return;
  const auto rgba = ToRgba(coverage);
  bgfx::updateTexture2D(
      texture, 0, 0, region.x, region.y, region.width, region.height,
      bgfx::copy(rgba.data(), static_cast<std::uint32_t>(rgba.size())));
}

std::vector<std::uint8_t> BuildOutline(
    const std::span<const std::uint8_t> body, const std::uint16_t width,
    const std::uint16_t height, const int radius) {
  if (radius <= 0) return {};
  std::vector<std::uint8_t> outline(body.size());
  for (std::uint16_t y = 0; y < height; ++y) {
    for (std::uint16_t x = 0; x < width; ++x) {
      const auto alpha = body[static_cast<std::size_t>(y) * width + x];
      if (alpha == 0) continue;
      for (int offset_y = -radius; offset_y <= radius; ++offset_y) {
        for (int offset_x = -radius; offset_x <= radius; ++offset_x) {
          const int target_x = static_cast<int>(x) + offset_x;
          const int target_y = static_cast<int>(y) + offset_y;
          if (target_x < 0 || target_y < 0 || target_x >= width ||
              target_y >= height) {
            continue;
          }
          auto& target =
              outline[static_cast<std::size_t>(target_y) * width + target_x];
          target = std::max(target, alpha);
        }
      }
    }
  }
  return outline;
}

}

struct BgfxGlyphAtlas::Page {
  explicit Page(const bool outlined)
      : body(kAtlasSize, kAtlasSize),
        outline(kAtlasSize, kAtlasSize),
        outlined(outlined) {}

  ~Page() {
    if (bgfx::isValid(textures.body)) bgfx::destroy(textures.body);
    if (bgfx::isValid(textures.outline)) bgfx::destroy(textures.outline);
  }

  GlyphAtlas body;
  GlyphAtlas outline;
  BgfxAtlasPage textures;
  bool outlined{};
};

BgfxGlyphAtlas::BgfxGlyphAtlas(std::shared_ptr<text::FontFace> face,
                               const int outline_pixels)
    : face_(std::move(face)),
      outline_pixels_(std::clamp(outline_pixels, 0, 4)) {}

BgfxGlyphAtlas::~BgfxGlyphAtlas() = default;

const BgfxAtlasGlyph* BgfxGlyphAtlas::Get(const std::uint32_t codepoint) {
  if (const auto found = glyphs_.find(codepoint); found != glyphs_.end()) {
    return &found->second;
  }

  const auto raster = face_->Rasterize(codepoint);
  if (!raster.metrics) return nullptr;
  if (raster.width == 0 || raster.height == 0) {
    return &glyphs_
                .emplace(codepoint,
                         BgfxAtlasGlyph{
                             .bearing_x = raster.metrics.bearing_x,
                             .bearing_y = raster.metrics.bearing_y,
                         })
                .first->second;
  }

  const auto padding = static_cast<std::uint16_t>(outline_pixels_);
  const auto width = static_cast<std::uint16_t>(raster.width + padding * 2u);
  const auto height =
      static_cast<std::uint16_t>(raster.height + padding * 2u);
  std::vector<std::uint8_t> body(static_cast<std::size_t>(width) * height);
  for (std::uint32_t row = 0; row < raster.height; ++row) {
    std::copy_n(
        raster.alpha.data() + static_cast<std::size_t>(row) * raster.pitch,
        raster.width,
        body.data() + static_cast<std::size_t>(row + padding) * width +
            padding);
  }
  const auto outline = BuildOutline(body, width, height, outline_pixels_);

  if (pages_.empty()) {
    pages_.push_back(std::make_unique<Page>(outline_pixels_ > 0));
  }
  Page* page = pages_.back().get();
  auto region = page->body.Insert(body, width, height, width);
  if (!region) {
    pages_.push_back(std::make_unique<Page>(outline_pixels_ > 0));
    page = pages_.back().get();
    region = page->body.Insert(body, width, height, width);
  }
  if (!region) return nullptr;
  if (page->outlined &&
      !page->outline.Insert(outline, width, height, width)) {
    return nullptr;
  }

  if (bgfx::isValid(page->textures.body)) {
    Update(page->textures.body, region, body);
    if (page->outlined) Update(page->textures.outline, region, outline);
  } else {
    Upload(page->textures.body, page->body.pixels());
    if (page->outlined) Upload(page->textures.outline, page->outline.pixels());
  }

  constexpr float inverse_size = 1.0f / kAtlasSize;
  return &glyphs_
              .emplace(codepoint,
                       BgfxAtlasGlyph{
                           .page = pages_.size() - 1,
                           .u0 = region.x * inverse_size,
                           .v0 = region.y * inverse_size,
                           .u1 = (region.x + region.width) * inverse_size,
                           .v1 = (region.y + region.height) * inverse_size,
                           .width = static_cast<float>(width),
                           .height = static_cast<float>(height),
                           .bearing_x = raster.metrics.bearing_x - padding,
                           .bearing_y = raster.metrics.bearing_y + padding,
                       })
              .first->second;
}

const BgfxAtlasPage& BgfxGlyphAtlas::page(const std::size_t index) const {
  return pages_[index]->textures;
}

}

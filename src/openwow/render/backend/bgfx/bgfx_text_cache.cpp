#include "openwow/render/backend/bgfx/bgfx_text_cache.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

#include "openwow/render/backend/bgfx/bgfx_glyph_atlas.h"
#include "openwow/render/resources/fonts/font_string_flags.h"
#include "openwow/render/resources/fonts/text_layout.h"

namespace openwow::render {
namespace {

constexpr std::size_t kMaximumLayouts = 2048;

std::string NormalizePath(std::string path) {
  std::replace(path.begin(), path.end(), '\\', '/');
  if (!path.empty() && path.front() != '/') path.insert(path.begin(), '/');
  return path;
}

void HashCombine(std::size_t& seed, const std::size_t value) {
  seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6u) + (seed >> 2u);
}

std::size_t HashGeometry(const BgfxTextKey& key, const std::string& font_path,
                         const int outline_px) {
  std::size_t hash = std::hash<std::string>{}(font_path);
  HashCombine(hash, std::hash<int>{}(key.height_px));
  HashCombine(hash, std::hash<std::string>{}(key.text));
  HashCombine(hash, std::hash<int>{}(key.wrap_px));
  HashCombine(hash, std::hash<int>{}(key.max_height_px));
  HashCombine(hash, std::hash<int>{}(key.line_spacing_px));
  HashCombine(hash, std::hash<int>{}(key.line_height_override_px));
  HashCombine(hash, std::hash<int>{}(outline_px));
  HashCombine(hash, std::hash<std::uint32_t>{}(key.max_lines));
  HashCombine(hash, std::hash<bool>{}(key.word_wrap));
  HashCombine(hash, std::hash<bool>{}(key.non_space_wrap));
  HashCombine(hash, std::hash<bool>{}(key.indented_word_wrap));
  HashCombine(hash, std::hash<bool>{}(key.monochrome));
  return hash;
}

bool GeometryKeyMatches(const BgfxTextGeometryKey& stored,
                        const BgfxTextKey& key, const std::string& font_path,
                        const int outline_px) {
  return stored.height_px == key.height_px && stored.wrap_px == key.wrap_px &&
         stored.max_height_px == key.max_height_px &&
         stored.line_spacing_px == key.line_spacing_px &&
         stored.line_height_override_px == key.line_height_override_px &&
         stored.outline_px == outline_px &&
         stored.max_lines == key.max_lines &&
         stored.word_wrap == key.word_wrap &&
         stored.non_space_wrap == key.non_space_wrap &&
         stored.indented_word_wrap == key.indented_word_wrap &&
         stored.monochrome == key.monochrome && stored.text == key.text &&
         stored.font_path == font_path;
}

openwow::render::text::FontOutline OutlineFromRingPixels(
    const int outline_px) {
  if (outline_px >= kRetailThickOutlinePixels) {
    return openwow::render::text::FontOutline::Thick;
  }
  if (outline_px >= kRetailNormalOutlinePixels) {
    return openwow::render::text::FontOutline::Normal;
  }
  return openwow::render::text::FontOutline::None;
}

std::uint32_t ArgbToAbgr(const std::uint32_t argb) {
  return (argb & 0xff00ff00u) | ((argb & 0x00ff0000u) >> 16u) |
         ((argb & 0x000000ffu) << 16u);
}

openwow::render::text::TextLayoutRequest MakeRequest(const BgfxTextKey& key) {
  openwow::render::text::TextLayoutRequest request;
  request.maximum_width = static_cast<float>(std::max(0, key.wrap_px));
  request.maximum_height = static_cast<float>(std::max(0, key.max_height_px));
  request.line_spacing = static_cast<float>(key.line_spacing_px);
  request.line_height =
      static_cast<float>(std::max(0, key.line_height_override_px));
  request.maximum_lines = key.max_lines;
  request.wrap = key.wrap_px > 0 ? openwow::render::text::ResolveWrapMode(
                                       key.word_wrap, key.non_space_wrap)
                                 : openwow::render::text::WrapMode::None;
  request.indent_continuation_lines = key.indented_word_wrap;
  return request;
}

std::vector<BgfxTextColorToken> ResolveColors(
    const openwow::render::text::TextLayout& layout) {
  std::vector<BgfxTextColorToken> colors(layout.tokens.size());
  BgfxTextColorToken current;
  for (std::size_t index = 0; index < layout.tokens.size(); ++index) {
    const auto& token = layout.tokens[index];
    if (token.kind == openwow::render::text::FormattedTokenKind::Color) {
      current = {.inline_abgr = ArgbToAbgr(token.color_argb),
                 .inherits_base = false};
    } else if (token.kind ==
               openwow::render::text::FormattedTokenKind::ResetColor) {
      current = {};
    }
    colors[index] = current;
  }
  return colors;
}

struct ParsedHyperlinkSpan {
  std::size_t first_token{};
  std::size_t last_token{};
  std::string link;
  std::string text;
};

std::vector<ParsedHyperlinkSpan> ParseHyperlinkSpans(
    const std::string_view source,
    const std::vector<openwow::render::text::FormattedToken>& tokens) {
  using openwow::render::text::FormattedTokenKind;
  std::vector<ParsedHyperlinkSpan> spans;
  for (std::size_t start = 0; start < tokens.size(); ++start) {
    const auto& start_token = tokens[start];
    if (start_token.kind != FormattedTokenKind::HyperlinkStart ||
        start_token.end < start_token.begin + 4u ||
        start_token.end > source.size()) {
      continue;
    }
    const auto close =
        std::find_if(tokens.begin() + static_cast<std::ptrdiff_t>(start + 1u),
                     tokens.end(), [](const auto& token) {
                       return token.kind == FormattedTokenKind::HyperlinkEnd;
                     });
    if (close == tokens.end()) {
      break;
    }
    const std::size_t close_index =
        static_cast<std::size_t>(std::distance(tokens.begin(), close));
    std::size_t text_begin = start_token.begin;
    std::size_t text_end = close->end;
    if (start != 0u && tokens[start - 1u].kind == FormattedTokenKind::Color &&
        tokens[start - 1u].end == start_token.begin) {
      text_begin = tokens[start - 1u].begin;
    }
    if (close_index + 1u < tokens.size() &&
        tokens[close_index + 1u].kind == FormattedTokenKind::ResetColor &&
        tokens[close_index + 1u].begin == close->end) {
      text_end = tokens[close_index + 1u].end;
    }
    spans.push_back({
        .first_token = start + 1u,
        .last_token = close_index,
        .link = std::string(source.substr(
            start_token.begin + 2u, start_token.end - start_token.begin - 4u)),
        .text = std::string(source.substr(text_begin, text_end - text_begin)),
    });
    start = close_index;
  }
  return spans;
}

}

BgfxTextGeometryKey MakeBgfxTextGeometryKey(const BgfxTextKey& key) {
  return {
      .font_path = key.font_path,
      .height_px = key.height_px,
      .text = key.text,
      .wrap_px = key.wrap_px,
      .max_height_px = key.max_height_px,
      .line_spacing_px = key.line_spacing_px,
      .line_height_override_px = key.line_height_override_px,
      .outline_px = key.outline_px,
      .max_lines = key.max_lines,
      .word_wrap = key.word_wrap,
      .non_space_wrap = key.non_space_wrap,
      .indented_word_wrap = key.indented_word_wrap,
      .monochrome = key.monochrome,
  };
}

std::uint32_t ResolveBgfxTextVertexColor(const std::uint32_t base,
                                         const BgfxTextColorToken& token) {
  const std::uint32_t straight =
      token.inherits_base ? base
                          : (token.inline_abgr & 0x00ffffffu) |
                                ((((base >> 24u) & 0xffu) *
                                      ((token.inline_abgr >> 24u) & 0xffu) +
                                  127u) /
                                     255u
                                 << 24u);
  const std::uint32_t alpha = straight >> 24u;
  const auto premultiply = [alpha](const std::uint32_t component) {
    return (component * alpha + 127u) / 255u;
  };
  return (alpha << 24u) | (premultiply((straight >> 16u) & 0xffu) << 16u) |
         (premultiply((straight >> 8u) & 0xffu) << 8u) |
         premultiply(straight & 0xffu);
}

std::uint32_t ResolveBgfxTextOutlineVertexColor(
    const std::uint32_t base, const BgfxTextColorToken& token) {
  const std::uint32_t inline_alpha =
      token.inherits_base ? 255u : token.inline_abgr >> 24u;
  return ((((base >> 24u) & 0xffu) * inline_alpha + 127u) / 255u) << 24u;
}

std::uint32_t ResolveBgfxTextShadowVertexColor(
    const std::uint32_t base, const BgfxTextColorToken& token) {

  const std::uint32_t inline_alpha =
      token.inherits_base ? 255u : token.inline_abgr >> 24u;
  const std::uint32_t alpha =
      (((base >> 24u) & 0xffu) * inline_alpha + 127u) / 255u;
  const auto premultiply = [alpha](const std::uint32_t component) {
    return (component * alpha + 127u) / 255u;
  };
  return (alpha << 24u) | (premultiply((base >> 16u) & 0xffu) << 16u) |
         (premultiply((base >> 8u) & 0xffu) << 8u) | premultiply(base & 0xffu);
}

struct BgfxTextCache::CachedLayout {
  BgfxTextGeometryKey key;
  BgfxTextLayout layout;
};

BgfxTextCache::BgfxTextCache(const openwow::vfs::VirtualFileSystem* vfs)
    : vfs_(vfs) {}

BgfxTextCache::~BgfxTextCache() { Shutdown(); }

void BgfxTextCache::Shutdown() {
  layouts_.clear();
  atlases_.clear();
  faces_.clear();
  normalized_font_path_cache_.clear();
}

const std::string& BgfxTextCache::NormalizedFontPath(
    const std::string& font_path) {
  if (const auto found = normalized_font_path_cache_.find(font_path);
      found != normalized_font_path_cache_.end()) {
    return found->second;
  }
  return normalized_font_path_cache_.emplace(font_path,
                                             NormalizePath(font_path))
      .first->second;
}

std::shared_ptr<openwow::render::text::FontFace> BgfxTextCache::LoadFace(
    const std::string& font_path, const int height_px,
    const openwow::render::text::FontStyle style) {

  const std::string& path = NormalizedFontPath(font_path);
  const std::string key =
      path + "#" + std::to_string(height_px) + "#" +
      std::to_string(static_cast<int>(style.outline)) +
      (style.monochrome ? "#mono" : "#gray");
  if (const auto found = faces_.find(key); found != faces_.end()) {
    return found->second;
  }
  if (vfs_ == nullptr || height_px <= 0) return {};
  const auto bytes = vfs_->ReadFileBytes(path);
  if (!bytes || bytes->empty()) return {};
  auto face =
      openwow::render::text::FontFace::LoadMemory(path, *bytes, height_px,
                                                  style);
  if (face) faces_.emplace(key, face);
  return face;
}

BgfxGlyphAtlas* BgfxTextCache::LoadAtlas(const BgfxTextKey& key) {
  const int outline = std::clamp(key.outline_px, 0, 4);
  const std::string cache_key =
      key.font_path + "#" + std::to_string(key.height_px) + "#" +
      std::to_string(outline) + (key.monochrome ? "#mono" : "#gray");
  if (const auto found = atlases_.find(cache_key); found != atlases_.end()) {
    return found->second.get();
  }
  auto face = LoadFace(key.font_path, key.height_px,
                       {.outline = OutlineFromRingPixels(outline),
                        .monochrome = key.monochrome});
  if (!face) return nullptr;
  auto atlas = std::make_unique<BgfxGlyphAtlas>(std::move(face), outline);
  auto* result = atlas.get();
  atlases_.emplace(cache_key, std::move(atlas));
  return result;
}

std::unique_ptr<BgfxTextCache::CachedLayout> BgfxTextCache::BuildLayout(
    const BgfxTextKey& key, BgfxGlyphAtlas& atlas) {
  const auto text_layout = openwow::render::text::LayoutText(
      atlas.face(), key.text, MakeRequest(key));
  const auto colors = ResolveColors(text_layout);
  const auto parsed_hyperlinks =
      ParseHyperlinkSpans(key.text, text_layout.tokens);
  auto cached = std::make_unique<CachedLayout>();
  cached->key = MakeBgfxTextGeometryKey(key);

  cached->layout.width =
      std::max(1, static_cast<int>(std::ceil(text_layout.width)) +
                      std::clamp(key.outline_px, 0, 4) * 2);

  cached->layout.height =
      std::max(1, static_cast<int>(std::ceil(text_layout.height)));

  std::uint32_t sequence_index = 0;
  std::vector<std::optional<std::size_t>> hyperlink_by_token(
      text_layout.tokens.size());
  for (std::size_t hyperlink = 0; hyperlink < parsed_hyperlinks.size();
       ++hyperlink) {
    const auto& span = parsed_hyperlinks[hyperlink];
    for (std::size_t token = span.first_token;
         token < span.last_token && token < hyperlink_by_token.size();
         ++token) {
      hyperlink_by_token[token] = hyperlink;
    }
  }
  cached->layout.hyperlinks.reserve(parsed_hyperlinks.size());
  for (const auto& span : parsed_hyperlinks) {
    cached->layout.hyperlinks.push_back({.link = span.link, .text = span.text});
  }
  std::vector<float> hyperlink_last_glyph_x(parsed_hyperlinks.size(), 0.0f);
  std::vector<bool> hyperlink_has_glyph(parsed_hyperlinks.size(), false);
  for (const auto& element : text_layout.elements) {
    const auto& token = text_layout.tokens[element.token_index];
    if (token.kind != openwow::render::text::FormattedTokenKind::Glyph) {
      continue;
    }
    const std::uint32_t glyph_sequence_index = sequence_index++;
    const auto* glyph = atlas.Get(token.codepoint);
    if (glyph == nullptr || glyph->width <= 0.0f || glyph->height <= 0.0f) {
      continue;
    }
    if (cached->layout.batches.size() <= glyph->page) {
      cached->layout.batches.resize(glyph->page + 1u);
    }
    const auto& page = atlas.page(glyph->page);
    auto& batch = cached->layout.batches[glyph->page];
    batch.body_texture = page.body;
    batch.outline_texture = page.outline;
    const float outline_pixels =
        static_cast<float>(std::clamp(key.outline_px, 0, 4));

    const BgfxTextGlyphQuad quad{
        .x = element.x + std::floor(glyph->bearing_x) + outline_pixels,
        .y = std::floor(element.y + atlas.face().ascent() -
                        glyph->bearing_y) +
             outline_pixels,
        .width = glyph->width,
        .height = glyph->height,
        .u0 = glyph->u0,
        .v0 = glyph->v0,
        .u1 = glyph->u1,
        .v1 = glyph->v1,
        .sequence_index = glyph_sequence_index,
        .color = colors[element.token_index],
    };
    batch.glyphs.push_back(quad);
    if (element.token_index >= hyperlink_by_token.size()) {
      continue;
    }
    const auto hyperlink = hyperlink_by_token[element.token_index];
    if (!hyperlink.has_value()) {
      continue;
    }
    auto& bounds = cached->layout.hyperlinks[*hyperlink].bounds;
    const bool wrapped = hyperlink_has_glyph[*hyperlink] &&
                         (quad.x + 0.5f < hyperlink_last_glyph_x[*hyperlink] ||
                          quad.y >= bounds.back().y + bounds.back().height ||
                          quad.y + quad.height <= bounds.back().y);
    if (bounds.empty() || wrapped) {
      bounds.push_back({.x = quad.x,
                        .y = quad.y,
                        .width = quad.width,
                        .height = quad.height});
    } else {
      auto& line = bounds.back();
      const float right = std::max(line.x + line.width, quad.x + quad.width);
      const float bottom = std::max(line.y + line.height, quad.y + quad.height);
      line.x = std::min(line.x, quad.x);
      line.y = std::min(line.y, quad.y);
      line.width = right - line.x;
      line.height = bottom - line.y;
    }
    hyperlink_last_glyph_x[*hyperlink] = quad.x;
    hyperlink_has_glyph[*hyperlink] = true;
  }
  return cached;
}

const BgfxTextLayout* BgfxTextCache::Layout(const BgfxTextKey& input) {

  const std::string& font_path = NormalizedFontPath(input.font_path);
  const int outline_px = std::clamp(input.outline_px, 0, 4);
  if (font_path.empty() || input.height_px <= 0 || input.text.empty()) {
    return nullptr;
  }
  const std::size_t hash = HashGeometry(input, font_path, outline_px);
  if (const auto found = layouts_.find(hash);
      found != layouts_.end() &&
      GeometryKeyMatches(found->second->key, input, font_path, outline_px)) {
    return &found->second->layout;
  }

  BgfxTextKey key = input;
  key.font_path = font_path;
  key.outline_px = outline_px;
  auto* atlas = LoadAtlas(key);
  if (atlas == nullptr) return nullptr;
  auto layout = BuildLayout(key, *atlas);
  if (!layout) return nullptr;
  if (layouts_.size() >= kMaximumLayouts && !layouts_.contains(hash)) {
    layouts_.erase(layouts_.begin());
  }
  const auto* result = &layout->layout;
  layouts_.insert_or_assign(hash, std::move(layout));
  return result;
}

int BgfxTextCache::MeasureLineWidthPx(const std::string& font_path,
                                      const int height_px,
                                      const std::string_view text) {

  auto face = LoadFace(font_path, height_px, {});
  if (!face || text.empty()) return 0;
  openwow::render::text::TextLayoutRequest request;
  request.formatting_enabled = false;
  const auto layout = openwow::render::text::LayoutText(*face, text, request);
  return static_cast<int>(std::lround(layout.width));
}

}

#include "openwow/ui/screens/login_scene.h"

#include "openwow/foundation/diagnostics/logging.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cstdint>

namespace openwow::ui::screens {

namespace {

std::vector<std::uint32_t> DecodeUtf8(const std::string &text) {
  std::vector<std::uint32_t> out;
  out.reserve(text.size());
  std::size_t i = 0;
  while (i < text.size()) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (c < 0x80) {
      out.push_back(static_cast<std::uint32_t>(c));
      ++i;
      continue;
    }
    auto fail = [&]() {
      out.push_back(0xFFFDu);
      ++i;
    };
    if ((c & 0xE0) == 0xC0) {
      if (i + 1 >= text.size()) {
        fail();
        continue;
      }
      const unsigned char c1 = static_cast<unsigned char>(text[i + 1]);
      if ((c1 & 0xC0) != 0x80) {
        fail();
        continue;
      }
      const std::uint32_t cp = ((c & 0x1Fu) << 6) | (c1 & 0x3Fu);
      out.push_back(cp);
      i += 2;
      continue;
    }
    if ((c & 0xF0) == 0xE0) {
      if (i + 2 >= text.size()) {
        fail();
        continue;
      }
      const unsigned char c1 = static_cast<unsigned char>(text[i + 1]);
      const unsigned char c2 = static_cast<unsigned char>(text[i + 2]);
      if (((c1 & 0xC0) != 0x80) || ((c2 & 0xC0) != 0x80)) {
        fail();
        continue;
      }
      const std::uint32_t cp = ((c & 0x0Fu) << 12) | ((c1 & 0x3Fu) << 6) | (c2 & 0x3Fu);
      out.push_back(cp);
      i += 3;
      continue;
    }
    if ((c & 0xF8) == 0xF0) {
      if (i + 3 >= text.size()) {
        fail();
        continue;
      }
      const unsigned char c1 = static_cast<unsigned char>(text[i + 1]);
      const unsigned char c2 = static_cast<unsigned char>(text[i + 2]);
      const unsigned char c3 = static_cast<unsigned char>(text[i + 3]);
      if (((c1 & 0xC0) != 0x80) || ((c2 & 0xC0) != 0x80) || ((c3 & 0xC0) != 0x80)) {
        fail();
        continue;
      }
      const std::uint32_t cp =
          ((c & 0x07u) << 18) | ((c1 & 0x3Fu) << 12) | ((c2 & 0x3Fu) << 6) | (c3 & 0x3Fu);
      out.push_back(cp);
      i += 4;
      continue;
    }
    fail();
  }
  return out;
}

struct LineLayout {
  std::size_t begin{0};
  std::size_t end{0};
  int width_px{0};
};

enum class TextWrapMode : std::uint8_t { None = 0, WordWrap, CharWrap };

TextWrapMode ResolveWrapMode(const bool word_wrap,
                             const bool non_space_wrap,
                             const int wrap_px) {
  if (wrap_px <= 0) {
    return TextWrapMode::None;
  }
  if (non_space_wrap) {
    return TextWrapMode::CharWrap;
  }
  if (word_wrap) {
    return TextWrapMode::WordWrap;
  }
  return TextWrapMode::None;
}

}

SDL_Texture *LoginScene::RenderText(SDL_Renderer *renderer, const std::string &cache_key,
                                    FontEntry *font, const std::string &text, SDL_Color color,
                                    int wrap_px, bool word_wrap, bool non_space_wrap,
                                    int *out_w, int *out_h) {
  if (out_w)
    *out_w = 0;
  if (out_h)
    *out_h = 0;
  const FT_Face face = (font != nullptr) ? static_cast<FT_Face>(font->face) : nullptr;
  if (renderer == nullptr || font == nullptr || face == nullptr || text.empty()) {
    return nullptr;
  }

  const std::uint32_t rgba =
      (static_cast<std::uint32_t>(color.r) << 24) | (static_cast<std::uint32_t>(color.g) << 16) |
      (static_cast<std::uint32_t>(color.b) << 8) | static_cast<std::uint32_t>(color.a);

  auto &entry = text_cache_[cache_key];
  const std::string font_key = font->path + "#" + std::to_string(font->height_px);
  const TextWrapMode wrap_mode = ResolveWrapMode(word_wrap, non_space_wrap, wrap_px);
  if (entry.texture != nullptr && entry.text == text && entry.font_key == font_key &&
      entry.rgba == rgba && entry.wrap_px == wrap_px &&
      entry.word_wrap == word_wrap && entry.non_space_wrap == non_space_wrap) {
    if (out_w)
      *out_w = entry.w;
    if (out_h)
      *out_h = entry.h;
    return entry.texture;
  }

  if (entry.texture) {
    SDL_DestroyTexture(entry.texture);
    entry.texture = nullptr;
  }

  const auto cps = DecodeUtf8(text);

  std::vector<LineLayout> lines;
  lines.reserve(4);
  std::size_t line_begin = 0;
  std::size_t last_break = std::string::npos;
  int width = 0;
  FT_UInt prev_glyph = 0;

  auto flush_line = [&](std::size_t end) {
    LineLayout line;
    line.begin = line_begin;
    line.end = end;
    line.width_px = width;
    lines.push_back(line);
  };

  auto glyph_advance = [&](FT_UInt glyph, FT_UInt prev_glyph) -> int {
    if (glyph == 0)
      return 0;
    FT_Vector kern{};
    int kern_x = 0;
    if (prev_glyph != 0 && FT_HAS_KERNING(face)) {
      if (FT_Get_Kerning(face, prev_glyph, glyph, FT_KERNING_DEFAULT, &kern) == 0) {
        kern_x = static_cast<int>(kern.x >> 6);
      }
    }
    if (FT_Load_Glyph(face, glyph, FT_LOAD_DEFAULT) != 0) {
      return kern_x;
    }
    const int adv = static_cast<int>(face->glyph->advance.x >> 6);
    return kern_x + adv;
  };

  for (std::size_t i = 0; i < cps.size(); ++i) {
    const std::uint32_t cp = cps[i];
    if (cp == '\n') {
      flush_line(i);
      line_begin = i + 1;
      width = 0;
      last_break = std::string::npos;
      prev_glyph = 0;
      continue;
    }

    const FT_UInt glyph = FT_Get_Char_Index(face, static_cast<FT_ULong>(cp));
    const int adv = glyph_advance(glyph, prev_glyph);

    if (wrap_px > 0 && wrap_mode != TextWrapMode::None &&
        width + adv > wrap_px && line_begin < i) {
      if (wrap_mode == TextWrapMode::WordWrap &&
          last_break != std::string::npos && last_break >= line_begin) {
        flush_line(last_break);
        line_begin = last_break + 1;
        i = line_begin - 1;
      } else if (wrap_mode == TextWrapMode::CharWrap) {
        flush_line(i);
        line_begin = i;
        i = line_begin - 1;
      } else {
        width += adv;
        prev_glyph = glyph;
        continue;
      }
      width = 0;
      last_break = std::string::npos;
      prev_glyph = 0;
      continue;
    }

    width += adv;
    if (wrap_mode == TextWrapMode::WordWrap && cp == ' ') {
      last_break = i;
    }
    prev_glyph = glyph;
  }
  flush_line(cps.size());

  int max_w = 0;
  for (const auto &line : lines) {
    max_w = std::max(max_w, line.width_px);
  }
  const int out_width = std::max(1, max_w);
  const int out_height = std::max(1, static_cast<int>(lines.size()) * font->line_height_px);

  std::vector<std::uint8_t> pixels(
      static_cast<std::size_t>(out_width) * static_cast<std::size_t>(out_height) * 4u, 0u);

  auto blend_pixel = [&](int x, int y, std::uint8_t cov) {
    if (x < 0 || y < 0 || x >= out_width || y >= out_height)
      return;
    const std::size_t idx = (static_cast<std::size_t>(y) * static_cast<std::size_t>(out_width) +
                             static_cast<std::size_t>(x)) *
                            4u;
    const std::uint32_t a =
        static_cast<std::uint32_t>(cov) * static_cast<std::uint32_t>(color.a) / 255u;
    pixels[idx + 0] = color.r;
    pixels[idx + 1] = color.g;
    pixels[idx + 2] = color.b;
    pixels[idx + 3] = static_cast<std::uint8_t>(std::max<std::uint32_t>(pixels[idx + 3], a));
  };

  for (std::size_t line_idx = 0; line_idx < lines.size(); ++line_idx) {
    const auto &line = lines[line_idx];
    int pen_x = 0;
    FT_UInt prev = 0;
    const int baseline_y = static_cast<int>(line_idx) * font->line_height_px + font->ascent_px;

    for (std::size_t i = line.begin; i < line.end; ++i) {
      const std::uint32_t cp = cps[i];
      if (cp == ' ') {
        const FT_UInt glyph = FT_Get_Char_Index(face, static_cast<FT_ULong>(cp));
        pen_x += glyph_advance(glyph, prev);
        prev = glyph;
        continue;
      }

      const FT_UInt glyph = FT_Get_Char_Index(face, static_cast<FT_ULong>(cp));

      if (prev != 0 && FT_HAS_KERNING(face)) {
        FT_Vector kern{};
        if (FT_Get_Kerning(face, prev, glyph, FT_KERNING_DEFAULT, &kern) == 0) {
          pen_x += static_cast<int>(kern.x >> 6);
        }
      }

      if (FT_Load_Char(face, static_cast<FT_ULong>(cp), FT_LOAD_RENDER) != 0) {
        prev = glyph;
        continue;
      }

      const FT_GlyphSlot slot = face->glyph;
      const int x0 = pen_x + static_cast<int>(slot->bitmap_left);
      const int y0 = baseline_y - static_cast<int>(slot->bitmap_top);

      const FT_Bitmap &bmp = slot->bitmap;
      for (int row = 0; row < static_cast<int>(bmp.rows); ++row) {
        for (int col = 0; col < static_cast<int>(bmp.width); ++col) {
          const std::uint8_t cov = bmp.buffer[row * bmp.pitch + col];
          if (cov == 0)
            continue;
          blend_pixel(x0 + col, y0 + row, cov);
        }
      }

      pen_x += static_cast<int>(slot->advance.x >> 6);
      prev = glyph;
    }
  }

  SDL_Texture *tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC,
                                       out_width, out_height);
  if (!tex) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       std::string("SDL_CreateTexture failed (text): ") + SDL_GetError());
    return nullptr;
  }
  SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
  if (SDL_UpdateTexture(tex, nullptr, pixels.data(), out_width * 4) != 0) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       std::string("SDL_UpdateTexture failed (text): ") + SDL_GetError());
    SDL_DestroyTexture(tex);
    return nullptr;
  }

  entry.texture = tex;
  entry.w = out_width;
  entry.h = out_height;
  entry.text = text;
  entry.font_key = font_key;
  entry.rgba = rgba;
  entry.wrap_px = wrap_px;
  entry.word_wrap = word_wrap;
  entry.non_space_wrap = non_space_wrap;

  if (out_w)
    *out_w = entry.w;
  if (out_h)
    *out_h = entry.h;
  return entry.texture;
}

}

#include "openwow/render/resources/fonts/text_layout.h"

#include "openwow/foundation/compiler/wide_ctype.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace openwow::render::text {
namespace {

bool IsLineBreakOpportunity(const std::uint32_t previous,
                            const std::uint32_t current) {
  if (previous == 0u) return false;
  if (previous == '-' || previous == ';' || previous == '/' ||
      current == '|') {
    return true;
  }
  if (openwow::compiler::IsUnicodeWhitespace(previous)) return false;
  if (openwow::compiler::IsUnicodeWhitespace(current)) return true;

  switch (current) {
    case '$':
    case '(':
    case '[':
    case '\\':
    case '{':
    case 0x2018:
    case 0x201c:
    case 0x2035:
    case 0x3008:
    case 0x300a:
    case 0x300c:
    case 0x300e:
    case 0x3010:
    case 0x3014:
    case 0xff08:
    case 0xff3b:
    case 0xff5b:
    case 0xfe59:
    case 0xfe5b:
    case 0xfe5d:
    case 0xff04:
    case 0xffe1:
    case 0xffe5:
    case 0xffe6:
      return false;
    default:
      break;
  }

  switch (previous) {
    case '!':
    case '%':
    case ')':
    case ',':
    case '.':
    case ':':
    case ';':
    case '?':
    case ']':
    case '}':
    case 0x00b0:
    case 0x00b7:
    case 0x2017:
    case 0x2019:
    case 0x201d:
    case 0x2022:
    case 0x2026:
    case 0x2027:
    case 0x2032:
    case 0x2033:
    case 0x2103:
    case 0x3001:
    case 0x3002:
    case 0x3009:
    case 0x300b:
    case 0x300d:
    case 0x300f:
    case 0x3011:
    case 0x3015:
    case 0x301e:
    case 0x30fc:
    case 0xfe30:
    case 0xfe50:
    case 0xfe51:
    case 0xfe52:
    case 0xfe54:
    case 0xfe55:
    case 0xfe56:
    case 0xfe57:
    case 0xfe5a:
    case 0xfe5c:
    case 0xfe5e:
    case 0xff01:
    case 0xff05:
    case 0xff09:
    case 0xff0c:
    case 0xff0e:
    case 0xff1a:
    case 0xff1b:
    case 0xff1f:
    case 0xff3d:
    case 0xff5d:
    case 0xff70:
    case 0xff9e:
    case 0xff9f:
    case 0xffe0:
      return false;
    default:
      break;
  }

  const auto in = [](const std::uint32_t value, const std::uint32_t first,
                     const std::uint32_t last) {
    return value >= first && value <= last;
  };
  return in(previous, 0x1100, 0x11ff) || in(previous, 0x3000, 0xd7af) ||
         in(previous, 0xf900, 0xfaff) || in(previous, 0xff00, 0xff9f) ||
         in(previous, 0xffa0, 0xffdc);
}

bool IsControl(const FormattedTokenKind kind) {
  return kind == FormattedTokenKind::Color ||
         kind == FormattedTokenKind::ResetColor ||
         kind == FormattedTokenKind::HyperlinkStart ||
         kind == FormattedTokenKind::HyperlinkEnd;
}

float InlineWidth(const FormattedToken& token, const FontFace& face,
                  const TextLayoutRequest& request) {
  return token.image.width.value_or(static_cast<float>(face.pixel_height())) *
         request.inline_image_scale;
}

float InlineHeight(const FormattedToken& token, const FontFace& face,
                   const TextLayoutRequest& request) {
  return token.image.height.value_or(static_cast<float>(face.pixel_height())) *
         request.inline_image_scale;
}

struct MeasuredToken {
  float advance{};
  float height{};
  std::uint32_t glyph{};
};

MeasuredToken MeasureToken(const FormattedToken& token, const FontFace& face,
                           const TextLayoutRequest& request,
                           const std::uint32_t previous_glyph) {
  if (token.kind == FormattedTokenKind::Glyph) {
    const GlyphMetrics glyph = face.Glyph(token.codepoint);

    return {.advance = std::floor(
                face.Advance(previous_glyph, glyph.glyph_index) + 0.5f),
            .height = static_cast<float>(face.pixel_height()),
            .glyph = glyph.glyph_index};
  }
  if (token.kind == FormattedTokenKind::InlineImage) {
    return {.advance = InlineWidth(token, face, request),
            .height = InlineHeight(token, face, request)};
  }
  return {};
}

std::uint32_t AllowedLines(const TextLayoutRequest& request,
                           const float default_line_height) {
  std::uint32_t count = request.maximum_lines == 0u
                            ? std::numeric_limits<std::uint32_t>::max()
                            : request.maximum_lines;
  if (request.maximum_height > 0.0f) {
    const float height = std::max(default_line_height, 1.0f);
    count = std::min(
        count, std::max(1u, static_cast<std::uint32_t>(
                               std::floor(request.maximum_height / height))));
  }
  return count;
}

}

TextLayout LayoutText(const FontFace& face, const std::string_view source,
                      const TextLayoutRequest& request) {
  TextLayout result;
  result.tokens =
      TokenizeFormattedText(source, request.formatting_enabled);
  if (result.tokens.empty()) return result;

  const float base_line_height =
      std::max(1.0f, (request.line_height > 0.0f
                          ? request.line_height
                          : static_cast<float>(face.pixel_height())) +
                         OutlineCellGrowthPixels(face.style().outline));
  const float normal_line_height =
      std::max(1.0f, base_line_height + request.line_spacing);
  const std::uint32_t allowed_lines =
      AllowedLines(request, normal_line_height);

  const auto complete = [&]() -> TextLayout {
    float y{};
    for (std::size_t line_index = 0; line_index < result.lines.size();
         ++line_index) {
      const auto& line = result.lines[line_index];
      float x = request.indent_continuation_lines && line_index != 0u
                    ? request.continuation_indent
                    : 0.0f;
      std::uint32_t previous{};
      for (std::size_t token_index = 0;
           token_index < result.tokens.size(); ++token_index) {
        const auto& token = result.tokens[token_index];
        if (token.begin < line.begin || token.end > line.end ||
            IsControl(token.kind) ||
            token.kind == FormattedTokenKind::Newline) {
          continue;
        }
        const auto measured =
            MeasureToken(token, face, request, previous);
        result.elements.push_back({
            .token_index = token_index,
            .x = x,
            .y = y,
            .width = measured.advance,
            .height = measured.height,
            .glyph = measured.glyph,
        });
        x += measured.advance;
        previous =
            token.kind == FormattedTokenKind::Glyph ? measured.glyph : 0u;
      }
      y += line.height;
    }
    return std::move(result);
  };

  auto finish_line = [&](const std::size_t begin, const std::size_t end,
                         const float width, const float content_height) {
    const float height =
        std::max(normal_line_height, content_height + request.line_spacing);
    if (!result.lines.empty() && request.maximum_height > 0.0f &&
        result.height + height > request.maximum_height) {
      return false;
    }
    const float indent =
        request.indent_continuation_lines && !result.lines.empty()
            ? request.continuation_indent
            : 0.0f;
    result.lines.push_back({.begin = begin,
                            .end = end,
                            .width = width + indent,
                            .height = height});
    result.width = std::max(result.width, width + indent);
    result.height += height;
    return true;
  };

  std::size_t index{};
  std::size_t line_start{};
  std::size_t line_begin_byte{};
  std::size_t last_break = std::numeric_limits<std::size_t>::max();
  float width_at_break{};
  float line_width{};
  float line_height{base_line_height};
  std::uint32_t previous_glyph{};
  std::uint32_t previous_codepoint{};

  while (index < result.tokens.size()) {
    const FormattedToken& token = result.tokens[index];
    if (IsControl(token.kind)) {
      result.fitting_bytes = token.end;
      ++index;
      continue;
    }

    if (token.kind == FormattedTokenKind::Newline) {
      if (!finish_line(line_begin_byte, token.begin, line_width,
                       line_height)) {
        result.truncated = true;
        result.fitting_bytes = token.begin;
        return complete();
      }
      result.fitting_bytes = token.end;
      ++index;
      if (result.lines.size() >= allowed_lines) {
        result.truncated = index < result.tokens.size() ||
                           token.end < source.size();
        return complete();
      }
      line_start = index;
      line_begin_byte = token.end;
      last_break = std::numeric_limits<std::size_t>::max();
      width_at_break = 0.0f;
      line_width = 0.0f;
      line_height = base_line_height;
      previous_glyph = 0u;
      previous_codepoint = 0u;
      continue;
    }

    const MeasuredToken measured =
        MeasureToken(token, face, request, previous_glyph);
    if ((request.wrap == WrapMode::Word ||
         request.wrap == WrapMode::WordWithCharacterFallback) &&
        token.kind == FormattedTokenKind::Glyph &&
        IsLineBreakOpportunity(previous_codepoint, token.codepoint)) {
      last_break = index;
      width_at_break = line_width;
    }

    float width_limit = request.maximum_width;
    if (width_limit > 0.0f && request.indent_continuation_lines &&
        !result.lines.empty()) {
      width_limit -= request.continuation_indent;
    }
    if (width_limit <= 0.0f && request.maximum_width > 0.0f) {
      result.truncated = true;
      result.fitting_bytes = token.begin;
      return complete();
    }

    const bool overflow =
        width_limit > 0.0f && line_width > 0.0f &&
        line_width + measured.advance > width_limit;
    if (overflow) {
      std::size_t next_line = index;
      float completed_width = line_width;
      bool can_wrap = false;

      if (request.wrap == WrapMode::Character ||
          request.wrap == WrapMode::WordWithCharacterFallback) {
        can_wrap = true;
      }
      if ((request.wrap == WrapMode::Word ||
           request.wrap == WrapMode::WordWithCharacterFallback) &&
          last_break != std::numeric_limits<std::size_t>::max() &&
          last_break > line_start) {
        next_line = last_break;
        completed_width = width_at_break;
        can_wrap = true;
      }
      if (!can_wrap) {
        result.truncated = true;
        result.fitting_bytes = token.begin;
        (void)finish_line(line_begin_byte, token.begin, line_width,
                          line_height);
        return complete();
      }

      const std::size_t completed_end =
          next_line < result.tokens.size() ? result.tokens[next_line].begin
                                           : source.size();
      if (!finish_line(line_begin_byte, completed_end, completed_width,
                       line_height)) {
        result.truncated = true;
        result.fitting_bytes = completed_end;
        return complete();
      }
      if (result.lines.size() >= allowed_lines) {
        result.truncated = true;
        result.fitting_bytes = completed_end;
        return complete();
      }

      while (next_line < result.tokens.size() &&
             result.tokens[next_line].kind == FormattedTokenKind::Glyph &&
             openwow::compiler::IsUnicodeWhitespace(
                 result.tokens[next_line].codepoint)) {
        ++next_line;
      }
      index = next_line;
      line_start = next_line;
      line_begin_byte =
          next_line < result.tokens.size() ? result.tokens[next_line].begin
                                           : source.size();
      last_break = std::numeric_limits<std::size_t>::max();
      width_at_break = 0.0f;
      line_width = 0.0f;
      line_height = base_line_height;
      previous_glyph = 0u;
      previous_codepoint = 0u;
      continue;
    }

    line_width += measured.advance;
    line_height = std::max(line_height, measured.height);
    result.fitting_bytes = token.end;
    if (token.kind == FormattedTokenKind::Glyph) {
      previous_glyph = measured.glyph;
      previous_codepoint = token.codepoint;
    } else {
      previous_glyph = 0u;
    }
    ++index;
  }

  if (!finish_line(line_begin_byte, source.size(), line_width, line_height)) {
    result.truncated = true;
    return complete();
  }
  if (!result.tokens.empty() &&
      result.tokens.back().kind == FormattedTokenKind::Newline &&
      result.lines.size() < allowed_lines) {
    if (!finish_line(source.size(), source.size(), 0.0f,
                     base_line_height)) {
      result.truncated = true;
      return complete();
    }
  }
  result.fitting_bytes = source.size();
  return complete();
}

int CountLeadingVisibleElements(const FontFace& face,
                                const std::string_view source,
                                const float maximum_width,
                                const bool formatting_enabled) {
  if (maximum_width <= 0.0f) return 0;
  const auto tokens =
      TokenizeFormattedText(source, formatting_enabled);
  float width{};
  std::uint32_t previous{};
  int count{};
  const TextLayoutRequest request;
  for (const auto& token : tokens) {
    if (token.kind == FormattedTokenKind::Newline) break;
    const auto measured = MeasureToken(token, face, request, previous);
    if (IsControl(token.kind)) continue;
    if (width + measured.advance > maximum_width) break;
    width += measured.advance;
    previous = token.kind == FormattedTokenKind::Glyph ? measured.glyph : 0u;
    ++count;
  }
  return count;
}

int CountTrailingVisibleElements(const FontFace& face,
                                 const std::string_view source,
                                 const float maximum_width,
                                 const bool formatting_enabled) {
  if (maximum_width <= 0.0f) return 0;
  const auto tokens =
      TokenizeFormattedText(source, formatting_enabled);
  float width{};
  int count{};
  const TextLayoutRequest request;
  for (auto token = tokens.rbegin(); token != tokens.rend(); ++token) {
    if (token->kind == FormattedTokenKind::Newline) break;
    if (IsControl(token->kind)) continue;
    const auto measured = MeasureToken(*token, face, request, 0u);
    if (width + measured.advance > maximum_width) break;
    width += measured.advance;
    ++count;
  }
  return count;
}

}

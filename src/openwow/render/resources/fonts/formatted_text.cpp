#include "openwow/render/resources/fonts/formatted_text.h"

#include <array>
#include <charconv>
#include <cstdlib>
#include <utility>

namespace openwow::render::text {
namespace {

std::uint32_t DecodeUtf8(const std::string_view text, std::size_t& offset) {
  const auto first = static_cast<unsigned char>(text[offset]);
  if (first < 0x80u) {
    ++offset;
    return first;
  }

  const auto continuation = [&](const std::size_t index) {
    return index < text.size() &&
           (static_cast<unsigned char>(text[index]) & 0xc0u) == 0x80u;
  };
  if ((first & 0xe0u) == 0xc0u && continuation(offset + 1u)) {
    const auto b1 = static_cast<unsigned char>(text[offset + 1u]);
    offset += 2u;
    return ((first & 0x1fu) << 6u) | (b1 & 0x3fu);
  }
  if ((first & 0xf0u) == 0xe0u && continuation(offset + 1u) &&
      continuation(offset + 2u)) {
    const auto b1 = static_cast<unsigned char>(text[offset + 1u]);
    const auto b2 = static_cast<unsigned char>(text[offset + 2u]);
    offset += 3u;
    return ((first & 0x0fu) << 12u) | ((b1 & 0x3fu) << 6u) |
           (b2 & 0x3fu);
  }
  if ((first & 0xf8u) == 0xf0u && continuation(offset + 1u) &&
      continuation(offset + 2u) && continuation(offset + 3u)) {
    const auto b1 = static_cast<unsigned char>(text[offset + 1u]);
    const auto b2 = static_cast<unsigned char>(text[offset + 2u]);
    const auto b3 = static_cast<unsigned char>(text[offset + 3u]);
    offset += 4u;
    return ((first & 0x07u) << 18u) | ((b1 & 0x3fu) << 12u) |
           ((b2 & 0x3fu) << 6u) | (b3 & 0x3fu);
  }

  ++offset;
  return 0xfffdu;
}

bool ParseColor(const std::string_view source, const std::size_t begin,
                std::uint32_t& color) {
  if (begin + 10u > source.size()) return false;
  const auto field = source.substr(begin + 2u, 8u);
  const auto [end, error] =
      std::from_chars(field.data(), field.data() + field.size(), color, 16);
  return error == std::errc{} && end == field.data() + field.size();
}

std::optional<float> ParseNumber(const std::string_view field) {
  if (field.empty()) return std::nullopt;
  std::string buffer(field);
  char* end{};
  const float value = std::strtof(buffer.c_str(), &end);
  return end == buffer.c_str() + buffer.size() ? std::optional<float>{value}
                                               : std::nullopt;
}

InlineImage ParseInlineImage(const std::string_view payload) {
  InlineImage image;
  std::array<std::string_view, 11> fields{};
  std::size_t field_count{};
  std::size_t begin{};
  while (field_count < fields.size()) {
    const std::size_t separator = payload.find(':', begin);
    fields[field_count++] =
        separator == std::string_view::npos
            ? payload.substr(begin)
            : payload.substr(begin, separator - begin);
    if (separator == std::string_view::npos) break;
    begin = separator + 1u;
  }
  if (field_count == 0u) return image;

  image.path = fields[0];
  auto assign = [&](const std::size_t index, std::optional<float>& value) {
    if (index < field_count) value = ParseNumber(fields[index]);
  };
  assign(1, image.width);
  assign(2, image.height);
  assign(3, image.x_offset);
  assign(4, image.y_offset);
  assign(5, image.texture_width);
  assign(6, image.texture_height);
  assign(7, image.left);
  assign(8, image.top);
  assign(9, image.right);
  assign(10, image.bottom);
  return image;
}

}

std::vector<FormattedToken> TokenizeFormattedText(
    const std::string_view source, const bool formatting_enabled) {
  std::vector<FormattedToken> tokens;
  tokens.reserve(source.size());

  std::size_t offset{};
  while (offset < source.size()) {
    const std::size_t begin = offset;
    if (source[offset] == '\r' || source[offset] == '\n') {
      if (source[offset] == '\r' && offset + 1u < source.size() &&
          source[offset + 1u] == '\n') {
        offset += 2u;
      } else {
        ++offset;
      }
      tokens.push_back(
          {.kind = FormattedTokenKind::Newline, .begin = begin, .end = offset});
      continue;
    }

    if (!formatting_enabled || source[offset] != '|' ||
        offset + 1u >= source.size()) {
      const std::uint32_t codepoint = DecodeUtf8(source, offset);
      tokens.push_back({.kind = FormattedTokenKind::Glyph,
                        .begin = begin,
                        .end = offset,
                        .codepoint = codepoint});
      continue;
    }

    const char command = source[offset + 1u];
    if (command == '|') {
      offset += 2u;
      tokens.push_back({.kind = FormattedTokenKind::Glyph,
                        .begin = begin,
                        .end = offset,
                        .codepoint = '|'});
      continue;
    }
    if (command == 'n' || command == 'N') {
      offset += 2u;
      tokens.push_back(
          {.kind = FormattedTokenKind::Newline, .begin = begin, .end = offset});
      continue;
    }
    if (command == 'r') {
      offset += 2u;
      tokens.push_back({.kind = FormattedTokenKind::ResetColor,
                        .begin = begin,
                        .end = offset});
      continue;
    }
    if (command == 'c') {
      std::uint32_t color{};
      if (ParseColor(source, begin, color)) {
        offset += 10u;
        tokens.push_back({.kind = FormattedTokenKind::Color,
                          .begin = begin,
                          .end = offset,
                          .color_argb = color});
        continue;
      }
    }
    if (command == 'H') {
      const std::size_t end = source.find("|h", offset + 2u);
      if (end != std::string_view::npos && end != offset + 2u) {
        offset = end + 2u;
        tokens.push_back({.kind = FormattedTokenKind::HyperlinkStart,
                          .begin = begin,
                          .end = offset});
        continue;
      }
    }
    if (command == 'h') {
      offset += 2u;
      tokens.push_back({.kind = FormattedTokenKind::HyperlinkEnd,
                        .begin = begin,
                        .end = offset});
      continue;
    }
    if (command == 'T') {
      const std::size_t end = source.find("|t", offset + 2u);
      if (end != std::string_view::npos && end != offset + 2u) {
        InlineImage image =
            ParseInlineImage(source.substr(offset + 2u, end - offset - 2u));
        if (!image.path.empty()) {
          offset = end + 2u;
          tokens.push_back({.kind = FormattedTokenKind::InlineImage,
                            .begin = begin,
                            .end = offset,
                            .image = std::move(image)});
          continue;
        }
      }
    }
    ++offset;
    tokens.push_back({.kind = FormattedTokenKind::Glyph,
                      .begin = begin,
                      .end = offset,
                      .codepoint = '|'});
  }
  return tokens;
}

}

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::render::text {

enum class FormattedTokenKind : std::uint8_t {
  Glyph,
  Color,
  ResetColor,
  Newline,
  HyperlinkStart,
  HyperlinkEnd,
  InlineImage,
};

struct InlineImage {
  std::string path;
  std::optional<float> width;
  std::optional<float> height;
  std::optional<float> x_offset;
  std::optional<float> y_offset;
  std::optional<float> texture_width;
  std::optional<float> texture_height;
  std::optional<float> left;
  std::optional<float> top;
  std::optional<float> right;
  std::optional<float> bottom;
};

struct FormattedToken {
  FormattedTokenKind kind{FormattedTokenKind::Glyph};
  std::size_t begin{};
  std::size_t end{};
  std::uint32_t codepoint{};
  std::uint32_t color_argb{};
  InlineImage image;
};

[[nodiscard]] std::vector<FormattedToken> TokenizeFormattedText(
    std::string_view source, bool formatting_enabled = true);

}

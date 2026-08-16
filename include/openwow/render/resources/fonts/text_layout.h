#pragma once

#include "openwow/render/resources/fonts/font_face.h"
#include "openwow/render/resources/fonts/formatted_text.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace openwow::render::text {

enum class WrapMode : std::uint8_t {
  None,
  Word,
  Character,
  WordWithCharacterFallback,
};

[[nodiscard]] constexpr WrapMode ResolveWrapMode(
    const bool word_wrap, const bool non_space_wrap) noexcept {
  if (word_wrap) {
    return non_space_wrap ? WrapMode::WordWithCharacterFallback
                          : WrapMode::Word;
  }
  return non_space_wrap ? WrapMode::Character : WrapMode::None;
}

struct TextLayoutRequest {
  float maximum_width{};
  float maximum_height{};
  float line_spacing{};
  float line_height{};
  float inline_image_scale{1.0f};
  float continuation_indent{15.0f};
  std::uint32_t maximum_lines{};
  WrapMode wrap{WrapMode::None};
  bool indent_continuation_lines{false};
  bool formatting_enabled{true};
};

struct TextLine {
  std::size_t begin{};
  std::size_t end{};
  float width{};
  float height{};
};

struct TextElement {
  std::size_t token_index{};
  float x{};
  float y{};
  float width{};
  float height{};
  std::uint32_t glyph{};
};

struct TextLayout {
  std::vector<FormattedToken> tokens;
  std::vector<TextLine> lines;
  std::vector<TextElement> elements;
  float width{};
  float height{};
  std::size_t fitting_bytes{};
  bool truncated{false};
};

[[nodiscard]] TextLayout LayoutText(const FontFace& face,
                                    std::string_view source,
                                    const TextLayoutRequest& request);

[[nodiscard]] int CountLeadingVisibleElements(
    const FontFace& face, std::string_view source, float maximum_width,
    bool formatting_enabled = true);
[[nodiscard]] int CountTrailingVisibleElements(
    const FontFace& face, std::string_view source, float maximum_width,
    bool formatting_enabled = true);

}

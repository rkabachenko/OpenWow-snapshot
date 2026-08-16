#pragma once

#include "openwow/render/resources/fonts/formatted_text.h"

#include <algorithm>
#include <cstdint>
#include <string_view>

namespace openwow::render::text {

struct TextAlphaGradient {
  std::uint32_t start{};
  std::uint32_t length{};
};

[[nodiscard]] inline bool IsTextAlphaGradientActive(
    const std::string_view text, const int start, const int length) {
  if (start < 0 || length < 0) {
    return false;
  }

  const auto tokens = TokenizeFormattedText(text);
  const auto rendered_characters = static_cast<std::size_t>(std::count_if(
      tokens.begin(), tokens.end(), [](const FormattedToken& token) {
        return token.kind == FormattedTokenKind::Glyph;
      }));

  return rendered_characters == 0u ||
         static_cast<std::size_t>(start) < rendered_characters;
}

[[nodiscard]] inline std::uint8_t ResolveTextGradientAlpha(
    const TextAlphaGradient& gradient, const std::uint32_t glyph_index,
    const bool trailing_edge) {
  constexpr std::uint32_t kBaseAlpha = 0xffu;
  if (glyph_index < gradient.start) {
    return static_cast<std::uint8_t>(kBaseAlpha);
  }
  if (gradient.length == 0u) {
    return static_cast<std::uint8_t>(kBaseAlpha);
  }

  const std::uint32_t step = kBaseAlpha / gradient.length;
  const std::uint64_t completed =
      2ull * static_cast<std::uint64_t>(glyph_index - gradient.start) +
      (trailing_edge ? 1u : 0u);
  return static_cast<std::uint8_t>(
      kBaseAlpha -
      std::min<std::uint64_t>(kBaseAlpha, completed * step));
}

[[nodiscard]] inline std::uint32_t ScalePremultipliedTextColor(
    const std::uint32_t color, const std::uint8_t alpha) {
  const auto scale = [alpha](const std::uint32_t component) {
    return (component * alpha + 127u) / 255u;
  };
  return (scale((color >> 24u) & 0xffu) << 24u) |
         (scale((color >> 16u) & 0xffu) << 16u) |
         (scale((color >> 8u) & 0xffu) << 8u) |
         scale(color & 0xffu);
}

}

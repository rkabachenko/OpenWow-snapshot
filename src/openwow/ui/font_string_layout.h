#pragma once

namespace openwow::ui {
namespace framexml {
struct UiFrame;
}

constexpr float kFontStringMinimumScriptDimension = 1.0f;

constexpr float kRetailEditBoxCaretWidthUiUnits = 4.0f;

constexpr float kRetailEditBoxCaretColorChannel = 1.0f;

constexpr float kRetailEditBoxHighlightColorChannel = 96.0f / 255.0f;
constexpr float kRetailEditBoxHighlightColorAlpha = 1.0f;

[[nodiscard]] constexpr float ResolveFontStringEffectiveScriptDimension(
    const float layout_dimension, const float rendered_dimension) noexcept {
  const float resolved =
      layout_dimension != 0.0f ? layout_dimension : rendered_dimension;
  return resolved > kFontStringMinimumScriptDimension
             ? resolved
             : kFontStringMinimumScriptDimension;
}

[[nodiscard]] bool FontStringWidthComesFromLayout(
    const framexml::UiFrame& frame) noexcept;
[[nodiscard]] bool FontStringHeightComesFromLayout(
    const framexml::UiFrame& frame) noexcept;
[[nodiscard]] int ResolveFontStringRenderWrapWidth(
    const framexml::UiFrame& frame, bool word_wrap, bool non_space_wrap,
    int rendered_width) noexcept;

}

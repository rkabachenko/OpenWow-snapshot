#pragma once

#include <string_view>

namespace openwow::ui::game::detail {

[[nodiscard]] constexpr bool ShouldSubmitSolidUiRegion(
    const std::string_view normalized_kind,
    const bool has_explicit_solid_color,
    const bool has_gradient) noexcept {
  return normalized_kind == "texture" &&
         (has_explicit_solid_color || has_gradient);
}

}

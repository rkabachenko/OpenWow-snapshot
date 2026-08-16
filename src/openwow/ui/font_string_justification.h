#pragma once

#include <string_view>

namespace openwow::ui {

struct FontStringJustification {
  std::string_view horizontal;
  std::string_view vertical;
};

[[nodiscard]] constexpr FontStringJustification
ResolveFontStringJustification(const std::string_view horizontal,
                               const std::string_view vertical) noexcept {
  return {
      .horizontal = horizontal.empty() ? std::string_view{"CENTER"}
                                       : horizontal,
      .vertical = vertical.empty() ? std::string_view{"MIDDLE"}
                                   : vertical,
  };
}

}

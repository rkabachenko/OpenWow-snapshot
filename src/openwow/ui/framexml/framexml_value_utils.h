#pragma once

#include "openwow/foundation/text/ascii.h"

#include <charconv>
#include <optional>
#include <string>
#include <string_view>

namespace openwow::ui::framexml {

[[nodiscard]] inline std::optional<int> ParseIntegerAttributeValue(
    const std::string_view raw_value) {
  const std::string value = openwow::text::Trim(std::string(raw_value));
  if (value.empty()) {
    return std::nullopt;
  }

  int parsed = 0;
  const auto [parse_end, error] =
      std::from_chars(value.data(), value.data() + value.size(), parsed);
  (void)parse_end;
  if (error != std::errc()) {
    return std::nullopt;
  }
  return parsed;
}

}

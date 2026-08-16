#pragma once

#include <cstdint>

namespace openwow::game {

[[nodiscard]] constexpr char16_t RetailNameToLower(
    const char16_t value) noexcept {
  const auto code_unit = static_cast<std::uint16_t>(value);
  if ((code_unit >= 0x0041u && code_unit <= 0x005Au) ||
      (code_unit >= 0x00C0u && code_unit <= 0x00DEu) ||
      (code_unit >= 0x0410u && code_unit <= 0x042Fu)) {
    return static_cast<char16_t>(code_unit + 0x20u);
  }
  if (code_unit == 0x0152u) {
    return static_cast<char16_t>(0x0153u);
  }
  if (code_unit == 0x0401u) {
    return static_cast<char16_t>(0x0451u);
  }
  return value;
}

[[nodiscard]] constexpr char16_t RetailNameToUpper(
    const char16_t value) noexcept {
  const auto code_unit = static_cast<std::uint16_t>(value);
  if ((code_unit >= 0x0061u && code_unit <= 0x007Au) ||
      (code_unit >= 0x00E0u && code_unit <= 0x00FEu) ||
      (code_unit >= 0x0430u && code_unit <= 0x044Fu)) {
    return static_cast<char16_t>(code_unit - 0x20u);
  }
  if (code_unit == 0x0153u) {
    return static_cast<char16_t>(0x0152u);
  }
  if (code_unit == 0x0451u) {
    return static_cast<char16_t>(0x0401u);
  }
  return value;
}

}

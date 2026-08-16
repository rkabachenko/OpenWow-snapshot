#pragma once

#include <cstdint>
#include <cwctype>

namespace openwow::compiler {

inline bool IsUnicodeWhitespace(const std::uint32_t codepoint) noexcept {
  if (codepoint > 0xFFFFu) {
    return false;
  }
  return std::iswspace(static_cast<std::wint_t>(codepoint)) != 0;
}

}

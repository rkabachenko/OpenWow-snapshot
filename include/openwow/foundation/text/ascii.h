#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace openwow::text {

[[nodiscard]] constexpr unsigned char ToLowerAsciiChar(
    const unsigned char character) noexcept {
  return character >= 'A' && character <= 'Z'
             ? static_cast<unsigned char>(character + ('a' - 'A'))
             : character;
}

[[nodiscard]] constexpr unsigned char ToUpperAsciiChar(
    const unsigned char character) noexcept {
  return character >= 'a' && character <= 'z'
             ? static_cast<unsigned char>(character - ('a' - 'A'))
             : character;
}

[[nodiscard]] inline std::string ToLowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](const unsigned char character) {
                   return static_cast<char>(ToLowerAsciiChar(character));
                 });
  return value;
}

[[nodiscard]] inline std::string ToUpperAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](const unsigned char character) {
                   return static_cast<char>(ToUpperAsciiChar(character));
                 });
  return value;
}

[[nodiscard]] inline std::string Trim(std::string value) {
  const auto start = value.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) {
    return {};
  }
  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(start, end - start + 1);
}

[[nodiscard]] inline std::string_view StripUtf8Bom(std::string_view value) {
  if (value.size() >= 3 &&
      static_cast<unsigned char>(value[0]) == 0xEF &&
      static_cast<unsigned char>(value[1]) == 0xBB &&
      static_cast<unsigned char>(value[2]) == 0xBF) {
    value.remove_prefix(3);
  }
  return value;
}

[[nodiscard]] inline bool EqualsIgnoreCaseAscii(const std::string_view lhs,
                                                const std::string_view rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t index = 0; index < lhs.size(); ++index) {
    const auto lhs_character = static_cast<unsigned char>(lhs[index]);
    const auto rhs_character = static_cast<unsigned char>(rhs[index]);
    if (ToLowerAsciiChar(lhs_character) != ToLowerAsciiChar(rhs_character)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] inline bool EqualsIgnoreCaseAscii(const char* lhs,
                                                const char* rhs) {
  if (lhs == nullptr || rhs == nullptr) {
    return false;
  }
  return EqualsIgnoreCaseAscii(std::string_view(lhs), std::string_view(rhs));
}

}

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace openwow::game::actions::macros::rules {

struct RetailMacroToken {
  std::string_view value;
  std::size_t raw_end = 0;
};

inline RetailMacroToken SplitRetailMacroToken(std::string_view text,
                                              std::size_t start,
                                              std::size_t limit,
                                              char delimiter) {
  if (limit > text.size()) {
    limit = text.size();
  }
  if (start > limit) {
    start = limit;
  }

  while (start < limit && text[start] == ' ') {
    ++start;
  }

  std::size_t raw_end = delimiter == '\0' ? limit : start;
  if (delimiter != '\0') {
    while (raw_end < limit && text[raw_end] != delimiter) {
      ++raw_end;
    }
  }

  std::size_t end = raw_end;
  while (end > start && text[end - 1] == ' ') {
    --end;
  }

  return {text.substr(start, end - start), raw_end};
}

inline std::string_view TrimRetailMacroSpaces(std::string_view text) {
  return SplitRetailMacroToken(text, 0, text.size(), '\0').value;
}

inline std::string CopyRetailMacroSpan(std::string_view text,
                                      std::size_t destination_size) {
  if (destination_size == 0) {
    return {};
  }

  const std::size_t copy_size =
      std::min(text.size(), destination_size - 1);
  return std::string(text.substr(0, copy_size));
}

inline std::uint32_t ParseRetailMacroUnsignedPrefix(std::string_view text) {
  std::uint32_t value = 0;
  for (const char ch : text) {
    const auto digit =
        static_cast<std::uint32_t>(ch - static_cast<char>('0'));
    if (digit >= 10u) {
      break;
    }
    value = digit + 10u * value;
  }
  return value;
}

}

#pragma once

#include <cstddef>

#include "openwow/core/storm_string.h"

namespace openwow::core {

template <typename... Args>
std::size_t FormatLocalized(char* out, std::size_t size, const char* format,
                            Args... args) {
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#pragma GCC diagnostic ignored "-Wformat-security"
#endif
  return openwow::core::SStrPrintf(out, size, format, args...);
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
}

}

#pragma once

#include "openwow/core/storm_error.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace openwow::core {

inline std::uint32_t ParseUnsignedDecimalLikeSub76F140(std::string_view text) {
  if (text.empty()) {
    return 0;
  }

  std::uint32_t result =
      static_cast<std::uint32_t>(text.front() - static_cast<char>('0'));
  if (result >= 10u) {
    return 0;
  }

  for (std::size_t i = 1; i < text.size(); ++i) {
    const auto digit =
        static_cast<std::uint32_t>(text[i] - static_cast<char>('0'));
    if (digit >= 10u) {
      break;
    }
    result = digit + 10u * result;
  }

  return result;
}

namespace detail {

constexpr std::size_t kPrecomputedFractionDigitCount = 20;

constexpr double PowDoubleIntExponentLikeSub76F930(double base, std::int32_t exponent) {
  std::uint32_t magnitude = static_cast<std::uint32_t>(exponent);
  if (exponent < 0) {
    magnitude = 0u - magnitude;
  }

  double factor = 1.0;
  while (magnitude != 0u) {
    if ((magnitude & 1u) != 0u) {
      factor *= base;
    }
    magnitude >>= 1u;
    if (magnitude != 0u) {
      base *= base;
    }
  }

  if (exponent < 0) {
    return 1.0 / factor;
  }
  return factor;
}

constexpr std::array<double, kPrecomputedFractionDigitCount * 10u>
BuildFloatFractionLookupLikeSub76F970() {
  std::array<double, kPrecomputedFractionDigitCount * 10u> table{};
  std::size_t index = 0;

  for (std::int32_t exponent = -1; exponent >= -20; --exponent) {
    for (std::uint32_t digit = 0; digit < 10u; ++digit) {
      table[index++] = PowDoubleIntExponentLikeSub76F930(10.0, exponent) *
                       static_cast<double>(digit);
    }
  }

  return table;
}

inline constexpr auto kFloatFractionLookup = BuildFloatFractionLookupLikeSub76F970();

inline std::uint32_t ParseSignedDecimalLikeSub76F0D0NonNull(std::string_view text) {
  if (text.empty()) {
    return 0;
  }

  bool negative = text.front() == '-';
  if (negative) {
    text.remove_prefix(1);
  }
  if (text.empty()) {
    return 0;
  }

  std::uint32_t result =
      static_cast<std::uint32_t>(text.front() - static_cast<char>('0'));
  if (result >= 10u) {
    return 0;
  }

  for (std::size_t i = 1; i < text.size(); ++i) {
    const auto digit =
        static_cast<std::uint32_t>(text[i] - static_cast<char>('0'));
    if (digit >= 10u) {
      break;
    }
    result = digit + 10u * result;
  }

  if (negative) {
    return 0u - result;
  }
  return result;
}

inline std::uint32_t ParseDecimalCursorLikeSub76F190NonNull(const char*& text) {
  std::uint32_t result = 0;
  if (*text >= '0') {
    while (*text >= '0') {
      if (*text > '9') {
        break;
      }

      result = static_cast<std::uint32_t>(*text - '0') + 10u * result;
      ++text;
    }
  }

  return result;
}

inline double ParseFloatLikeSub76FB80NonNull(std::string_view text) {
  if (text.empty()) {
    return 0.0;
  }

  bool negative = false;
  if (text.front() == '-') {
    negative = true;
    text.remove_prefix(1);
  }

  const char* cursor = text.data();
  const char* const end = cursor + text.size();
  const char* chunk_begin = cursor;

  std::uint32_t chunk_value = 0;
  double integer_accumulator = 0.0;

  if (cursor != end) {
    const auto first_digit =
        static_cast<std::uint32_t>(*cursor - static_cast<char>('0'));
    if (first_digit < 10u) {
      chunk_value = first_digit;
      ++cursor;

      while (cursor != end) {
        const auto digit =
            static_cast<std::uint32_t>(*cursor - static_cast<char>('0'));
        if (digit >= 10u) {
          break;
        }

        chunk_value = digit + 10u * chunk_value;
        ++cursor;

        if (chunk_value >= 0x19999999u) {
          const auto chunk_digits = static_cast<std::int32_t>(cursor - chunk_begin);
          integer_accumulator =
              integer_accumulator *
                  PowDoubleIntExponentLikeSub76F930(10.0, chunk_digits) +
              static_cast<double>(chunk_value);
          chunk_value = 0;
          chunk_begin = cursor;
        }
      }
    }
  }

  double result = static_cast<double>(chunk_value);
  if (integer_accumulator != 0.0) {
    const auto trailing_digits = static_cast<std::int32_t>(cursor - chunk_begin);
    result = PowDoubleIntExponentLikeSub76F930(10.0, trailing_digits) *
                 integer_accumulator +
             static_cast<double>(chunk_value);
  }

  if (cursor != end && *cursor == '.') {
    ++cursor;
    std::size_t fractional_digits = 0;
    std::size_t lookup_index = 0;
    std::int32_t exponent = -1;

    while (cursor != end) {
      const auto digit =
          static_cast<std::uint32_t>(*cursor - static_cast<char>('0'));
      if (digit >= 10u) {
        break;
      }

      if (fractional_digits < kPrecomputedFractionDigitCount) {
        result += kFloatFractionLookup[lookup_index + digit];
      } else {
        result += PowDoubleIntExponentLikeSub76F930(10.0, exponent) *
                  static_cast<double>(digit);
      }

      ++cursor;
      ++fractional_digits;
      ++lookup_index;
      lookup_index += 9;
      --exponent;
    }
  }

  if (cursor != end && (*cursor == 'e' || *cursor == 'E')) {
    ++cursor;
    if (cursor != end && *cursor == '+') {
      ++cursor;
    }

    const auto exponent_text =
        std::string_view(cursor, static_cast<std::size_t>(end - cursor));
    const auto signed_exponent = static_cast<std::int32_t>(
        ParseSignedDecimalLikeSub76F0D0NonNull(exponent_text));
    result *= PowDoubleIntExponentLikeSub76F930(10.0, signed_exponent);
  }

  if (negative) {
    return -result;
  }
  return result;
}

}

inline std::uint32_t ParseSignedDecimalLikeSub76F0D0(const char* text) {
  if (text == nullptr) {
    SErrSetLastError(87);
    return 0;
  }

  return detail::ParseSignedDecimalLikeSub76F0D0NonNull(text);
}

inline std::uint32_t ParseSignedDecimalLikeSub76F0D0(std::string_view text) {
  return detail::ParseSignedDecimalLikeSub76F0D0NonNull(text);
}

inline std::uint32_t ParseDecimalCursorLikeSub76F190(const char** text) {
  if (text == nullptr) {
    SErrSetLastError(87);
    return 0;
  }

  const char* cursor = *text;
  const auto result = detail::ParseDecimalCursorLikeSub76F190NonNull(cursor);
  *text = cursor;
  return result;
}

inline std::uint32_t ParseDecimalCursorLikeSub76F190(std::string_view& text) {
  const char* cursor = text.data();
  const char* const begin = cursor;
  const char* const end = cursor + text.size();
  std::uint32_t result = 0;

  if (cursor != end && *cursor >= '0') {
    while (cursor != end && *cursor >= '0') {
      if (*cursor > '9') {
        break;
      }

      result = static_cast<std::uint32_t>(*cursor - '0') + 10u * result;
      ++cursor;
    }
  }

  text.remove_prefix(static_cast<std::size_t>(cursor - begin));
  return result;
}

inline double ParseFloatLikeSub76FB80(const char* text) {
  if (text == nullptr) {
    SErrSetLastError(87);
    return 0.0;
  }

  return detail::ParseFloatLikeSub76FB80NonNull(text);
}

inline double ParseFloatLikeSub76FB80(std::string_view text) {
  return detail::ParseFloatLikeSub76FB80NonNull(text);
}

}

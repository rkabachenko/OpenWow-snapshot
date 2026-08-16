
#pragma once

#include "openwow/core/storm_string.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace openwow::game {

struct BNetUtf8CopyResult {
  std::int32_t logical_length = 0;
  std::int32_t byte_length = 0;
};

inline std::size_t CopyBoundedCStringPrefix(char *dest, const std::size_t dest_size,
                                            const std::string_view source,
                                            const std::size_t max_chars) {
  if (!dest || dest_size == 0) {
    return 0;
  }

  const auto copy_limit = std::min<std::size_t>(max_chars, dest_size - 1);
  std::size_t copied = 0;
  while (copied < copy_limit && copied < source.size()) {
    const char ch = source[copied];
    if (ch == '\0') {
      break;
    }

    dest[copied] = ch;
    ++copied;
  }

  dest[copied] = '\0';
  return copied;
}

inline BNetUtf8CopyResult CopyBoundedLegacyUtf8Text(char *dest, const std::size_t dest_size,
                                                    const std::string_view source,
                                                    const std::size_t max_codepoints) {
  BNetUtf8CopyResult result{};
  if (!dest || dest_size == 0) {
    return result;
  }

  std::fill(dest, dest + dest_size, '\0');
  if (source.empty()) {
    return result;
  }

  const std::string owned(source);
  const char *const start = owned.c_str();
  const char *cursor = start;
  while (*cursor != '\0' && result.logical_length < static_cast<std::int32_t>(max_codepoints)) {
    std::uint32_t raw = 0;
    std::uint32_t upper = 0;
    const char *const before = cursor;
    const int consumed = core::SStrGetNextUTF8Char_ToUpper(&raw, &cursor, &upper);
    if (consumed <= 0 || cursor <= before) {
      break;
    }
    ++result.logical_length;
  }

  const auto bytes_to_copy =
      std::min<std::size_t>(static_cast<std::size_t>(cursor - start), dest_size - 1);
  if (bytes_to_copy != 0) {
    std::memcpy(dest, start, bytes_to_copy);
  }
  dest[bytes_to_copy] = '\0';
  result.byte_length = static_cast<std::int32_t>(bytes_to_copy);
  return result;
}

inline BNetUtf8CopyResult CopyStormUtf8Prefix(char *dest, const std::size_t dest_size,
                                              const std::string_view source,
                                              const std::size_t max_codepoints) {
  BNetUtf8CopyResult result{};
  if (!dest || dest_size == 0) {
    return result;
  }

  const std::string owned(source);
  result.byte_length =
      static_cast<std::int32_t>(core::SStrCopyUTF8(dest, owned.c_str(), dest_size, max_codepoints));

  for (std::int32_t index = 0; index < result.byte_length; ++index) {
    if ((static_cast<unsigned char>(owned[static_cast<std::size_t>(index)]) & 0xC0u) != 0x80u) {
      ++result.logical_length;
    }
  }

  return result;
}

inline std::size_t CopySanitizedBNetChatText(char *dest, const std::size_t dest_size,
                                             const std::string_view source) {
  if (!dest || dest_size == 0) {
    return 0;
  }

  std::size_t source_offset = 0;
  std::size_t dest_offset = 0;
  while (source_offset < source.size() && dest_offset + 1 < dest_size) {
    if (source[source_offset] == '|' && source_offset + 1 < source.size()) {
      const char escape = source[source_offset + 1];
      if (escape == 'c') {
        source_offset = std::min(source.size(), source_offset + 10);
        continue;
      }
      if (escape == 'T' || escape == 'r' || escape == 't') {
        source_offset += 2;
        continue;
      }
    }

    dest[dest_offset++] = source[source_offset++];
  }

  dest[dest_offset] = '\0';
  return dest_offset;
}

inline std::string CopySanitizedBNetChatText(const std::string_view source,
                                             const std::size_t output_capacity) {
  if (output_capacity == 0) {
    return {};
  }

  const auto buffer_size = std::min(output_capacity, source.size() + 1);
  std::string sanitized(buffer_size, '\0');
  const auto copied = CopySanitizedBNetChatText(sanitized.data(), buffer_size, source);
  sanitized.resize(copied);
  return sanitized;
}

}

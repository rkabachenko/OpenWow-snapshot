#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace openwow::text {

inline void PopBackUtf8(std::string* text) {
  if (text == nullptr || text->empty()) {
    return;
  }
  auto pos = text->size();
  while (pos > 0) {
    --pos;

    const auto byte = static_cast<std::uint8_t>((*text)[pos]);
    if ((byte & 0xC0u) != 0x80u) {
      break;
    }
  }
  text->erase(pos);
}

inline int Utf8CodepointCount(std::string_view text) {
  int count = 0;
  for (std::size_t i = 0; i < text.size();) {
    const auto byte = static_cast<std::uint8_t>(text[i]);
    if (byte < 0x80u) {
      i += 1;
    } else if ((byte & 0xE0u) == 0xC0u) {
      i += 2;
    } else if ((byte & 0xF0u) == 0xE0u) {
      i += 3;
    } else {
      i += 4;
    }
    ++count;
  }
  return count;
}

inline int ClampUtf8ByteIndex(std::string_view text, int byte_index) {
  const int i = std::clamp(byte_index, 0, static_cast<int>(text.size()));
  if (i <= 0) return 0;
  if (i >= static_cast<int>(text.size())) return static_cast<int>(text.size());

  int pos = i;
  while (pos > 0 && (static_cast<std::uint8_t>(text[static_cast<std::size_t>(pos)]) & 0xC0u) == 0x80u) {
    --pos;
  }
  return pos;
}

inline int Utf8PrevByteIndex(std::string_view text, int byte_index) {
  int i = ClampUtf8ByteIndex(text, byte_index);
  if (i <= 0) return 0;
  --i;
  while (i > 0 && (static_cast<std::uint8_t>(text[static_cast<std::size_t>(i)]) & 0xC0u) == 0x80u) {
    --i;
  }
  return i;
}

inline int Utf8NextByteIndex(std::string_view text, int byte_index) {
  const int max_pos = static_cast<int>(text.size());
  int i = ClampUtf8ByteIndex(text, byte_index);
  if (i >= max_pos) return max_pos;
  const auto lead = static_cast<std::uint8_t>(text[static_cast<std::size_t>(i)]);
  int step = 1;
  if ((lead & 0xE0u) == 0xC0u) step = 2;
  else if ((lead & 0xF0u) == 0xE0u) step = 3;
  else if ((lead & 0xF8u) == 0xF0u) step = 4;
  return std::min(i + step, max_pos);
}

inline std::string Utf8TakeCodepoints(std::string_view text, int max_codepoints) {
  if (max_codepoints <= 0 || text.empty()) {
    return {};
  }
  int count = 0;
  std::size_t pos = 0;
  while (pos < text.size() && count < max_codepoints) {
    const auto byte = static_cast<std::uint8_t>(text[pos]);
    if (byte < 0x80u) {
      pos += 1;
    } else if ((byte & 0xE0u) == 0xC0u) {
      pos += 2;
    } else if ((byte & 0xF0u) == 0xE0u) {
      pos += 3;
    } else {
      pos += 4;
    }
    ++count;
  }
  pos = std::min(pos, text.size());
  return std::string(text.substr(0, pos));
}

inline void AppendUtf8Clamped(std::string* base, std::string_view addition, int max_codepoints) {
  if (base == nullptr || addition.empty()) {
    return;
  }
  if (max_codepoints <= 0) {
    return;
  }
  const int used = Utf8CodepointCount(*base);
  const int remaining = max_codepoints - used;
  if (remaining <= 0) {
    return;
  }
  const auto trimmed = Utf8TakeCodepoints(addition, remaining);
  const auto bytes_to_add = trimmed.size();
  if (bytes_to_add > 0) {
    base->append(trimmed);
  }
}

}

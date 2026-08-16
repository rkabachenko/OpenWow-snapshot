#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace openwow::ui::game {

inline std::string NormalizePackedChoiceState(std::string_view encoded) {
  if (encoded.empty()) {
    return {};
  }

  const auto version = static_cast<unsigned char>(encoded.front());
  if (version != 1u) {
    return std::string(encoded);
  }

  std::string normalized;
  normalized.reserve(encoded.size() + 1);
  normalized.push_back(static_cast<char>(2));

  std::size_t dest_index = 1;
  unsigned int bit_offset = 0;
  unsigned int consumed_bits = 0;
  for (std::size_t source_index = 1;
       source_index < encoded.size() && encoded[source_index] != '\0' && dest_index < 0xFFu;) {
    if (bit_offset == 0) {
      normalized.push_back('\0');
    }

    const auto source_byte = static_cast<unsigned char>(encoded[source_index]) & 0x7Fu;

    const int shift_amount =
        static_cast<int>(bit_offset) - static_cast<int>(consumed_bits);
    const unsigned int shifted =
        (shift_amount >= 0 && shift_amount < 8)
            ? (static_cast<unsigned int>(source_byte) << shift_amount)
            : 0u;
    const auto merged = static_cast<unsigned char>(
        (static_cast<unsigned char>(normalized[dest_index]) | shifted) &
        0x3Fu);
    normalized[dest_index] = static_cast<char>(merged | 0x40u);

    if (consumed_bits <= bit_offset + 1) {
      if (consumed_bits >= bit_offset + 1) {
        ++source_index;
        consumed_bits = 0;
      } else {
        consumed_bits += 6u - bit_offset;
      }

      ++dest_index;
      bit_offset = 0;
    } else {
      ++source_index;
      bit_offset = bit_offset - consumed_bits + 7u;
      consumed_bits = 0;
    }
  }

  if (bit_offset == 0) {
    while (normalized.size() <= dest_index)
      normalized.push_back('\0');
    normalized[dest_index] = '\0';
  } else {
    const auto null_pos = dest_index + 1;
    while (normalized.size() <= null_pos)
      normalized.push_back('\0');
    normalized[null_pos] = '\0';
  }

  normalized.resize(std::strlen(normalized.c_str()));
  return normalized;
}

inline bool PackedChoiceStateContains(std::string_view encoded, const std::uint32_t key) {
  const auto nul_offset = encoded.find('\0');
  const auto cstring_length =
      nul_offset == std::string_view::npos ? encoded.size() : nul_offset;
  if (cstring_length == 0u) {
    return false;
  }

  const auto max_key = 2u * (3u * static_cast<std::uint32_t>(cstring_length) - 3u);
  if (key >= max_key) {
    return false;
  }

  const auto bucket_index = static_cast<std::size_t>(key / 6u + 1u);
  if (bucket_index >= cstring_length) {
    return false;
  }

  return (static_cast<unsigned char>(encoded[bucket_index]) &
          (1u << (key % 6u))) != 0;
}

inline std::string SetPackedChoiceStateBit(std::string_view current,
                                           std::uint32_t key,
                                           bool enabled) {
  std::string encoded(current);
  const auto current_length = encoded.size();
  const auto bucket_index = static_cast<std::size_t>(key / 6u + 1u);

  if (bucket_index + 1u >= 0x100u) {
    return encoded;
  }

  if (encoded.empty()) {
    encoded.push_back('\0');
  }
  encoded[0] = static_cast<char>(2);

  if (enabled) {
    if (encoded.size() <= bucket_index) {
      encoded.resize(bucket_index + 1u, static_cast<char>(0x40));
    }

    encoded[bucket_index] = static_cast<char>(
        static_cast<unsigned char>(encoded[bucket_index]) | (1u << (key % 6u)));
  } else {
    if (bucket_index >= current_length) {
      return encoded;
    }

    encoded[bucket_index] = static_cast<char>(
        static_cast<unsigned char>(encoded[bucket_index]) & ~(1u << (key % 6u)));

    while (encoded.size() > 1u && encoded.back() == static_cast<char>(0x40)) {
      encoded.pop_back();
    }
  }

  return encoded;
}

}

#pragma once

#include <array>
#include <cstdint>

namespace openwow::core::detail {

constexpr std::uint8_t LegacyUtf8SequenceLength(const std::uint8_t lead_byte) {
  if (lead_byte < 0x80) return 1;
  if (lead_byte < 0xC0) return 0;
  if (lead_byte < 0xE0) return 2;
  if (lead_byte < 0xF0) return 3;
  if (lead_byte < 0xF8) return 4;
  if (lead_byte < 0xFC) return 5;
  if (lead_byte < 0xFE) return 6;
  return 0;
}

inline constexpr std::array<std::uint32_t, 7> kLegacyUtf8Offsets{
    0x00000000, 0x00000000, 0x00003080, 0x000E2080,
    0x03C82080, 0xFA082080, 0x82082080,
};

}

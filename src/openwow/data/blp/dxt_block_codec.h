#pragma once

#include <array>
#include <cstdint>

namespace openwow::data::blp::detail {

inline std::uint64_t ReadLittleEndian48(const std::uint8_t* bytes) {
  std::uint64_t value = 0;
  for (int i = 0; i < 6; ++i) {
    value |= static_cast<std::uint64_t>(bytes[i]) << (8 * i);
  }
  return value;
}

inline std::array<std::uint8_t, 8> BuildDxt5AlphaTable(std::uint8_t alpha0,
                                                       std::uint8_t alpha1) {
  std::array<std::uint8_t, 8> table{};
  table[0] = alpha0;
  table[1] = alpha1;

  if (alpha0 > alpha1) {
    table[2] = static_cast<std::uint8_t>((6 * alpha0 + alpha1 + 3) / 7);
    table[3] = static_cast<std::uint8_t>((5 * alpha0 + 2 * alpha1 + 3) / 7);
    table[4] = static_cast<std::uint8_t>((4 * alpha0 + 3 * alpha1 + 3) / 7);
    table[5] = static_cast<std::uint8_t>((3 * alpha0 + 4 * alpha1 + 3) / 7);
    table[6] = static_cast<std::uint8_t>((2 * alpha0 + 5 * alpha1 + 3) / 7);
    table[7] = static_cast<std::uint8_t>((alpha0 + 6 * alpha1 + 3) / 7);
  } else {
    table[2] = static_cast<std::uint8_t>((4 * alpha0 + alpha1 + 2) / 5);
    table[3] = static_cast<std::uint8_t>((3 * alpha0 + 2 * alpha1 + 2) / 5);
    table[4] = static_cast<std::uint8_t>((2 * alpha0 + 3 * alpha1 + 2) / 5);
    table[5] = static_cast<std::uint8_t>((alpha0 + 4 * alpha1 + 2) / 5);
    table[6] = 0;
    table[7] = 255;
  }

  return table;
}

}

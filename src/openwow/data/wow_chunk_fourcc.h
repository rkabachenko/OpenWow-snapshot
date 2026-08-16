#pragma once

#include <cstdint>

namespace openwow::data {

[[nodiscard]] constexpr std::uint32_t WowChunkFourCC(
    const char (&tag)[5]) noexcept {
  return static_cast<std::uint32_t>(static_cast<std::uint8_t>(tag[3])) |
         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(tag[2])) << 8u) |
         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(tag[1])) << 16u) |
         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(tag[0])) << 24u);
}

}

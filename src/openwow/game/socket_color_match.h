
#pragma once

#include <cstdint>

namespace openwow::game {

inline constexpr std::uint32_t kGemColorMaskMeta = 0x1u;
inline constexpr std::uint32_t kGemColorMaskRed = 0x2u;
inline constexpr std::uint32_t kGemColorMaskYellow = 0x4u;
inline constexpr std::uint32_t kGemColorMaskBlue = 0x8u;

[[nodiscard]] constexpr bool SocketMaskMatchesGemColorMask(
    const std::uint32_t socket_color_mask,
    const std::uint32_t gem_color_mask) {
  return socket_color_mask == 0 || (socket_color_mask & gem_color_mask) != 0;
}

}

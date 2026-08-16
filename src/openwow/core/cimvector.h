
#pragma once

#include <cstdint>
#include <cstring>

namespace openwow::core {

[[nodiscard]] inline constexpr std::uint8_t QuantizeColorComponent(float value) noexcept {
  return static_cast<std::uint8_t>(static_cast<int>(value * 255.0f + 0.5f));
}

[[nodiscard]] inline constexpr std::uint32_t PackArgbFloatsToBgra(float alpha, float red,
                                                                   float green,
                                                                   float blue) noexcept {
  const auto a = static_cast<std::uint32_t>(QuantizeColorComponent(alpha));
  const auto r = static_cast<std::uint32_t>(QuantizeColorComponent(red));
  const auto g = static_cast<std::uint32_t>(QuantizeColorComponent(green));
  const auto b = static_cast<std::uint32_t>(QuantizeColorComponent(blue));
  return (a << 24) | (r << 16) | (g << 8) | b;
}

inline constexpr void PackArgbFloatsToBgra(std::uint32_t &dest, float alpha, float red,
                                           float green, float blue) noexcept {
  dest = PackArgbFloatsToBgra(alpha, red, green, blue);
}

inline void CImVector_BlendRGB(std::uint32_t& color,
                               unsigned int factor,
                               std::uint32_t target) noexcept {
  std::uint8_t c[4], t[4];
  std::memcpy(c, &color, 4);
  std::memcpy(t, &target, 4);

  for (int i = 0; i < 3; ++i) {
    const int delta = static_cast<int>(t[i]) - static_cast<int>(c[i]);
    const auto product =
        static_cast<std::uint32_t>(delta * static_cast<int>(factor));
    c[i] = static_cast<std::uint8_t>(
        c[i] + static_cast<std::uint8_t>(product >> 8));
  }

  std::memcpy(&color, c, 4);
}

}

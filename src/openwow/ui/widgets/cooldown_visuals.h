#pragma once

#include <cstdint>

namespace openwow::ui::widgets {

struct CooldownVisualColors {
  std::uint32_t fill_abgr{0};
  std::uint32_t ready_flash_abgr{0};
  std::uint32_t edge_abgr{0};
};

constexpr std::uint8_t MultiplyCooldownAlphaBytes(const std::uint8_t lhs,
                                                  const std::uint8_t rhs) noexcept {
  return static_cast<std::uint8_t>(
      (static_cast<std::uint32_t>(lhs) * static_cast<std::uint32_t>(rhs)) /
      255u);
}

constexpr std::uint32_t PackAbgr(const std::uint8_t red, const std::uint8_t green,
                                 const std::uint8_t blue,
                                 const std::uint8_t alpha) noexcept {
  return static_cast<std::uint32_t>(red) |
         (static_cast<std::uint32_t>(green) << 8) |
         (static_cast<std::uint32_t>(blue) << 16) |
         (static_cast<std::uint32_t>(alpha) << 24);
}

constexpr CooldownVisualColors ComputeCooldownVisualColors(
    const bool ready_flash_active, const std::uint8_t current_alpha,
    const std::uint8_t inherited_alpha) noexcept {
  const auto combined_alpha =
      MultiplyCooldownAlphaBytes(current_alpha, inherited_alpha);

  if (ready_flash_active) {
    return {
        .fill_abgr = 0u,
        .ready_flash_abgr = PackAbgr(0xFFu, 0xA0u, 0x50u, combined_alpha),
        .edge_abgr = 0u,
    };
  }

  return {
      .fill_abgr = PackAbgr(
          0u, 0u, 0u, MultiplyCooldownAlphaBytes(combined_alpha, 160u)),
      .ready_flash_abgr = 0u,
      .edge_abgr = PackAbgr(0xFFu, 0xFFu, 0xFFu, combined_alpha),
  };
}

}

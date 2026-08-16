#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace openwow::game {

inline constexpr std::array<std::uint32_t, 3> kAuctionRuntimeMinutes{
    720u, 1440u, 2880u};

[[nodiscard]] constexpr bool IsRetailAuctionRuntimeMinutes(
    const std::uint32_t runtime_minutes) noexcept {
  return runtime_minutes == kAuctionRuntimeMinutes[0] ||
         runtime_minutes == kAuctionRuntimeMinutes[1] ||
         runtime_minutes == kAuctionRuntimeMinutes[2];
}

[[nodiscard]] inline std::uint32_t CalculateRetailAuctionDeposit(
    const std::uint32_t deposit_rate,
    const std::uint32_t total_vendor_sell_price,
    const std::uint32_t runtime_minutes) noexcept {
  if (!IsRetailAuctionRuntimeMinutes(runtime_minutes)) {
    return 100u;
  }

  const std::uint32_t wrapped_product =
      total_vendor_sell_price * deposit_rate;
  const std::uint32_t base_deposit = wrapped_product / 100u;

  const float base_as_float =
      static_cast<float>(base_deposit >> 16u) * 65536.0f +
      static_cast<float>(base_deposit & 0xFFFFu);
  const float runtime_as_float =
      static_cast<float>(runtime_minutes >> 16u) * 65536.0f +
      static_cast<float>(runtime_minutes & 0xFFFFu);
  const float scaled = base_as_float * (runtime_as_float / 240.0f);

  std::uint32_t deposit = 0;
  if (scaled >= 4294967296.0f || !std::isfinite(scaled)) {
    deposit = std::numeric_limits<std::uint32_t>::max();
  } else if (scaled > 0.0f) {
    deposit = static_cast<std::uint32_t>(scaled);
  }
  return std::max(deposit, 100u);
}

}

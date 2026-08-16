#pragma once

#include <cstdint>

namespace openwow::core {

inline constexpr std::uint32_t kClientCrtRandMultiplier = 214013u;
inline constexpr std::uint32_t kClientCrtRandIncrement = 2531011u;
inline constexpr std::uint32_t kClientCrtRandMask = 0x7fffu;

class ClientCrtRandom final {
 public:
  explicit constexpr ClientCrtRandom(
      const std::uint32_t seed = 1u) noexcept
      : state_(seed) {}

  constexpr void Seed(const std::uint32_t seed) noexcept {
    state_ = seed;
  }

  [[nodiscard]] constexpr std::uint32_t Next() noexcept {
    state_ = kClientCrtRandMultiplier * state_ + kClientCrtRandIncrement;
    return (state_ >> 16u) & kClientCrtRandMask;
  }

 private:
  std::uint32_t state_;
};

}

#pragma once

#include <array>
#include <cstdint>

namespace openwow::game {

inline constexpr std::array<std::uint8_t, 20>
    kTrivialLevelDifferenceByPlayerBucket{
        4u, 4u, 5u, 5u, 6u, 6u, 7u, 7u, 8u, 8u,
        8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u, 8u,
    };

[[nodiscard]] constexpr std::uint32_t GetTrivialLevelDifference(
    const std::uint32_t player_level) {
  const auto bucket = player_level / 5u;
  return bucket < kTrivialLevelDifferenceByPlayerBucket.size()
             ? kTrivialLevelDifferenceByPlayerBucket[bucket]
             : kTrivialLevelDifferenceByPlayerBucket.back();
}

[[nodiscard]] constexpr bool IsLevelTrivial(
    const std::uint32_t player_level, const std::uint32_t target_level) {
  return player_level > target_level &&
         player_level - target_level > GetTrivialLevelDifference(player_level);
}

}

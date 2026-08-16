#pragma once

#include <array>
#include <cstdint>

namespace openwow::game {

inline std::array<std::uint32_t, 7> BuildCompletedAchievementSortKey(
    const std::uint32_t packed_time, const std::uint32_t order_in_group) {
  return {
      (packed_time >> 24) & 0x1F,
      (packed_time >> 20) & 0x0F,
      (packed_time >> 14) & 0x3F,
      (packed_time >> 11) & 0x07,
      (packed_time >> 6) & 0x1F,
      packed_time & 0x3F,
      order_in_group,
  };
}

inline bool CompletedAchievementSortsBefore(
    const std::uint32_t lhs_packed_time, const std::uint32_t lhs_order_in_group,
    const std::uint32_t rhs_packed_time, const std::uint32_t rhs_order_in_group) {
  return BuildCompletedAchievementSortKey(lhs_packed_time, lhs_order_in_group) >
         BuildCompletedAchievementSortKey(rhs_packed_time, rhs_order_in_group);
}

}

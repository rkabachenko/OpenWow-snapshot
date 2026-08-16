#pragma once

#include <cstdint>

#include "openwow/runtime/time/game_time.h"

namespace openwow::game {

struct PackedCalendarTimeParts {
  std::uint32_t year = 2000;
  std::uint32_t month = 1;
  std::uint32_t day = 1;
  std::uint32_t hour = 0;
  std::uint32_t minute = 0;
};

inline constexpr std::uint32_t kCalendarPackedMinuteMask = 0x3Fu;
inline constexpr std::uint32_t kCalendarPackedHourMask = 0x1Fu;
inline constexpr std::uint32_t kCalendarPackedWeekdayMask = 0x7u;
inline constexpr std::uint32_t kCalendarPackedDayMask = 0x3Fu;
inline constexpr std::uint32_t kCalendarPackedMonthMask = 0xFu;
inline constexpr std::uint32_t kCalendarPackedYearMask = 0x1Fu;
inline constexpr std::uint32_t kCalendarPackedYearBase = 2000u;
inline constexpr std::int64_t kCalendarNsPerMinute =
    60LL * 1000LL * 1000LL * 1000LL;

[[nodiscard]] constexpr PackedCalendarTimeParts DecodePackedCalendarTime(
    const std::uint32_t packed_time) noexcept {
  return {
      .year = ((packed_time >> 24) & kCalendarPackedYearMask) +
              kCalendarPackedYearBase,
      .month = ((packed_time >> 20) & kCalendarPackedMonthMask) + 1u,
      .day = ((packed_time >> 14) & kCalendarPackedDayMask) + 1u,
      .hour = (packed_time >> 6) & kCalendarPackedHourMask,
      .minute = packed_time & kCalendarPackedMinuteMask,
  };
}

[[nodiscard]] constexpr std::uint32_t PackCalendarTime(
    const PackedCalendarTimeParts &parts) noexcept {
  return (parts.minute & kCalendarPackedMinuteMask) |
         ((parts.hour & kCalendarPackedHourMask) << 6) |
         (((parts.day - 1u) & kCalendarPackedDayMask) << 14) |
         (((parts.month - 1u) & kCalendarPackedMonthMask) << 20) |
         (((parts.year - kCalendarPackedYearBase) & kCalendarPackedYearMask)
          << 24);
}

[[nodiscard]] constexpr std::uint32_t PackCalendarDayKey(
    const std::uint32_t year, const std::uint32_t month,
    const std::uint32_t day) noexcept {
  return kCalendarPackedMinuteMask |
         (kCalendarPackedHourMask << 6) |
         (kCalendarPackedWeekdayMask << 11) |
         (((day - 1u) & kCalendarPackedDayMask) << 14) |
         (((month - 1u) & kCalendarPackedMonthMask) << 20) |
         (((year - kCalendarPackedYearBase) & kCalendarPackedYearMask) << 24);
}

[[nodiscard]] constexpr std::uint32_t PackCalendarDayKey(
    const PackedCalendarTimeParts &parts) noexcept {
  return PackCalendarDayKey(parts.year, parts.month, parts.day);
}

[[nodiscard]] constexpr std::uint32_t PackCalendarDayKeyFromPackedTime(
    const std::uint32_t packed_time) noexcept {
  return PackCalendarDayKey(DecodePackedCalendarTime(packed_time));
}

[[nodiscard]] constexpr bool SamePackedCalendarDay(
    const std::uint32_t lhs, const std::uint32_t rhs) noexcept {
  return PackCalendarDayKeyFromPackedTime(lhs) ==
         PackCalendarDayKeyFromPackedTime(rhs);
}

[[nodiscard]] inline std::int64_t PackedCalendarTimeToNsSince2000(
    const std::uint32_t packed_time) {
  const auto parts = DecodePackedCalendarTime(packed_time);
  return ::openwow::core::ida::CalendarTimeNsSince2000FromFields({
      .year = static_cast<std::int32_t>(parts.year),
      .month = static_cast<std::int32_t>(parts.month),
      .day = static_cast<std::int32_t>(parts.day),
      .hour = static_cast<std::int32_t>(parts.hour),
      .minute = static_cast<std::int32_t>(parts.minute),
      .second = 0,
      .nanoseconds = 0,
  });
}

[[nodiscard]] inline std::uint32_t NsSince2000ToPackedCalendarTime(
    const std::int64_t ns_since_2000) {
  const auto breakdown =
      ::openwow::core::ida::CalendarTimeBreakdownFromNsSince2000(
          ns_since_2000);
  return PackCalendarTime({
      .year = static_cast<std::uint32_t>(breakdown.year),
      .month = static_cast<std::uint32_t>(breakdown.month),
      .day = static_cast<std::uint32_t>(breakdown.day),
      .hour = static_cast<std::uint32_t>(breakdown.hour),
      .minute = static_cast<std::uint32_t>(breakdown.minute),
  });
}

[[nodiscard]] inline std::uint32_t AddSecondsToPackedCalendarTime(
    const std::uint32_t packed_time, const std::uint32_t seconds) {
  return NsSince2000ToPackedCalendarTime(
      PackedCalendarTimeToNsSince2000(packed_time) +
      static_cast<std::int64_t>(seconds / 60u) * kCalendarNsPerMinute);
}

}

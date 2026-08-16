#pragma once

namespace openwow::game {

constexpr float kObservedDayStartHour = 5.5f;
constexpr float kObservedDayEndHour = 21.0f;

[[nodiscard]] constexpr bool IsObservedDaytime(const float hour_of_day) noexcept {
  return hour_of_day >= kObservedDayStartHour
      && hour_of_day < kObservedDayEndHour;
}

[[nodiscard]] constexpr bool IsObservedNighttime(const float hour_of_day) noexcept {
  return !IsObservedDaytime(hour_of_day);
}

}

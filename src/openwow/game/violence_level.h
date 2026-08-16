#pragma once

#include "openwow/game/client_config.h"
#include "openwow/ui/game/cvar_system.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace openwow::game {

inline constexpr std::array<std::string_view, 9>
    kViolenceLevelLocaleOrder = {
        "enUS", "koKR", "frFR", "deDE", "zhCN",
        "zhTW", "esES", "esMX", "ruRU",
};
inline constexpr std::array<std::int32_t, 9>
    kViolenceLevelLocaleMaximum = {2, 1, 2, 2, 1, 2, 2, 2, 2};

[[nodiscard]] inline std::int32_t ParseClientViolenceLevel(
    const std::string_view value) noexcept {
  if (value.empty()) {
    return 0;
  }

  std::size_t index = 0;
  const bool negative = value[index] == '-';
  if (negative) {
    ++index;
  }
  if (index >= value.size() || value[index] < '0' || value[index] > '9') {
    return 0;
  }

  std::uint32_t parsed = static_cast<std::uint32_t>(value[index] - '0');
  while (++index < value.size() && value[index] >= '0' &&
         value[index] <= '9') {
    parsed = parsed * 10u +
             static_cast<std::uint32_t>(value[index] - '0');
  }
  return negative ? static_cast<std::int32_t>(0u - parsed)
                  : static_cast<std::int32_t>(parsed);
}

[[nodiscard]] inline std::int32_t ClientViolenceLevelLocaleMaximum(
    std::string_view locale) noexcept {
  if (locale.empty() || locale == "****") {
    locale = "enUS";
  }
  for (std::size_t index = 0; index < kViolenceLevelLocaleOrder.size();
       ++index) {
    if (locale == kViolenceLevelLocaleOrder[index]) {
      return kViolenceLevelLocaleMaximum[index];
    }
  }
  return kViolenceLevelLocaleMaximum.front();
}

[[nodiscard]] inline std::int32_t ClientViolenceLevelLocaleMaximum() {
  return ClientViolenceLevelLocaleMaximum(ClientConfig::Get().GetLocale());
}

[[nodiscard]] inline std::int32_t ResolveClientViolenceLevel(
    const std::string_view configured_value,
    const std::string_view locale) noexcept {
  const auto locale_maximum = ClientViolenceLevelLocaleMaximum(locale);
  const auto configured = ParseClientViolenceLevel(configured_value);
  return configured < 0 || configured > locale_maximum
             ? locale_maximum
             : configured;
}

[[nodiscard]] inline std::int32_t ResolveClientViolenceLevel(
    const std::string_view configured_value) {
  return ResolveClientViolenceLevel(configured_value,
                                    ClientConfig::Get().GetLocale());
}

[[nodiscard]] inline std::int32_t GetClientViolenceLevel() {
  return ResolveClientViolenceLevel(
      ui::game::CVarSystem::Instance().GetCVar("violenceLevel"));
}

}

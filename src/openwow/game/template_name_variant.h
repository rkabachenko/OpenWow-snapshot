#pragma once

#include "openwow/game/client_config.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace openwow::game {

inline int GetQuestObjectivePluralVariant(const std::uint32_t count) {
  const auto locale = ClientConfig::Get().GetLocale();
  if (locale == "koKR" || locale == "frFR" || locale == "zhCN" || locale == "zhTW") {
    return count > 1 ? 1 : 0;
  }
  if (locale == "ruRU") {
    if ((count % 100u) >= 11u && (count % 100u) <= 14u) {
      return 2;
    }
    if ((count % 10u) == 1u) {
      return 0;
    }
    return (((count % 10u) - 2u) > 2u) ? 2 : 1;
  }
  return count != 1 ? 1 : 0;
}

template <std::size_t N>
[[nodiscard]] inline std::string_view GetTemplateNameVariantOrBase(
    std::string_view base_name,
    const std::array<std::string, N>& alternate_names,
    const std::uint32_t variant_index) {
  if (variant_index > 0) {
    const auto alternate_index = static_cast<std::size_t>(variant_index - 1);
    if (alternate_index < alternate_names.size() && !alternate_names[alternate_index].empty()) {
      return alternate_names[alternate_index];
    }
  }

  return base_name;
}

template <std::size_t N>
[[nodiscard]] inline std::string_view GetQuestObjectiveNameVariantOrBase(
    std::string_view base_name,
    const std::array<std::string, N>& alternate_names,
    const std::uint32_t count) {
  return GetTemplateNameVariantOrBase(
      base_name, alternate_names,
      static_cast<std::uint32_t>(GetQuestObjectivePluralVariant(count)));
}

}

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace openwow::game::declension {

inline constexpr std::array<int, 3> kLuaGenderValues{2, 3, 1};

[[nodiscard]] int MapLuaGenderValueToIndex(std::int32_t gender_value);
[[nodiscard]] bool StartsWithCyrillicCodeUnit(std::string_view text);
[[nodiscard]] int GetNumSets(std::string_view name, int gender_index);
[[nodiscard]] bool BuildForms(std::string_view name, int gender_index,
                              unsigned declension_set_index,
                              std::array<std::string, 5>& out_forms);
[[nodiscard]] std::uint8_t ValidateDeclinedCharacterForm(
    std::string_view base_name, std::string_view declined_form);

}

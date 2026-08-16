
#pragma once

#include <cstdint>
#include <string>

namespace openwow::game {

[[nodiscard]] constexpr const char* CharCreate_GetRaceModelToken(
    const std::uint32_t race_id) {
  switch (race_id) {
    case 1: return "Human";
    case 2: return "Orc";
    case 3: return "Dwarf";
    case 4: return "NightElf";
    case 5: return "Scourge";
    case 6: return "Tauren";
    case 7: return "Gnome";
    case 8: return "Troll";
    case 9: return "Goblin";
    case 10: return "BloodElf";
    case 11: return "Draenei";
    default: return "";
  }
}

[[nodiscard]] constexpr const char* CharCreate_GetGenderModelToken(
    const std::uint8_t gender) {
  return gender == 0 ? "Male" : "Female";
}

[[nodiscard]] inline std::string CharCreate_GetDeathKnightModelName(
    const std::uint32_t race_id, const std::uint8_t gender) {
  std::string model_name = "Death Knight ";
  model_name += CharCreate_GetRaceModelToken(race_id);
  model_name += ' ';
  model_name += CharCreate_GetGenderModelToken(gender);
  return model_name;
}

}

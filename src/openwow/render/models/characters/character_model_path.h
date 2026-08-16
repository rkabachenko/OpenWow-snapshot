#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace openwow::render {

struct CharacterAppearance {
  std::uint8_t race{1};
  std::uint8_t gender{0};
  std::uint8_t skin_color{0};
  std::uint8_t face{0};
  std::uint8_t hair_style{0};
  std::uint8_t hair_color{0};
  std::uint8_t facial_hair{0};
  struct EquipmentVisual {
    std::uint32_t display_id{0};
    std::uint8_t inventory_type{0};
    std::uint32_t enchant_visual{0};
  };
  std::array<EquipmentVisual, 23> equipment{};
  std::vector<std::uint32_t> aura_visuals;
};

[[nodiscard]] inline std::string CharacterModelPath(
    const std::uint8_t race, const std::uint8_t gender) {
  const char* race_name = nullptr;
  switch (race) {
    case 1: race_name = "Human"; break;
    case 2: race_name = "Orc"; break;
    case 3: race_name = "Dwarf"; break;
    case 4: race_name = "NightElf"; break;
    case 5: race_name = "Scourge"; break;
    case 6: race_name = "Tauren"; break;
    case 7: race_name = "Gnome"; break;
    case 8: race_name = "Troll"; break;
    case 10: race_name = "BloodElf"; break;
    case 11: race_name = "Draenei"; break;
    default: return {};
  }
  const char* sex = gender == 0 ? "Male" : "Female";
  return "Character\\" + std::string(race_name) + "\\" + sex + "\\" +
         race_name + sex + ".m2";
}

}

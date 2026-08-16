#include "openwow/ui/screens/character_create_screen.h"

namespace openwow::ui::screens {

namespace {

const char* RaceNameForId(int race_id) {
  switch (race_id) {
    case 1:  return "Human";
    case 2:  return "Orc";
    case 3:  return "Dwarf";
    case 4:  return "Night Elf";
    case 5:  return "Undead";
    case 6:  return "Tauren";
    case 7:  return "Gnome";
    case 8:  return "Troll";
    case 10: return "Blood Elf";
    case 11: return "Draenei";
    default: return "Unknown";
  }
}

const char* ClassNameForId(int class_id) {
  switch (class_id) {
    case 1:  return "Warrior";
    case 2:  return "Paladin";
    case 3:  return "Hunter";
    case 4:  return "Rogue";
    case 5:  return "Priest";
    case 6:  return "Death Knight";
    case 7:  return "Shaman";
    case 8:  return "Mage";
    case 9:  return "Warlock";
    case 11: return "Druid";
    default: return "Unknown";
  }
}

const char* FactionForRace(int race_id) {
  switch (race_id) {
    case 1: case 3: case 4: case 7: case 11:
      return "Alliance";
    case 2: case 5: case 6: case 8: case 10:
      return "Horde";
    default:
      return "Unknown";
  }
}

}

void CharacterCreateScreen::SyncFromGameState(const openwow::ui::glue::GlueGameState& gs) {
  selected_race_ = gs.create_race;
  selected_class_ = gs.create_class;
  selected_sex_ = gs.create_sex;
  race_name_ = RaceNameForId(selected_race_);
  class_name_ = ClassNameForId(selected_class_);
  faction_label_ = FactionForRace(selected_race_);
}

}

#include "openwow/net/adapters/presentation/char_enum_display.h"

#include <algorithm>
#include <unordered_map>

namespace openwow::net {

void CharEnumDisplay::SetCharacters(std::vector<CharEnumEntry> chars) {
  characters_ = std::move(chars);
}

const std::vector<CharEnumEntry>& CharEnumDisplay::GetCharacters() const {
  return characters_;
}

std::optional<CharEnumEntry> CharEnumDisplay::GetCharacter(game::ObjectGuid guid) const {
  for (const auto& c : characters_) {
    if (c.guid == guid) return c;
  }
  return std::nullopt;
}

size_t CharEnumDisplay::GetCharacterCount() const { return characters_.size(); }

std::optional<game::ObjectGuid> CharEnumDisplay::GetSelectedCharacter() const {
  return selected_;
}

bool CharEnumDisplay::SelectCharacter(game::ObjectGuid guid) {
  for (const auto& c : characters_) {
    if (c.guid == guid) {
      selected_ = guid;
      return true;
    }
  }
  return false;
}

uint8_t CharEnumDisplay::GetMaxCharacters() const {
  return kMaxCharactersPerRealm;
}

bool CharEnumDisplay::CanCreateMore() const {
  return characters_.size() < kMaxCharactersPerRealm;
}

std::string CharEnumDisplay::GetClassName(uint8_t classId) {
  switch (classId) {
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

std::string CharEnumDisplay::GetRaceName(uint8_t race) {
  switch (race) {
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

std::string CharEnumDisplay::GetZoneName(uint32_t zoneId) {

  static const std::unordered_map<uint32_t, const char*> kZoneNames = {
      {1,    "Dun Morogh"},
      {12,   "Elwynn Forest"},
      {14,   "Durotar"},
      {85,   "Tirisfal Glades"},
      {130,  "Silverpine Forest"},
      {141,  "Teldrassil"},
      {215,  "Mulgore"},
      {3430, "Eversong Woods"},
      {3524, "Azuremyst Isle"},
      {4298, "Plaguelands: The Scarlet Enclave"},
      {1497, "Undercity"},
      {1519, "Stormwind City"},
      {1537, "Ironforge"},
      {1637, "Orgrimmar"},
      {1638, "Thunder Bluff"},
      {1657, "Darnassus"},
      {3487, "Silvermoon City"},
      {3557, "The Exodar"},
      {4395, "Dalaran"},
      {3703, "Shattrath City"},
      {15,   "Dustwallow Marsh"},
      {17,   "The Barrens"},
      {33,   "Stranglethorn Vale"},
      {36,   "Alterac Mountains"},
      {38,   "Loch Modan"},
      {40,   "Westfall"},
      {44,   "Redridge Mountains"},
      {45,   "Arathi Highlands"},
      {46,   "Burning Steppes"},
      {47,   "The Hinterlands"},
      {51,   "Searing Gorge"},
      {65,   "Swamp of Sorrows"},
      {66,   "Deadwind Pass"},
      {67,   "Stormwind"},
      {130,  "Silverpine Forest"},
      {148,  "Darkshore"},
      {267,  "Hillsbrad Foothills"},
      {331,  "Ashenvale"},
      {357,  "Feralas"},
      {361,  "Felwood"},
      {394,  "Grizzly Hills"},
      {400,  "Thousand Needles"},
      {405,  "Desolace"},
      {406,  "Stonetalon Mountains"},
      {440,  "Tanaris"},
      {490,  "Un'Goro Crater"},
      {493,  "Moonglade"},
      {618,  "Winterspring"},
      {1377, "Silithus"},
      {3483, "Hellfire Peninsula"},
      {3518, "Nagrand"},
      {3519, "Terokkar Forest"},
      {3520, "Shadowmoon Valley"},
      {3521, "Zangarmarsh"},
      {3522, "Blade's Edge Mountains"},
      {3523, "Netherstorm"},
      {65,   "Dragonblight"},
      {66,   "Zul'Drak"},
      {67,   "The Storm Peaks"},
      {210,  "Icecrown"},
      {394,  "Grizzly Hills"},
      {495,  "Howling Fjord"},
      {3537, "Borean Tundra"},
      {4197, "Wintergrasp"},
      {4742, "Hrothgar's Landing"},
      {3711, "Sholazar Basin"},
      {4080, "Isle of Quel'Danas"},
  };

  auto it = kZoneNames.find(zoneId);
  if (it != kZoneNames.end()) return it->second;

  return "Zone " + std::to_string(zoneId);
}

std::string CharEnumDisplay::GetLevelText(uint8_t level) {
  return "Level " + std::to_string(level);
}

bool CharEnumDisplay::IsDeathKnight(uint8_t classId) {
  return classId == 6;
}

bool CharEnumDisplay::HasBannedFlag(uint32_t charFlags) {
  return (charFlags & CharFlag::Banned) != 0;
}

void CharEnumDisplay::Clear() {
  characters_.clear();
  selected_.reset();
}

}

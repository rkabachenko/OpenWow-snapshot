
#include "openwow/game/spell_failure_names.h"

#include <cstdio>

#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/core/localized_format.h"
#include "openwow/game/localization.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"

namespace openwow::game {

const char* SpellFailedReasonToString(std::uint32_t reason) {
  switch (reason) {
    case 0: return "SPELL_FAILED_SUCCESS";
    case 1: return "SPELL_FAILED_AFFECTING_COMBAT";
    case 2: return "SPELL_FAILED_ALREADY_AT_FULL_HEALTH";
    case 3: return "SPELL_FAILED_ALREADY_AT_FULL_MANA";
    case 4: return "SPELL_FAILED_ALREADY_AT_FULL_POWER";
    case 5: return "SPELL_FAILED_ALREADY_BEING_TAMED";
    case 6: return "SPELL_FAILED_ALREADY_HAVE_CHARM";
    case 7: return "SPELL_FAILED_ALREADY_HAVE_SUMMON";
    case 8: return "SPELL_FAILED_ALREADY_OPEN";
    case 9: return "SPELL_FAILED_AURA_BOUNCED";
    case 10: return "SPELL_FAILED_AUTOTRACK_INTERRUPTED";
    case 11: return "SPELL_FAILED_BAD_IMPLICIT_TARGETS";
    case 12: return "SPELL_FAILED_BAD_TARGETS";
    case 13: return "SPELL_FAILED_CANT_BE_CHARMED";
    case 14: return "SPELL_FAILED_CANT_BE_DISENCHANTED";
    case 15: return "SPELL_FAILED_CANT_BE_DISENCHANTED_SKILL";
    case 16: return "SPELL_FAILED_CANT_BE_MILLED";
    case 17: return "SPELL_FAILED_CANT_BE_PROSPECTED";
    case 18: return "SPELL_FAILED_CANT_CAST_ON_TAPPED";
    case 19: return "SPELL_FAILED_CANT_DUEL_WHILE_INVISIBLE";
    case 20: return "SPELL_FAILED_CANT_DUEL_WHILE_STEALTHED";
    case 21: return "SPELL_FAILED_CANT_STEALTH";
    case 22: return "SPELL_FAILED_CASTER_AURASTATE";
    case 23: return "SPELL_FAILED_CASTER_DEAD";
    case 24: return "SPELL_FAILED_CHARMED";
    case 25: return "SPELL_FAILED_CHEST_IN_USE";
    case 26: return "SPELL_FAILED_CONFUSED";
    case 27: return "SPELL_FAILED_DONT_REPORT";
    case 28: return "SPELL_FAILED_EQUIPPED_ITEM";
    case 29: return "SPELL_FAILED_EQUIPPED_ITEM_CLASS";
    case 30: return "SPELL_FAILED_EQUIPPED_ITEM_CLASS_MAINHAND";
    case 31: return "SPELL_FAILED_EQUIPPED_ITEM_CLASS_OFFHAND";
    case 32: return "SPELL_FAILED_ERROR";
    case 33: return "SPELL_FAILED_FIZZLE";
    case 34: return "SPELL_FAILED_FLEEING";
    case 35: return "SPELL_FAILED_FOOD_LOWLEVEL";
    case 36: return "SPELL_FAILED_HIGHLEVEL";
    case 37: return "SPELL_FAILED_HUNGER_SATIATED";
    case 38: return "SPELL_FAILED_IMMUNE";
    case 39: return "SPELL_FAILED_INCORRECT_AREA";
    case 40: return "SPELL_FAILED_INTERRUPTED";
    case 41: return "SPELL_FAILED_INTERRUPTED_COMBAT";
    case 42: return "SPELL_FAILED_ITEM_ALREADY_ENCHANTED";
    case 43: return "SPELL_FAILED_ITEM_GONE";
    case 44: return "SPELL_FAILED_ITEM_NOT_FOUND";
    case 45: return "SPELL_FAILED_ITEM_NOT_READY";
    case 46: return "SPELL_FAILED_LEVEL_REQUIREMENT";
    case 47: return "SPELL_FAILED_LINE_OF_SIGHT";
    case 48: return "SPELL_FAILED_LOWLEVEL";
    case 49: return "SPELL_FAILED_LOW_CASTLEVEL";
    case 50: return "SPELL_FAILED_MAINHAND_EMPTY";
    case 51: return "SPELL_FAILED_MOVING";
    case 52: return "SPELL_FAILED_NEED_AMMO";
    case 53: return "SPELL_FAILED_NEED_AMMO_POUCH";
    case 54: return "SPELL_FAILED_NEED_EXOTIC_AMMO";
    case 55: return "SPELL_FAILED_NEED_MORE_ITEMS";
    case 56: return "SPELL_FAILED_NOPATH";
    case 57: return "SPELL_FAILED_NOT_BEHIND";
    case 58: return "SPELL_FAILED_NOT_FISHABLE";
    case 59: return "SPELL_FAILED_NOT_FLYING";
    case 60: return "SPELL_FAILED_NOT_HERE";
    case 61: return "SPELL_FAILED_NOT_INFRONT";
    case 62: return "SPELL_FAILED_NOT_IN_CONTROL";
    case 63: return "SPELL_FAILED_NOT_KNOWN";
    case 64: return "SPELL_FAILED_NOT_MOUNTED";
    case 65: return "SPELL_FAILED_NOT_ON_TAXI";
    case 66: return "SPELL_FAILED_NOT_ON_TRANSPORT";
    case 67: return "SPELL_FAILED_NOT_READY";
    case 68: return "SPELL_FAILED_NOT_SHAPESHIFT";
    case 69: return "SPELL_FAILED_NOT_STANDING";
    case 70: return "SPELL_FAILED_NOT_TRADEABLE";
    case 71: return "SPELL_FAILED_NOT_TRADING";
    case 72: return "SPELL_FAILED_NOT_UNSHEATHED";
    case 73: return "SPELL_FAILED_NOT_WHILE_GHOST";
    case 74: return "SPELL_FAILED_NOT_WHILE_LOOTING";
    case 75: return "SPELL_FAILED_NO_AMMO";
    case 76: return "SPELL_FAILED_NO_CHARGES_REMAIN";
    case 77: return "SPELL_FAILED_NO_CHAMPION";
    case 78: return "SPELL_FAILED_NO_COMBO_POINTS";
    case 79: return "SPELL_FAILED_NO_DUELING";
    case 80: return "SPELL_FAILED_NO_ENDURANCE";
    case 81: return "SPELL_FAILED_NO_FISH";
    case 82: return "SPELL_FAILED_NO_ITEMS_WHILE_SHAPESHIFTED";
    case 83: return "SPELL_FAILED_NO_MOUNTS_ALLOWED";
    case 84: return "SPELL_FAILED_NO_PET";
    case 85: return "SPELL_FAILED_NO_POWER";
    case 86: return "SPELL_FAILED_NOTHING_TO_DISPEL";
    case 87: return "SPELL_FAILED_NOTHING_TO_STEAL";
    case 88: return "SPELL_FAILED_ONLY_ABOVEWATER";
    case 89: return "SPELL_FAILED_ONLY_DAYTIME";
    case 90: return "SPELL_FAILED_ONLY_INDOORS";
    case 91: return "SPELL_FAILED_ONLY_MOUNTED";
    case 92: return "SPELL_FAILED_ONLY_NIGHTTIME";
    case 93: return "SPELL_FAILED_ONLY_OUTDOORS";
    case 94: return "SPELL_FAILED_ONLY_SHAPESHIFT";
    case 95: return "SPELL_FAILED_ONLY_STEALTHED";
    case 96: return "SPELL_FAILED_ONLY_UNDERWATER";
    case 97: return "SPELL_FAILED_OUT_OF_RANGE";
    case 98: return "SPELL_FAILED_PACIFIED";
    case 99: return "SPELL_FAILED_POSSESSED";
    case 100: return "SPELL_FAILED_REAGENTS";
    case 101: return "SPELL_FAILED_REQUIRES_AREA";
    case 102: return "SPELL_FAILED_REQUIRES_SPELL_FOCUS";
    case 103: return "SPELL_FAILED_ROOTED";
    case 104: return "SPELL_FAILED_SILENCED";
    case 105: return "SPELL_FAILED_SPELL_IN_PROGRESS";
    case 106: return "SPELL_FAILED_SPELL_LEARNED";
    case 107: return "SPELL_FAILED_SPELL_UNAVAILABLE";
    case 108: return "SPELL_FAILED_STUNNED";
    case 109: return "SPELL_FAILED_TARGETS_DEAD";
    case 110: return "SPELL_FAILED_TARGET_AFFECTING_COMBAT";
    case 111: return "SPELL_FAILED_TARGET_AURASTATE";
    case 112: return "SPELL_FAILED_TARGET_DUELING";
    case 113: return "SPELL_FAILED_TARGET_ENEMY";
    case 114: return "SPELL_FAILED_TARGET_ENRAGED";
    case 115: return "SPELL_FAILED_TARGET_FRIENDLY";
    case 116: return "SPELL_FAILED_TARGET_IN_COMBAT";
    case 117: return "SPELL_FAILED_TARGET_IS_PLAYER";
    case 118: return "SPELL_FAILED_TARGET_IS_PLAYER_CONTROLLED";
    case 119: return "SPELL_FAILED_TARGET_NOT_DEAD";
    case 120: return "SPELL_FAILED_TARGET_NOT_IN_PARTY";
    case 121: return "SPELL_FAILED_TARGET_NOT_LOOTED";
    case 122: return "SPELL_FAILED_TARGET_NOT_PLAYER";
    case 123: return "SPELL_FAILED_TARGET_NO_POCKETS";
    case 124: return "SPELL_FAILED_TARGET_NO_WEAPONS";
    case 125: return "SPELL_FAILED_TARGET_NO_RANGED_WEAPONS";
    case 126: return "SPELL_FAILED_TARGET_UNSKINNABLE";
    case 127: return "SPELL_FAILED_THIRST_SATIATED";
    case 128: return "SPELL_FAILED_TOO_CLOSE";
    case 129: return "SPELL_FAILED_TOO_MANY_OF_ITEM";
    case 130: return "SPELL_FAILED_TOTEM_CATEGORY";
    case 131: return "SPELL_FAILED_TOTEMS";
    case 132: return "SPELL_FAILED_TRY_AGAIN";
    case 133: return "SPELL_FAILED_UNIT_NOT_BEHIND";
    case 134: return "SPELL_FAILED_UNIT_NOT_INFRONT";
    case 135: return "SPELL_FAILED_WRONG_PET_FOOD";
    case 136: return "SPELL_FAILED_NOT_WHILE_FATIGUED";
    case 137: return "SPELL_FAILED_TARGET_NOT_IN_INSTANCE";
    case 138: return "SPELL_FAILED_NOT_WHILE_TRADING";
    case 139: return "SPELL_FAILED_TARGET_NOT_IN_RAID";
    case 140: return "SPELL_FAILED_TARGET_FREEFORALL";
    case 141: return "SPELL_FAILED_NO_EDIBLE_CORPSES";
    case 142: return "SPELL_FAILED_ONLY_BATTLEGROUNDS";
    case 143: return "SPELL_FAILED_TARGET_NOT_GHOST";
    case 144: return "SPELL_FAILED_TRANSFORM_UNUSABLE";
    case 145: return "SPELL_FAILED_WRONG_WEATHER";
    case 146: return "SPELL_FAILED_DAMAGE_IMMUNE";
    case 147: return "SPELL_FAILED_PREVENTED_BY_MECHANIC";
    case 148: return "SPELL_FAILED_PLAY_TIME";
    case 149: return "SPELL_FAILED_REPUTATION";
    case 150: return "SPELL_FAILED_MIN_SKILL";
    case 151: return "SPELL_FAILED_NOT_IN_ARENA";
    case 152: return "SPELL_FAILED_NOT_ON_SHAPESHIFT";
    case 153: return "SPELL_FAILED_NOT_ON_STEALTHED";
    case 154: return "SPELL_FAILED_NOT_ON_DAMAGE_IMMUNE";
    case 155: return "SPELL_FAILED_NOT_ON_MOUNTED";
    case 156: return "SPELL_FAILED_TOO_SHALLOW";
    case 157: return "SPELL_FAILED_TARGET_NOT_IN_SANCTUARY";
    case 158: return "SPELL_FAILED_TARGET_IS_TRIVIAL";
    case 159: return "SPELL_FAILED_BM_OR_INVISGOD";
    case 160: return "SPELL_FAILED_EXPERT_RIDING_REQUIREMENT";
    case 161: return "SPELL_FAILED_ARTISAN_RIDING_REQUIREMENT";
    case 162: return "SPELL_FAILED_NOT_IDLE";
    case 163: return "SPELL_FAILED_NOT_INACTIVE";
    case 164: return "SPELL_FAILED_PARTIAL_PLAYTIME";
    case 165: return "SPELL_FAILED_NO_PLAYTIME";
    case 166: return "SPELL_FAILED_NOT_IN_BATTLEGROUND";
    case 167: return "SPELL_FAILED_NOT_IN_RAID_INSTANCE";
    case 168: return "SPELL_FAILED_ONLY_IN_ARENA";
    case 169: return "SPELL_FAILED_TARGET_LOCKED_TO_RAID_INSTANCE";
    case 170: return "SPELL_FAILED_ON_USE_ENCHANT";
    case 171: return "SPELL_FAILED_NOT_ON_GROUND";
    case 172: return "SPELL_FAILED_CUSTOM_ERROR";
    case 173: return "SPELL_FAILED_CANT_DO_THAT_RIGHT_NOW";
    case 174: return "SPELL_FAILED_TOO_MANY_SOCKETS";
    case 175: return "SPELL_FAILED_INVALID_GLYPH";
    case 176: return "SPELL_FAILED_UNIQUE_GLYPH";
    case 177: return "SPELL_FAILED_GLYPH_SOCKET_LOCKED";
    case 178: return "SPELL_FAILED_NO_VALID_TARGETS";
    case 179: return "SPELL_FAILED_ITEM_AT_MAX_CHARGES";
    case 180: return "SPELL_FAILED_NOT_IN_BARBERSHOP";
    case 181: return "SPELL_FAILED_FISHING_TOO_LOW";
    case 182: return "SPELL_FAILED_ITEM_ENCHANT_TRADE_WINDOW";
    case 183: return "SPELL_FAILED_SUMMON_PENDING";
    case 184: return "SPELL_FAILED_MAX_SOCKETS";
    case 185: return "SPELL_FAILED_PET_CAN_RENAME";
    case 186: return "SPELL_FAILED_TARGET_CANNOT_BE_RESURRECTED";
    default: return "SPELL_FAILED_UNKNOWN";
  }
}

const char* PowerTypeToString(std::uint32_t power_type) {
  static const char* const kNames[] = {
    "MANA",
    "RAGE",
    "FOCUS",
    "ENERGY",
    "HAPPINESS",
    "RUNES",
    "RUNIC_POWER",
  };
  if (power_type < 7) return kNames[power_type];
  return "";
}

const char* PetTameFailureToString(std::uint8_t code) {
  switch (code) {
    case 1:  return "PETTAME_INVALIDCREATURE";
    case 2:  return "PETTAME_TOOMANY";
    case 3:  return "PETTAME_CREATUREALREADYOWNED";
    case 4:  return "PETTAME_NOTTAMEABLE";
    case 5:  return "PETTAME_ANOTHERSUMMONACTIVE";
    case 6:  return "PETTAME_UNITSCANTTAME";
    case 7:  return "PETTAME_NOPETAVAILABLE";
    case 8:  return "PETTAME_INTERNALERROR";
    case 9:  return "PETTAME_TOOHIGHLEVEL";
    case 10: return "PETTAME_DEAD";
    case 11: return "PETTAME_NOTDEAD";
    case 12: return "PETTAME_CANTCONTROLEXOTIC";
    default: return "PETTAME_UNKNOWNERROR";
  }
}

void DisplaySpellFailedNeedMoreItems(const ItemDefinitions& item_definitions,
                                     const std::uint32_t item_id,
                                     const std::uint32_t quantity) {
  const std::string format = Localization::Get().GetString(
      "SPELL_FAILED_NEED_MORE_ITEMS", "SPELL_FAILED_NEED_MORE_ITEMS");
  const ItemTemplate* const item = item_definitions.GetItem(item_id);

  char message[1024]{};
  if (item != nullptr) {
    core::FormatLocalized(message, sizeof(message), format.c_str(), quantity,
                          item->name.c_str());
  } else {

    core::FormatLocalized(message, sizeof(message), format.c_str(), 0u,
                          "UNKNOWN");
  }
  openwow::ui::game::DisplaySystemMessage(48, message);
}

void DisplaySpellFailedCustomError(const std::uint32_t custom_error_id) {
  char key[64]{};
  std::snprintf(key, sizeof(key), "SPELL_FAILED_CUSTOM_ERROR_%u",
                custom_error_id);
  const std::string message = Localization::Get().GetString(key, key);
  openwow::ui::game::DisplaySystemMessage(48, message.c_str());
}

void DisplayPetTameFailure(const std::uint8_t code) {
  const char* const global_string_key = PetTameFailureToString(code);
  const std::string localized_reason =
      Localization::Get().GetString(global_string_key, global_string_key);
  openwow::ui::game::DisplaySystemMessage(253, localized_reason.c_str());
}

void SpellCastFailure_OnItemTemplateReady(const ItemDefinitions& item_definitions,
                                          std::uint32_t item_entry,
                                          std::uint32_t ,
                                          std::uint32_t spell_cast_result) {

  const char* const key = SpellFailedReasonToString(spell_cast_result);
  const std::string format =
      Localization::Get().GetString(key, key);

  int system_msg_index = 48;
  if (spell_cast_result == 100) {
    system_msg_index = 225;
  } else if (spell_cast_result == 131) {
    system_msg_index = 224;
  }

  const char* item_name = "UNKNOWN";
  const ItemTemplate* tmpl = item_definitions.GetItem(item_entry);
  if (tmpl) {
    item_name = tmpl->name.c_str();
  }

  char buf[1024];
  core::FormatLocalized(buf, sizeof(buf), format.c_str(), item_name);
  openwow::ui::game::DisplaySystemMessage(system_msg_index, buf);
}

}

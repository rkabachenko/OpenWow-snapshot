
#include "openwow/game/update_field_event_mapper.h"

#include <algorithm>
#include <unordered_set>

namespace openwow::game {

namespace evt {

static constexpr const char* UNIT_HEALTH             = "UNIT_HEALTH";
static constexpr const char* UNIT_MAXHEALTH          = "UNIT_MAXHEALTH";
static constexpr const char* UNIT_MANA               = "UNIT_MANA";
static constexpr const char* UNIT_RAGE               = "UNIT_RAGE";
static constexpr const char* UNIT_FOCUS              = "UNIT_FOCUS";
static constexpr const char* UNIT_ENERGY             = "UNIT_ENERGY";
static constexpr const char* UNIT_HAPPINESS          = "UNIT_HAPPINESS";
static constexpr const char* UNIT_RUNIC_POWER        = "UNIT_RUNIC_POWER";
static constexpr const char* UNIT_MAXMANA            = "UNIT_MAXMANA";
static constexpr const char* UNIT_MAXRAGE            = "UNIT_MAXRAGE";
static constexpr const char* UNIT_MAXFOCUS           = "UNIT_MAXFOCUS";
static constexpr const char* UNIT_MAXENERGY          = "UNIT_MAXENERGY";
static constexpr const char* UNIT_MAXHAPPINESS       = "UNIT_MAXHAPPINESS";
static constexpr const char* UNIT_MAXRUNIC_POWER     = "UNIT_MAXRUNIC_POWER";
static constexpr const char* UNIT_LEVEL              = "UNIT_LEVEL";
static constexpr const char* UNIT_FLAGS              = "UNIT_FLAGS";
static constexpr const char* UNIT_DYNAMIC_FLAGS      = "UNIT_DYNAMIC_FLAGS";
static constexpr const char* UNIT_TARGET             = "UNIT_TARGET";
static constexpr const char* UNIT_MODEL_CHANGED      = "UNIT_MODEL_CHANGED";
static constexpr const char* UNIT_STATS              = "UNIT_STATS";
static constexpr const char* UNIT_ATTACK_POWER       = "UNIT_ATTACK_POWER";
static constexpr const char* UNIT_ATTACK_SPEED       = "UNIT_ATTACK_SPEED";
static constexpr const char* UNIT_DAMAGE             = "UNIT_DAMAGE";
static constexpr const char* UNIT_RANGEDDAMAGE       = "UNIT_RANGEDDAMAGE";
static constexpr const char* UNIT_RANGED_ATTACK_POWER= "UNIT_RANGED_ATTACK_POWER";
static constexpr const char* UNIT_RESISTANCES        = "UNIT_RESISTANCES";
static constexpr const char* UNIT_FACTION            = "UNIT_FACTION";
static constexpr const char* UNIT_DISPLAYPOWER       = "UNIT_DISPLAYPOWER";
static constexpr const char* UNIT_PORTRAIT_UPDATE    = "UNIT_PORTRAIT_UPDATE";
static constexpr const char* UNIT_INVENTORY_CHANGED  = "UNIT_INVENTORY_CHANGED";
static constexpr const char* UNIT_PET                = "UNIT_PET";
static constexpr const char* UNIT_DEFENSE            = "UNIT_DEFENSE";

static constexpr const char* PLAYER_FLAGS_CHANGED    = "PLAYER_FLAGS_CHANGED";
static constexpr const char* PLAYER_GUILD_UPDATE     = "PLAYER_GUILD_UPDATE";
static constexpr const char* UNIT_QUEST_LOG_CHANGED  = "UNIT_QUEST_LOG_CHANGED";
static constexpr const char* SPELLS_CHANGED          = "SPELLS_CHANGED";
}

static bool InRange(std::uint16_t field, std::uint16_t lo, std::uint16_t count) {
  return field >= lo && field < lo + count;
}

static std::uint8_t PowerTypeFromField(std::uint16_t field) {
  if (field >= UNIT_FIELD_POWER1 && field <= UNIT_FIELD_POWER7)
    return static_cast<std::uint8_t>(field - UNIT_FIELD_POWER1);
  if (field >= UNIT_FIELD_MAXPOWER1 && field <= UNIT_FIELD_MAXPOWER7)
    return static_cast<std::uint8_t>(field - UNIT_FIELD_MAXPOWER1);
  return 0;
}

static const char* PowerEventName(std::uint8_t power_type) {
  switch (power_type) {
    case 0: return evt::UNIT_MANA;
    case 1: return evt::UNIT_RAGE;
    case 2: return evt::UNIT_FOCUS;
    case 3: return evt::UNIT_ENERGY;
    case 4: return evt::UNIT_HAPPINESS;
    case 5: return nullptr;
    case 6: return evt::UNIT_RUNIC_POWER;
    default: return nullptr;
  }
}

static const char* MaxPowerEventName(std::uint8_t power_type) {
  switch (power_type) {
    case 0: return evt::UNIT_MAXMANA;
    case 1: return evt::UNIT_MAXRAGE;
    case 2: return evt::UNIT_MAXFOCUS;
    case 3: return evt::UNIT_MAXENERGY;
    case 4: return evt::UNIT_MAXHAPPINESS;
    case 5: return nullptr;
    case 6: return evt::UNIT_MAXRUNIC_POWER;
    default: return nullptr;
  }
}

std::vector<std::uint16_t> ExtractFieldIndices(
    const std::vector<std::uint32_t>& bitmask) {
  std::vector<std::uint16_t> indices;
  for (std::size_t block = 0; block < bitmask.size(); ++block) {
    std::uint32_t mask = bitmask[block];
    while (mask) {
      std::uint32_t bit = mask & (~mask + 1);
      std::uint32_t bit_pos = 0;
      {
        std::uint32_t tmp = bit;
        while (tmp >>= 1) ++bit_pos;
      }
      indices.push_back(
          static_cast<std::uint16_t>(block * 32 + bit_pos));
      mask &= ~bit;
    }
  }
  return indices;
}

std::vector<FieldEvent> MapChangedFieldsToEvents(
    TypeID type_id,
    std::uint64_t guid_raw,
    const std::vector<std::uint16_t>& updated_fields,
    bool is_create) {

  if (is_create) {
    return {};
  }

  std::unordered_set<const char*> seen;
  std::vector<FieldEvent> events;

  auto emit = [&](const char* name, bool per_unit, std::uint8_t power = 0) {
    if (!name) return;
    if (seen.count(name)) return;
    seen.insert(name);
    events.push_back({name, per_unit, guid_raw, power});
  };

  bool is_unit = (type_id == TypeID::kUnit || type_id == TypeID::kPlayer);

  if (is_unit) {
    for (std::uint16_t f : updated_fields) {

      if (f == UNIT_FIELD_HEALTH) {
        emit(evt::UNIT_HEALTH, true);
        continue;
      }
      if (f == UNIT_FIELD_MAXHEALTH) {
        emit(evt::UNIT_MAXHEALTH, true);
        continue;
      }

      if (f >= UNIT_FIELD_POWER1 && f <= UNIT_FIELD_POWER7) {
        std::uint8_t pt = PowerTypeFromField(f);
        const char* specific = PowerEventName(pt);
        if (specific) emit(specific, true, pt);
        continue;
      }

      if (f >= UNIT_FIELD_MAXPOWER1 && f <= UNIT_FIELD_MAXPOWER7) {
        std::uint8_t pt = PowerTypeFromField(f);
        const char* specific = MaxPowerEventName(pt);
        if (specific) emit(specific, true, pt);
        continue;
      }

      if (f == UNIT_FIELD_LEVEL) {
        emit(evt::UNIT_LEVEL, true);
        continue;
      }

      if (f == UNIT_FIELD_FLAGS || f == UNIT_FIELD_FLAGS_2) {
        emit(evt::UNIT_FLAGS, true);
        continue;
      }

      if (f == UNIT_FIELD_AURASTATE) {
        continue;
      }

      if (f == UNIT_DYNAMIC_FLAGS) {
        emit(evt::UNIT_DYNAMIC_FLAGS, true);
        continue;
      }

      if (f == UNIT_FIELD_TARGET || f == UNIT_FIELD_TARGET + 1) {
        emit(evt::UNIT_TARGET, true);
        continue;
      }

      if (f == UNIT_FIELD_DISPLAYID || f == UNIT_FIELD_NATIVEDISPLAYID ||
          f == UNIT_FIELD_MOUNTDISPLAYID) {
        emit(evt::UNIT_MODEL_CHANGED, true);
        emit(evt::UNIT_PORTRAIT_UPDATE, true);
        continue;
      }

      if (f >= UNIT_FIELD_STAT0 && f <= UNIT_FIELD_STAT4) {
        emit(evt::UNIT_STATS, true);
        continue;
      }

      if (f >= UNIT_FIELD_POSSTAT0 && f <= UNIT_FIELD_POSSTAT4) {
        emit(evt::UNIT_STATS, true);
        continue;
      }
      if (f >= UNIT_FIELD_NEGSTAT0 && f <= UNIT_FIELD_NEGSTAT4) {
        emit(evt::UNIT_STATS, true);
        continue;
      }

      if (f == UNIT_FIELD_ATTACK_POWER ||
          f == UNIT_FIELD_ATTACK_POWER_MODS ||
          f == UNIT_FIELD_ATTACK_POWER_MULTIPLIER) {
        emit(evt::UNIT_ATTACK_POWER, true);
        continue;
      }

      if (f == UNIT_FIELD_RANGED_ATTACK_POWER ||
          f == UNIT_FIELD_RANGED_ATTACK_POWER_MODS ||
          f == UNIT_FIELD_RANGED_ATTACK_POWER_MULTIPLIER) {
        emit(evt::UNIT_RANGED_ATTACK_POWER, true);
        continue;
      }

      if (f == UNIT_FIELD_BASEATTACKTIME || f == UNIT_FIELD_BASEATTACKTIME + 1 ||
          f == UNIT_FIELD_RANGEDATTACKTIME) {
        emit(evt::UNIT_ATTACK_SPEED, true);
        continue;
      }

      if (f == UNIT_FIELD_MINDAMAGE || f == UNIT_FIELD_MAXDAMAGE ||
          f == UNIT_FIELD_MINOFFHANDDAMAGE || f == UNIT_FIELD_MAXOFFHANDDAMAGE) {
        emit(evt::UNIT_DAMAGE, true);
        continue;
      }

      if (f == UNIT_FIELD_MINRANGEDDAMAGE || f == UNIT_FIELD_MAXRANGEDDAMAGE) {
        emit(evt::UNIT_RANGEDDAMAGE, true);
        continue;
      }

      if (InRange(f, UNIT_FIELD_RESISTANCES, 7) ||
          InRange(f, UNIT_FIELD_RESISTANCEBUFFMODSPOSITIVE, 7) ||
          InRange(f, UNIT_FIELD_RESISTANCEBUFFMODSNEGATIVE, 7)) {
        emit(evt::UNIT_RESISTANCES, true);

        if (f == UNIT_FIELD_RESISTANCES ||
            f == UNIT_FIELD_RESISTANCEBUFFMODSPOSITIVE ||
            f == UNIT_FIELD_RESISTANCEBUFFMODSNEGATIVE) {
          emit(evt::UNIT_DEFENSE, true);
        }
        continue;
      }

      if (f == UNIT_FIELD_FACTIONTEMPLATE) {
        emit(evt::UNIT_FACTION, true);
        continue;
      }

      if (f == UNIT_FIELD_BYTES_0) {
        emit(evt::UNIT_DISPLAYPOWER, true);
        emit(evt::UNIT_PORTRAIT_UPDATE, true);
        continue;
      }

      if (f == UNIT_FIELD_SUMMON || f == UNIT_FIELD_SUMMON + 1 ||
          f == UNIT_FIELD_CHARM || f == UNIT_FIELD_CHARM + 1 ||
          f == UNIT_FIELD_CRITTER || f == UNIT_FIELD_CRITTER + 1) {
        emit(evt::UNIT_PET, true);
        continue;
      }

      if (f == UNIT_NPC_FLAGS) {
        emit(evt::UNIT_FLAGS, true);
        continue;
      }

      if (InRange(f, UNIT_FIELD_POWER_COST_MODIFIER, 7) ||
          InRange(f, UNIT_FIELD_POWER_COST_MULTIPLIER, 7)) {
        emit(evt::UNIT_STATS, true);
        continue;
      }

      if (f == UNIT_MOD_CAST_SPEED) {
        emit(evt::UNIT_STATS, true);
        continue;
      }

      if (InRange(f, UNIT_VIRTUAL_ITEM_SLOT_ID, 3)) {
        emit(evt::UNIT_MODEL_CHANGED, true);
        continue;
      }
    }
  }

  if (type_id == TypeID::kPlayer) {
    for (std::uint16_t f : updated_fields) {

      if (f == PLAYER_XP || f == PLAYER_NEXT_LEVEL_XP) {
        continue;
      }

      if (f == PLAYER_FIELD_COINAGE) {
        continue;
      }

      if (f == PLAYER_FLAGS) {
        emit(evt::PLAYER_FLAGS_CHANGED, false);
        continue;
      }

      if (f == PLAYER_GUILDID || f == PLAYER_GUILDRANK) {
        emit(evt::PLAYER_GUILD_UPDATE, true);
        continue;
      }

      if (f >= PLAYER_FIELD_INV_SLOT_HEAD &&
          f < PLAYER_FIELD_CURRENCYTOKEN_SLOT_1 + 64) {
        continue;
      }

      if (f >= PLAYER_QUEST_LOG_1_1 &&
          f < PLAYER_QUEST_LOG_1_1 + 25 * 5) {
        emit(evt::UNIT_QUEST_LOG_CHANGED, false);
        continue;
      }

      if (f >= PLAYER_VISIBLE_ITEM_1_ENTRYID &&
          f < PLAYER_VISIBLE_ITEM_1_ENTRYID + 19 * 2) {
        emit(evt::UNIT_INVENTORY_CHANGED, true);
        emit(evt::UNIT_MODEL_CHANGED, true);
        continue;
      }

      if (f >= PLAYER_FIELD_COMBAT_RATING_1 &&
          f < PLAYER_FIELD_COMBAT_RATING_1 + 25) {
        emit(evt::UNIT_STATS, true);
        continue;
      }

      if (f >= PLAYER_SKILL_INFO_1_1 &&
          f < PLAYER_SKILL_INFO_1_1 + 128 * 3) {
        emit(evt::SPELLS_CHANGED, false);
        continue;
      }

      if (f == PLAYER_SHIELD_BLOCK) {
        continue;
      }

      if (f == PLAYER_BLOCK_PERCENTAGE || f == PLAYER_DODGE_PERCENTAGE ||
          f == PLAYER_PARRY_PERCENTAGE || f == PLAYER_CRIT_PERCENTAGE ||
          f == PLAYER_RANGED_CRIT_PERCENTAGE ||
          f == PLAYER_OFFHAND_CRIT_PERCENTAGE) {
        emit(evt::UNIT_DEFENSE, true);
        emit(evt::UNIT_STATS, true);
        continue;
      }

      if (f >= PLAYER_SPELL_CRIT_PERCENTAGE1 &&
          f < PLAYER_SPELL_CRIT_PERCENTAGE1 + 7) {
        emit(evt::UNIT_STATS, true);
        continue;
      }

      if (f >= PLAYER_FIELD_MOD_DAMAGE_DONE_POS &&
          f < PLAYER_FIELD_MOD_DAMAGE_DONE_POS + 7) {
        emit(evt::UNIT_STATS, true);
        continue;
      }
      if (f >= PLAYER_FIELD_MOD_DAMAGE_DONE_NEG &&
          f < PLAYER_FIELD_MOD_DAMAGE_DONE_NEG + 7) {
        emit(evt::UNIT_STATS, true);
        continue;
      }

      if (f == PLAYER_FIELD_HONOR_CURRENCY || f == PLAYER_FIELD_ARENA_CURRENCY) {
        continue;
      }

      if (f == PLAYER_EXPERTISE || f == PLAYER_OFFHAND_EXPERTISE) {
        emit(evt::UNIT_STATS, true);
        continue;
      }

      if (f == PLAYER_FIELD_MOD_TARGET_RESISTANCE) {
        continue;
      }

    }
  }

  return events;
}

}

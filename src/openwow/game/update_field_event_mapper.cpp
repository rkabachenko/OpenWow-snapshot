
#include "openwow/game/update_field_event_mapper.h"

#include "openwow/game/player_unit_field_event_callbacks.h"

#include <algorithm>
#include <unordered_set>

namespace openwow::game {

namespace evt {

static constexpr const char* UNIT_FLAGS              = "UNIT_FLAGS";
static constexpr const char* UNIT_MODEL_CHANGED      = "UNIT_MODEL_CHANGED";
static constexpr const char* UNIT_STATS              = "UNIT_STATS";
static constexpr const char* UNIT_PORTRAIT_UPDATE    = "UNIT_PORTRAIT_UPDATE";
static constexpr const char* UNIT_INVENTORY_CHANGED  = "UNIT_INVENTORY_CHANGED";
static constexpr const char* UNIT_PET                = "UNIT_PET";
static constexpr const char* UNIT_DEFENSE            = "UNIT_DEFENSE";

static constexpr const char* PLAYER_FLAGS_CHANGED    = "PLAYER_FLAGS_CHANGED";
static constexpr const char* PLAYER_GUILD_UPDATE     = "PLAYER_GUILD_UPDATE";
static constexpr const char* PLAYER_XP_UPDATE        = "PLAYER_XP_UPDATE";
static constexpr const char* PLAYER_MONEY            = "PLAYER_MONEY";
static constexpr const char* UPDATE_EXHAUSTION       = "UPDATE_EXHAUSTION";
static constexpr const char* UNIT_QUEST_LOG_CHANGED  = "UNIT_QUEST_LOG_CHANGED";
static constexpr const char* SKILL_LINES_CHANGED     = "SKILL_LINES_CHANGED";
}

static bool InRange(std::uint16_t field, std::uint16_t lo, std::uint16_t count) {
  return field >= lo && field < lo + count;
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

  auto emit = [&](const char* name, bool per_unit) {
    if (!name) return;
    if (seen.count(name)) return;
    seen.insert(name);
    events.push_back({name, per_unit, guid_raw});
  };

  bool is_unit = (type_id == TypeID::kUnit || type_id == TypeID::kPlayer);

  if (is_unit) {
    std::unordered_set<std::uint32_t> emitted_direct_event_ids;
    for (std::uint16_t f : updated_fields) {
      if (f >= OBJECT_END) {
        std::uint32_t event_id = 0;
        const char* event_name = GetUnitFieldEventNameForUpdatedField(
            static_cast<std::uint32_t>(f - OBJECT_END), &event_id);
        if (event_name != nullptr &&
            emitted_direct_event_ids.insert(event_id).second) {
          events.push_back({event_name, true, guid_raw});
        }
      }

      if (f == UNIT_FIELD_HEALTH || f == UNIT_FIELD_MAXHEALTH ||
          (f >= UNIT_FIELD_POWER1 && f <= UNIT_FIELD_POWER7) ||
          (f >= UNIT_FIELD_MAXPOWER1 && f <= UNIT_FIELD_MAXPOWER7) ||
          f == UNIT_FIELD_LEVEL || f == UNIT_FIELD_FACTIONTEMPLATE ||
          f == UNIT_FIELD_FLAGS || f == UNIT_FIELD_FLAGS_2 ||
          f == UNIT_DYNAMIC_FLAGS || f == UNIT_FIELD_PETEXPERIENCE ||
          f == UNIT_FIELD_PETNEXTLEVELEXP ||
          (f >= UNIT_FIELD_ATTACK_POWER &&
           f <= UNIT_FIELD_RANGED_ATTACK_POWER_MULTIPLIER) ||
          (f >= UNIT_FIELD_BASEATTACKTIME &&
           f <= UNIT_FIELD_RANGEDATTACKTIME) ||
          (f >= UNIT_FIELD_MINDAMAGE && f <= UNIT_FIELD_MAXOFFHANDDAMAGE) ||
          (f >= UNIT_FIELD_MINRANGEDDAMAGE &&
           f <= UNIT_FIELD_MAXRANGEDDAMAGE) ||
          (f >= UNIT_FIELD_STAT0 && f <= UNIT_FIELD_STAT4) ||
          InRange(f, UNIT_FIELD_POWER_COST_MODIFIER, 7) ||
          InRange(f, UNIT_FIELD_POWER_COST_MULTIPLIER, 7) ||
          f == UNIT_FIELD_MAXHEALTHMODIFIER ||
          f == UNIT_FIELD_TARGET || f == UNIT_FIELD_TARGET + 1) {
        continue;
      }

      if (f == UNIT_FIELD_AURASTATE) {
        continue;
      }

      if (f == UNIT_FIELD_DISPLAYID || f == UNIT_FIELD_NATIVEDISPLAYID ||
          f == UNIT_FIELD_MOUNTDISPLAYID) {
        emit(evt::UNIT_MODEL_CHANGED, true);
        emit(evt::UNIT_PORTRAIT_UPDATE, true);
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

      if (InRange(f, UNIT_FIELD_RESISTANCES, 7) ||
          InRange(f, UNIT_FIELD_RESISTANCEBUFFMODSPOSITIVE, 7) ||
          InRange(f, UNIT_FIELD_RESISTANCEBUFFMODSNEGATIVE, 7)) {
        if (f == UNIT_FIELD_RESISTANCES ||
            f == UNIT_FIELD_RESISTANCEBUFFMODSPOSITIVE ||
            f == UNIT_FIELD_RESISTANCEBUFFMODSNEGATIVE) {
          emit(evt::UNIT_DEFENSE, true);
        }
        continue;
      }

      if (f == UNIT_FIELD_BYTES_0) {
        emit(evt::UNIT_PORTRAIT_UPDATE, true);
        continue;
      }

      if (f == UNIT_FIELD_CHARM || f == UNIT_FIELD_CHARM + 1 ||
          f == UNIT_FIELD_SUMMON || f == UNIT_FIELD_SUMMON + 1) {
        continue;
      }

      if (f == UNIT_FIELD_CRITTER || f == UNIT_FIELD_CRITTER + 1) {
        emit(evt::UNIT_PET, true);
        continue;
      }

      if (f == UNIT_NPC_FLAGS) {
        emit(evt::UNIT_FLAGS, true);
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
        events.push_back({evt::PLAYER_XP_UPDATE, false, guid_raw});
        continue;
      }

      if (f == PLAYER_REST_STATE_EXPERIENCE) {
        events.push_back({evt::UPDATE_EXHAUSTION, false, guid_raw});
        continue;
      }

      if (f == PLAYER_FIELD_COINAGE) {
        events.push_back({evt::PLAYER_MONEY, false, guid_raw});
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
        emit(evt::SKILL_LINES_CHANGED, false);
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

#include "openwow/game/skill_line_ability_lookup.h"

namespace openwow::game {
namespace {

[[nodiscard]] bool UsesTradeSkillLookupFallback(
    const std::optional<SkillRaceClassIdentity>& active_identity,
    const std::uint8_t race, const std::uint8_t player_class) {
  return active_identity.has_value() && active_identity->race != 0 &&
         active_identity->player_class != 0 && active_identity->race != race &&
         active_identity->player_class != player_class;
}

}

bool SkillRaceClassMaskMatches(const std::uint32_t mask,
                               const std::uint8_t value) {
  return SkillRaceClassInfoMaskMatches(mask, value);
}

bool SkillRaceClassInfoMatches(const data::dbc::SkillRaceClassInfoEntry& entry,
                               const std::uint8_t race,
                               const std::uint8_t player_class) {
  return SkillRaceClassInfoMaskMatches(entry.race_mask, race) &&
         SkillRaceClassInfoMaskMatches(entry.class_mask, player_class);
}

bool SkillLineAbilityMatchesRaceClass(
    const data::dbc::SkillLineAbilityEntry& entry, const std::uint8_t race,
    const std::uint8_t player_class) {
  const std::uint32_t race_mask =
      entry.exclude_race_mask != 0 ? ~entry.race_mask : entry.race_mask;
  const std::uint32_t class_mask =
      entry.exclude_class_mask != 0 ? ~entry.class_mask : entry.class_mask;
  return SkillRaceClassMaskMatches(race_mask, race) &&
         SkillRaceClassMaskMatches(class_mask, player_class);
}

const data::dbc::SkillRaceClassInfoEntry* FindSkillRaceClassInfoBySkillId(
    const std::span<const data::dbc::SkillRaceClassInfoEntry> entries,
    const std::uint8_t race, const std::uint8_t player_class,
    const std::uint32_t skill_line_id) {
  for (const auto& entry : entries) {
    if (entry.skill_id != skill_line_id ||
        !SkillRaceClassInfoMatches(entry, race, player_class)) {
      continue;
    }

    return &entry;
  }

  return nullptr;
}

const data::dbc::SkillLineAbilityEntry* FindSkillLineAbilityBySkillAndSpell(
    const std::span<const data::dbc::SkillLineAbilityEntry> entries,
    const std::uint32_t skill_line_id, const std::uint32_t spell_id) {
  for (const auto& entry : entries) {
    if (entry.skill_id == skill_line_id && entry.spell_id == spell_id) {
      return &entry;
    }
  }

  return nullptr;
}

const data::dbc::SkillLineAbilityEntry* FindSkillLineAbilityForRaceClassSpell(
    const std::span<const data::dbc::SkillLineAbilityEntry> ability_entries,
    const std::span<const data::dbc::SkillRaceClassInfoEntry> race_class_entries,
    const std::uint8_t race, const std::uint8_t player_class,
    const std::uint32_t spell_id,
    const std::optional<SkillRaceClassIdentity> active_identity) {
  if (spell_id == 0) {
    return nullptr;
  }

  const bool use_first_match =
      UsesTradeSkillLookupFallback(active_identity, race, player_class);
  const data::dbc::SkillLineAbilityEntry* match = nullptr;
  for (const auto& entry : ability_entries) {
    if (entry.spell_id != spell_id ||
        !SkillLineAbilityMatchesRaceClass(entry, race, player_class)) {
      continue;
    }

    if (FindSkillRaceClassInfoBySkillId(race_class_entries, race, player_class,
                                        entry.skill_id) == nullptr) {
      continue;
    }

    if (use_first_match) {
      return &entry;
    }

    match = &entry;
  }

  return match;
}

bool GetMinSkillValueForSpell(
    const std::uint8_t race, const std::uint8_t player_class,
    const std::uint32_t spell_id,
    const std::span<const data::dbc::SkillLineAbilityEntry> ability_entries,
    const std::span<const data::dbc::SkillRaceClassInfoEntry> race_class_entries,
    const std::uint32_t spell_level,
    std::uint32_t* out_min_skill_value) {
  if (spell_id == 0 || out_min_skill_value == nullptr) {
    return false;
  }

  const auto* ability = FindSkillLineAbilityForRaceClassSpell(
      ability_entries, race_class_entries, race, player_class, spell_id);
  if (ability == nullptr) {
    return false;
  }

  const auto* race_class_info = FindSkillRaceClassInfoBySkillId(
      race_class_entries, race, player_class, ability->skill_id);

  if (race_class_info != nullptr) {
    const std::uint32_t min_level = race_class_info->min_level;
    *out_min_skill_value =
        spell_level > min_level ? spell_level : min_level;
  } else {
    *out_min_skill_value = spell_level;
  }

  return true;
}

}

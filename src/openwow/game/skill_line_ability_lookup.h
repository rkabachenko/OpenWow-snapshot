#pragma once

#include "openwow/data/formats/dbc/dbc_entries_gameplay.h"
#include "openwow/data/formats/dbc/dbc_structures.h"

#include <cstdint>
#include <optional>
#include <span>

namespace openwow::game {

[[nodiscard]] bool SkillRaceClassMaskMatches(std::uint32_t mask,
                                             std::uint8_t value);

struct SkillRaceClassIdentity {
  std::uint8_t race = 0;
  std::uint8_t player_class = 0;
};

[[nodiscard]] constexpr bool SkillRaceClassInfoMaskMatches(
    const std::uint32_t mask, const std::uint8_t value) {
  if (mask == 0) {
    return true;
  }

  const auto shift = (static_cast<std::uint32_t>(value) - 1u) & 31u;
  return (mask & (std::uint32_t{1} << shift)) != 0;
}

[[nodiscard]] bool SkillRaceClassInfoMatches(
    const data::dbc::SkillRaceClassInfoEntry& entry, std::uint8_t race,
    std::uint8_t player_class);

[[nodiscard]] bool SkillLineAbilityMatchesRaceClass(
    const data::dbc::SkillLineAbilityEntry& entry, std::uint8_t race,
    std::uint8_t player_class);

[[nodiscard]] const data::dbc::SkillRaceClassInfoEntry*
FindSkillRaceClassInfoBySkillId(
    std::span<const data::dbc::SkillRaceClassInfoEntry> entries,
    std::uint8_t race, std::uint8_t player_class, std::uint32_t skill_line_id);

[[nodiscard]] const data::dbc::SkillLineAbilityEntry*
FindSkillLineAbilityBySkillAndSpell(
    std::span<const data::dbc::SkillLineAbilityEntry> entries,
    std::uint32_t skill_line_id, std::uint32_t spell_id);

[[nodiscard]] const data::dbc::SkillLineAbilityEntry*
FindSkillLineAbilityForRaceClassSpell(
    std::span<const data::dbc::SkillLineAbilityEntry> ability_entries,
    std::span<const data::dbc::SkillRaceClassInfoEntry> race_class_entries,
    std::uint8_t race, std::uint8_t player_class, std::uint32_t spell_id,
    std::optional<SkillRaceClassIdentity> active_identity = std::nullopt);

[[nodiscard]] bool GetMinSkillValueForSpell(
    std::uint8_t race, std::uint8_t player_class, std::uint32_t spell_id,
    std::span<const data::dbc::SkillLineAbilityEntry> ability_entries,
    std::span<const data::dbc::SkillRaceClassInfoEntry> race_class_entries,
    std::uint32_t spell_level, std::uint32_t* out_min_skill_value);

}

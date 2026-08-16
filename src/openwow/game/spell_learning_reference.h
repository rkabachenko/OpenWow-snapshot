#pragma once

#include "openwow/game/spell_query_bridge.h"
#include "openwow/foundation/text/ascii.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace openwow::game {

[[nodiscard]] inline std::optional<std::uint32_t> ParseFirstAsciiDigitRun(
    std::string_view value) {
  std::uint32_t parsed = 0;
  bool saw_digit = false;

  for (const char ch : value) {
    if (ch >= '0' && ch <= '9') {
      parsed = (parsed * 10U) + static_cast<std::uint32_t>(ch - '0');
      saw_digit = true;
      continue;
    }

    if (saw_digit) {
      return parsed;
    }
  }

  if (!saw_digit) {
    return std::nullopt;
  }

  return parsed;
}

[[nodiscard]] inline std::uint32_t ResolveSpellProgressionRank(
    std::string_view subtext,
    const std::uint32_t fallback_rank) {
  return ParseFirstAsciiDigitRun(subtext).value_or(fallback_rank);
}

[[nodiscard]] inline std::uint32_t ResolveSpellProgressionRank(
    const SpellQueryResult& spell) {
  return ResolveSpellProgressionRank(
      spell.subtext, static_cast<std::uint32_t>(spell.rank));
}

[[nodiscard]] inline bool ShouldReplaceLearnedSpellReference(
    const std::uint32_t candidate_spell_id,
    const std::uint32_t old_spell_id,
    const std::optional<SpellQueryResult>& replacement_spell) {
  if (candidate_spell_id == 0) {
    return false;
  }

  if (candidate_spell_id == old_spell_id) {
    return true;
  }

  if (!replacement_spell || replacement_spell->name.empty()) {
    return false;
  }

  const auto existing_spell = SpellQueryBridge::Get().Query(candidate_spell_id);
  if (!existing_spell || existing_spell->name.empty()) {
    return false;
  }

  return text::EqualsIgnoreCaseAscii(existing_spell->name,
                                     replacement_spell->name) &&
         ResolveSpellProgressionRank(*replacement_spell) ==
             ResolveSpellProgressionRank(*existing_spell) + 1U;
}

}

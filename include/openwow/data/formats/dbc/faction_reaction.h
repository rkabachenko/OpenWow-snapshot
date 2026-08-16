#pragma once

#include "openwow/data/formats/dbc/dbc_entries_gameplay.h"
#include "openwow/data/formats/dbc/dbc_store.h"
#include "openwow/game/unit_defines.h"

#include <cstdint>

namespace openwow::data::dbc {

inline constexpr std::uint32_t kFactionMaskPlayer   = 1;
inline constexpr std::uint32_t kFactionMaskAlliance  = 2;
inline constexpr std::uint32_t kFactionMaskHorde     = 4;
inline constexpr std::uint32_t kFactionMaskMonster   = 8;

inline constexpr std::uint32_t kFactionFlagPvP = 0x02;
inline constexpr std::uint32_t kFactionFlagHostileByDefault = 0x2000u;

[[nodiscard]] game::ReactionType ComputeFactionReaction(
    std::uint32_t faction_a, std::uint32_t faction_b,
    const DbcStore<FactionTemplateEntry>& store);

[[nodiscard]] game::ReactionType ComputeFactionReactionForEntries(
    const FactionTemplateEntry& a, const FactionTemplateEntry& b);

[[nodiscard]] int ComputeCorpseReactionLevel(
    std::uint32_t source_template, std::uint32_t target_template,
    const DbcStore<FactionTemplateEntry>& store);

[[nodiscard]] bool IsPvPFactionTemplate(
    std::uint32_t faction_template_id,
    const DbcStore<FactionTemplateEntry>& store);

}

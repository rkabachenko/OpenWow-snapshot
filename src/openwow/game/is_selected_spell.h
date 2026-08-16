#pragma once

#include "openwow/data/formats/dbc/dbc_structures.h"

#include <cstdint>

namespace openwow::game {

inline constexpr std::uint32_t kSpellEffectTradeSkill = 47;
inline constexpr std::uint32_t kSpellAuraTransform    = 36;

[[nodiscard]] bool IsSelectedSpellImpl(
    const data::dbc::SpellEntry* dbc_spell,
    std::uint32_t active_trade_skill_line,
    bool is_trade_skill_linked,
    std::uint32_t spell_skill_line_id,
    std::uint8_t current_shapeshift_form);

}

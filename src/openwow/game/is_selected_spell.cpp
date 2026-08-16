
#include "openwow/game/is_selected_spell.h"

namespace openwow::game {

bool IsSelectedSpellImpl(
    const data::dbc::SpellEntry* dbc_spell,
    const std::uint32_t active_trade_skill_line,
    const bool is_trade_skill_linked,
    const std::uint32_t spell_skill_line_id,
    const std::uint8_t current_shapeshift_form) {
  if (!dbc_spell) {
    return false;
  }

  if (dbc_spell->effect[0] == kSpellEffectTradeSkill) {
    if (spell_skill_line_id != 0 &&
        !is_trade_skill_linked &&
        active_trade_skill_line == spell_skill_line_id) {
      return true;
    }
  }

  for (std::size_t i = 0; i < data::dbc::kMaxSpellEffects; ++i) {
    if (dbc_spell->effect_apply_aura[i] == kSpellAuraTransform) {
      const auto form_id = dbc_spell->effect_misc_value[i];
      if (form_id != 0 &&
          current_shapeshift_form != 0 &&
          static_cast<std::int32_t>(current_shapeshift_form) == form_id) {
        return true;
      }
    }
  }

  return false;
}

}

#pragma once

#include "openwow/data/formats/dbc/dbc_entries_gameplay.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_structures.h"

#include <cstddef>
#include <cstdint>

namespace openwow::game {

inline constexpr std::uint32_t kAttackActionSpellEffectId = 78u;
inline constexpr std::uint32_t kShapeshiftAuraType = 36u;
inline constexpr std::uint32_t kTurnSensitiveShapeshiftFlag = 0x1u;
inline constexpr std::uint32_t kCancelableDisplayModelFlag = 0x4u;

[[nodiscard]] inline bool SpellHasAttackActionEffect(
    const data::dbc::SpellEntry& spell) {
  for (const auto effect_id : spell.effect) {
    if (effect_id == kAttackActionSpellEffectId) {
      return true;
    }
  }

  return false;
}

[[nodiscard]] inline bool SpellAppliesShapeshiftForm(
    const data::dbc::SpellEntry& spell, const std::uint32_t form_id) {
  for (std::size_t effect_index = 0; effect_index < spell.effect.size();
       ++effect_index) {
    if (spell.effect_apply_aura[effect_index] == kShapeshiftAuraType &&
        static_cast<std::uint32_t>(spell.effect_misc_value[effect_index]) ==
            form_id) {
      return true;
    }
  }

  return false;
}

[[nodiscard]] inline bool DisplaySuppressesAttackActionShapeshiftAutoCancel(
    const data::dbc::DbcLoader& dbc, const std::uint32_t display_id) {
  if (display_id == 0u) {
    return false;
  }

  const auto* display = dbc.creature_display_info().LookupEntry(display_id);
  if (display == nullptr) {
    return false;
  }

  if (display->extra_info != 0u &&
      dbc.creature_display_info_extra().LookupEntry(display->extra_info) !=
          nullptr) {
    return true;
  }

  const auto* model = dbc.creature_model_data().LookupEntry(display->model_id);
  return model != nullptr &&
         (model->flags & kCancelableDisplayModelFlag) != 0u;
}

[[nodiscard]] inline bool ShapeshiftFormHasAttackActionCancelableDisplay(
    const data::dbc::DbcLoader& dbc, const std::uint32_t form_id) {
  if (form_id == 0u) {
    return false;
  }

  const auto* form = dbc.spell_shapeshift_form().LookupEntry(form_id);
  if (form == nullptr) {
    return false;
  }

  for (const auto display_id : form->creature_display_id) {
    if (DisplaySuppressesAttackActionShapeshiftAutoCancel(dbc, display_id)) {
      return true;
    }
  }

  return false;
}

}

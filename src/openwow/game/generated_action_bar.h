#pragma once

#include "openwow/data/formats/dbc/dbc_entries_gameplay.h"
#include "openwow/game/actions/application/action_assignment_runtime.h"
#include "openwow/game/pet_manager.h"
#include "openwow/game/shapeshift_bonus_bar.h"

#include <cstddef>
#include <cstdint>

namespace openwow::game {

inline constexpr std::size_t kGeneratedActionBarSlotCount = 12;
inline constexpr std::size_t kOverrideSpellActionCount = 10;
inline constexpr std::uint32_t kGeneratedActionBarBonusOffset =
    kShapeshiftGeneratedBonusBarOffset;
inline constexpr std::uint32_t kOverrideSpellDataAllowUnlistedSpellsFlag = 0x4u;

enum class GeneratedActionBarSource : std::uint8_t {
  kNone,
  kOverrideSpellData,
  kShapeshiftOverride,
  kPet,
};

struct GeneratedActionBarState {
  std::uint32_t bonus_bar_offset{0};
  GeneratedActionBarSource source{GeneratedActionBarSource::kNone};

  [[nodiscard]] bool uses_generated_slots() const {
    return source != GeneratedActionBarSource::kNone;
  }
};

inline bool HasOverrideSpellActions(
    const data::dbc::OverrideSpellDataEntry* entry) {
  if (entry == nullptr) {
    return false;
  }

  for (const auto spell_id : entry->spell) {
    if (spell_id != 0) {
      return true;
    }
  }

  return false;
}

inline bool OverrideSpellDataContainsSpell(
    const data::dbc::OverrideSpellDataEntry* entry,
    const std::uint32_t spell_id) {
  if (entry == nullptr || spell_id == 0u) {
    return false;
  }

  for (const auto listed_spell_id : entry->spell) {
    if (listed_spell_id == spell_id) {
      return true;
    }
  }

  return false;
}

inline bool OverrideSpellDataAllowsSpellLikeAction(
    const data::dbc::OverrideSpellDataEntry* entry,
    const std::uint32_t spell_id) {
  return entry == nullptr ||
         (entry->flags & kOverrideSpellDataAllowUnlistedSpellsFlag) != 0u ||
         OverrideSpellDataContainsSpell(entry, spell_id);
}

inline GeneratedActionBarState DescribeGeneratedActionBar(
    const data::dbc::OverrideSpellDataEntry* override_spell_data,
    const data::dbc::SpellShapeshiftFormEntry* shapeshift_form,
    const PetBarState* pet_bar) {
  if (HasOverrideSpellActions(override_spell_data)) {
    return {kGeneratedActionBarBonusOffset,
            GeneratedActionBarSource::kOverrideSpellData};
  }

  const auto shapeshift_state = DescribeShapeshiftBonusBar(shapeshift_form);
  if (shapeshift_state.uses_generated_slots) {
    return {shapeshift_state.offset,
            GeneratedActionBarSource::kShapeshiftOverride};
  }

  if (pet_bar != nullptr && pet_bar->generated_bar_active) {
    return {kGeneratedActionBarBonusOffset, GeneratedActionBarSource::kPet};
  }

  return {shapeshift_state.offset, GeneratedActionBarSource::kNone};
}

inline ActionPresentationEntry GetGeneratedActionButton(
    GeneratedActionBarSource source,
    const data::dbc::OverrideSpellDataEntry* override_spell_data,
    const data::dbc::SpellShapeshiftFormEntry* shapeshift_form,
    const PetBarState* pet_bar,
    std::size_t slot_index) {
  switch (source) {
    case GeneratedActionBarSource::kOverrideSpellData:
      if (override_spell_data == nullptr ||
          slot_index >= kOverrideSpellActionCount) {
        return {};
      }
      return {override_spell_data->spell[slot_index], ActionPresentationKind::kSpell};

    case GeneratedActionBarSource::kShapeshiftOverride:
      if (shapeshift_form == nullptr) {
        return {};
      }
      return GetShapeshiftGeneratedActionButton(*shapeshift_form, slot_index);

    case GeneratedActionBarSource::kPet:
      if (pet_bar == nullptr || slot_index >= 10) {
        return {};
      }
      if (pet_bar->action_bar[slot_index].raw == 0) {
        return {};
      }
      return {pet_bar->action_bar[slot_index].ActionId(),
              ActionPresentationKind::kPet};

    case GeneratedActionBarSource::kNone:
      return {};
  }

  return {};
}

}

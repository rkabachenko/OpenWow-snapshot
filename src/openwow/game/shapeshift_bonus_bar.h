#pragma once

#include "openwow/data/formats/dbc/dbc_entries_gameplay.h"
#include "openwow/game/actions/application/action_assignment_runtime.h"

#include <cstddef>
#include <cstdint>

namespace openwow::game {

inline constexpr std::size_t kShapeshiftOverrideActionCount = 8;
inline constexpr std::size_t kShapeshiftGeneratedSlotCount = 12;
inline constexpr std::uint32_t kShapeshiftGeneratedBonusBarOffset = 5;

struct ShapeshiftBonusBarState {
  std::uint32_t offset{0};
  bool uses_generated_slots{false};
};

inline bool HasCompleteShapeshiftOverrideBar(
    const data::dbc::SpellShapeshiftFormEntry* entry) {
  if (entry == nullptr) {
    return false;
  }

  for (std::size_t index = 0; index < kShapeshiftOverrideActionCount; ++index) {
    if (entry->override_actions[index] == 0) {
      return false;
    }
  }

  return true;
}

inline ShapeshiftBonusBarState DescribeShapeshiftBonusBar(
    const data::dbc::SpellShapeshiftFormEntry* entry) {
  if (entry == nullptr) {
    return {};
  }

  if (HasCompleteShapeshiftOverrideBar(entry)) {
    return {kShapeshiftGeneratedBonusBarOffset, true};
  }

  return {entry->bonus_action_bar, false};
}

inline ActionPresentationEntry GetShapeshiftGeneratedActionButton(
    const data::dbc::SpellShapeshiftFormEntry& entry,
    std::size_t generated_slot_index) {
  if (!HasCompleteShapeshiftOverrideBar(&entry) ||
      generated_slot_index >= kShapeshiftGeneratedSlotCount ||
      generated_slot_index >= kShapeshiftOverrideActionCount) {
    return {};
  }

  return ActionPresentationEntry::FromAssignedAction(
      actions::Action::Decode(entry.override_actions[generated_slot_index]));
}

}

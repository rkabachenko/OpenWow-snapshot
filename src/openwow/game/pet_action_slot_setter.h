
#pragma once

#include <cstddef>
#include <cstdint>

namespace openwow::game {

inline constexpr std::size_t kPetActionBarSlotCount = 10;
inline constexpr std::uint8_t kPetActionTypeSpell = 1;
inline constexpr std::uint8_t kPetActionTypeReact = 6;
inline constexpr std::uint8_t kPetActionTypeCommand = 7;

struct PetActionSlotMutationResult {
  bool applied{false};
  int secondary_slot{-1};
  std::uint32_t previous_target_action{0};
};

inline std::uint8_t GetPetActionType(std::uint32_t raw_action) {
  return static_cast<std::uint8_t>((raw_action >> 24) & 0x3Fu);
}

inline bool IsPetActionEmptySpell(std::uint32_t raw_action) {
  return GetPetActionType(raw_action) == kPetActionTypeSpell &&
         (raw_action & 0x00FFFFFFu) == 0;
}

inline bool IsPetActionReactOrCommand(std::uint32_t raw_action) {
  const auto action_type = GetPetActionType(raw_action);
  return action_type == kPetActionTypeReact ||
         action_type == kPetActionTypeCommand;
}

inline int FindFirstEmptySpellSlot(const std::uint32_t* slots) {
  for (std::size_t index = 0; index < kPetActionBarSlotCount; ++index) {
    if (!IsPetActionReactOrCommand(slots[index]) &&
        (slots[index] & 0x00FFFFFFu) == 0) {
      return static_cast<int>(index);
    }
  }

  return -1;
}

inline PetActionSlotMutationResult ApplyPetActionSlotMutation(
    std::uint32_t* slots,
    std::size_t slot_index,
    std::uint32_t new_action) {
  PetActionSlotMutationResult result{};
  if (slot_index >= kPetActionBarSlotCount) {
    return result;
  }

  result.previous_target_action = slots[slot_index];
  if (new_action == result.previous_target_action) {
    return result;
  }

  if (!IsPetActionEmptySpell(new_action)) {
    const auto duplicate_key = new_action & 0x3FFFFFFFu;
    for (std::size_t index = 0; index < kPetActionBarSlotCount; ++index) {
      if (index == slot_index) {
        continue;
      }
      if ((slots[index] & 0x3FFFFFFFu) == duplicate_key) {
        slots[index] = 0;
        result.secondary_slot = static_cast<int>(index);
        break;
      }
    }
  }

  if (result.secondary_slot < 0 &&
      IsPetActionReactOrCommand(result.previous_target_action)) {
    result.secondary_slot = FindFirstEmptySpellSlot(slots);
    if (result.secondary_slot < 0) {
      return {};
    }
  }

  if (result.secondary_slot >= 0) {
    slots[static_cast<std::size_t>(result.secondary_slot)] =
        result.previous_target_action;
  }

  slots[slot_index] = new_action;
  result.applied = true;
  return result;
}

}

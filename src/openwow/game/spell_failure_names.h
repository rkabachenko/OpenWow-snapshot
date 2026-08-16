#pragma once

#include <cstdint>

namespace openwow::game {

class ItemDefinitions;

[[nodiscard]] const char* SpellFailedReasonToString(std::uint32_t reason);

[[nodiscard]] const char* PowerTypeToString(std::uint32_t power_type);

[[nodiscard]] const char* PetTameFailureToString(std::uint8_t code);

void DisplaySpellFailedNeedMoreItems(const ItemDefinitions& item_definitions,
                                     std::uint32_t item_id,
                                     std::uint32_t quantity);

void DisplaySpellFailedCustomError(std::uint32_t custom_error_id);

void SpellCastFailure_OnItemTemplateReady(const ItemDefinitions& item_definitions,
                                          std::uint32_t item_entry,
                                          std::uint32_t lookup_context,
                                          std::uint32_t spell_cast_result);

void DisplayPetTameFailure(std::uint8_t code);

}

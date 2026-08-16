#pragma once

#include "openwow/game/actions/bindings/model/binding_types.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace openwow::game {
class BindingProfiles;
}

namespace openwow::game::actions::bindings::adapters::retail {

[[nodiscard]] std::uint8_t ParseModifierBits(
    std::string_view binding,
    std::string_view& remainder);
[[nodiscard]] bool ModifierBitsMatch(std::uint8_t bits,
                                     std::uint16_t platform_modifier_state);
[[nodiscard]] bool AnyModifierKeyDown(
    std::uint16_t platform_modifier_state);

[[nodiscard]] std::optional<ModifiedClickBindingState>
ParseModifiedClickBinding(std::string_view binding,
                          bool omit_empty_from_serialization);

[[nodiscard]] std::string FormatModifiedClickBinding(
    const ModifiedClickBindingState& state);

[[nodiscard]] ModifiedClickInputState BuildModifiedClickInput(
    std::uint16_t platform_modifier_state,
    std::string_view mouse_button);

[[nodiscard]] std::optional<ModifiedClickAction> ModifiedClickActionAt(
    const BindingProfiles& profiles,
    int retail_index);

[[nodiscard]] std::optional<std::string> GetModifiedClickBinding(
    const BindingProfiles& profiles,
    std::string_view action,
    BindingProfileScope scope = BindingProfileScope::kActive);

[[nodiscard]] bool SetModifiedClickBinding(
    BindingProfiles& profiles,
    std::string_view action,
    std::string_view binding,
    BindingProfileScope scope = BindingProfileScope::kActive);

[[nodiscard]] bool IsModifiedClickActive(
    const BindingProfiles& profiles,
    const char* action,
    std::uint16_t platform_modifier_state,
    std::string_view mouse_button = {});

[[nodiscard]] bool IsModifiedClickActiveNow(
    const BindingProfiles& profiles,
    const char* action,
    std::string_view mouse_button = {});

}

#pragma once

#include "openwow/game/actions/bindings/application/binding_profiles.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::game::actions::bindings::adapters::lua {

struct BindingDisplayValue {
  std::string command;
  std::vector<std::string> keys;
};

[[nodiscard]] std::optional<BindingDisplayValue> ReadBindingValue(
    const BindingProfiles& profiles,
    int display_index,
    std::uint8_t slot);
[[nodiscard]] std::vector<std::string> ReadBindingKeys(
    const BindingProfiles& profiles,
    std::string_view command,
    BindingProfiles::BindingSlotSelector selector);
[[nodiscard]] std::string ReadBindingAction(
    const BindingProfiles& profiles,
    std::string_view key,
    bool check_override,
    BindingProfiles::BindingSlotSelector selector);
[[nodiscard]] std::string ReadBindingActionForScope(
    const BindingProfiles& profiles,
    std::string_view key,
    bool check_override,
    BindingProfileScope scope);

[[nodiscard]] bool AssignBindingValue(BindingProfiles& profiles,
                                      std::string_view key,
                                      std::string_view command,
                                      std::uint8_t slot = 0);
[[nodiscard]] bool AssignSpellBinding(BindingProfiles& profiles,
                                      std::string_view key,
                                      std::string_view spell_name,
                                      std::uint8_t slot);
[[nodiscard]] bool AssignItemBinding(BindingProfiles& profiles,
                                     std::string_view key,
                                     std::string_view item_name,
                                     std::uint8_t slot);
[[nodiscard]] bool AssignMacroBinding(BindingProfiles& profiles,
                                      std::string_view key,
                                      std::string_view macro_name,
                                      std::uint8_t slot);
[[nodiscard]] bool AssignClickBinding(BindingProfiles& profiles,
                                      std::string_view key,
                                      std::string_view button_name,
                                      std::string_view mouse_button,
                                      std::uint8_t slot);
void AssignOverrideBindingValue(
    BindingProfiles& profiles,
    const BindingProfiles::OverrideOwner& owner,
    bool priority,
    std::string_view key,
    std::string_view command);

}

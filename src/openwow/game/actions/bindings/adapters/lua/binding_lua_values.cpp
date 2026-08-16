#include "openwow/game/actions/bindings/adapters/lua/binding_lua_values.h"

#include "openwow/game/actions/bindings/adapters/persistence/retail_binding_text_codec.h"

#include <string>
#include <algorithm>
#include <cctype>

namespace openwow::game::actions::bindings::adapters::lua {
namespace {

std::string UppercaseAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](const unsigned char character) {
                   return static_cast<char>(std::toupper(character));
                 });
  return value;
}

}

std::optional<BindingDisplayValue> ReadBindingValue(
    const BindingProfiles& profiles,
    const int display_index,
    const std::uint8_t slot_value) {
  const auto slot = BindingSlot::FromValue(slot_value);
  if (!slot) {
    return std::nullopt;
  }
  const auto entry = profiles.BindingAt(
      display_index, BindingProfileScope::kActive, *slot);
  if (!entry) {
    return std::nullopt;
  }

  BindingDisplayValue value{entry->command.value(), {}};
  value.keys.reserve(entry->chords.size());
  for (const auto& chord : entry->chords) {
    value.keys.push_back(chord.value());
  }
  return value;
}

std::vector<std::string> ReadBindingKeys(
    const BindingProfiles& profiles,
    const std::string_view command,
    const BindingProfiles::BindingSlotSelector selector) {
  const auto chords = profiles.ChordsForCommandInActiveSlots(
      BindingCommand(std::string(command)), selector);
  std::vector<std::string> keys;
  keys.reserve(chords.size());
  for (const auto& chord : chords) {
    keys.push_back(chord.value());
  }
  return keys;
}

std::string ReadBindingAction(
    const BindingProfiles& profiles,
    const std::string_view key,
    const bool check_override,
    const BindingProfiles::BindingSlotSelector selector) {
  const auto command = profiles.ResolveBindingInActiveSlots(
      BindingChord(UppercaseAscii(std::string(key))), check_override, selector);
  return command ? command->value() : std::string{};
}

std::string ReadBindingActionForScope(
    const BindingProfiles& profiles,
    const std::string_view key,
    const bool check_override,
    const BindingProfileScope scope) {
  const auto command = profiles.ResolveBinding(
      BindingChord(UppercaseAscii(std::string(key))), check_override, scope);
  return command ? command->value() : std::string{};
}

bool AssignBindingValue(BindingProfiles& profiles,
                        const std::string_view key,
                        const std::string_view command,
                        const std::uint8_t slot_value) {
  const auto slot = BindingSlot::FromValue(slot_value);
  const std::string chord =
      persistence::NormalizeRetailBindingChord(key);
  if (!slot || !BindingProfiles::IsValidBindingKeyName(chord)) {
    return false;
  }
  return profiles.AssignBinding(
      BindingProfileScope::kActive, *slot, BindingChord(chord),
      BindingCommand(command.empty() ? "NONE" : std::string(command)));
}

bool AssignSpellBinding(BindingProfiles& profiles,
                        const std::string_view key,
                        const std::string_view spell_name,
                        const std::uint8_t slot) {
  return !key.empty() && !spell_name.empty() &&
         AssignBindingValue(profiles, key,
                            "SPELL " + std::string(spell_name), slot);
}

bool AssignItemBinding(BindingProfiles& profiles,
                       const std::string_view key,
                       const std::string_view item_name,
                       const std::uint8_t slot) {
  return !key.empty() && !item_name.empty() &&
         AssignBindingValue(profiles, key,
                            "ITEM " + std::string(item_name), slot);
}

bool AssignMacroBinding(BindingProfiles& profiles,
                        const std::string_view key,
                        const std::string_view macro_name,
                        const std::uint8_t slot) {
  return !key.empty() &&
         AssignBindingValue(profiles, key,
                            "MACRO " + std::string(macro_name), slot);
}

bool AssignClickBinding(BindingProfiles& profiles,
                        const std::string_view key,
                        const std::string_view button_name,
                        const std::string_view mouse_button,
                        const std::uint8_t slot) {
  if (key.empty() || button_name.empty()) {
    return false;
  }

  std::string command = "CLICK " + std::string(button_name);
  if (!mouse_button.empty()) {
    command += ':';
    command += mouse_button;
  }
  return AssignBindingValue(profiles, key, command, slot);
}

void AssignOverrideBindingValue(
    BindingProfiles& profiles,
    const BindingProfiles::OverrideOwner& owner,
    const bool priority,
    const std::string_view key,
    const std::string_view command) {
  std::string normalized_key = UppercaseAscii(std::string(key));
  std::optional<BindingCommand> typed_command;
  if (!command.empty()) {
    typed_command.emplace(std::string(command));
  }
  profiles.SetOverrideBinding(owner, priority, BindingChord(normalized_key),
                              std::move(typed_command));
}

}

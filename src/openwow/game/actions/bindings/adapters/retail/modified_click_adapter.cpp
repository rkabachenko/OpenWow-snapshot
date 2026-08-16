#include "openwow/game/actions/bindings/adapters/retail/modified_click_adapter.h"

#include "openwow/foundation/text/ascii.h"
#include "openwow/game/actions/bindings/application/binding_profiles.h"
#include "openwow/ui/widgets/simple_frame.h"

#include <SDL.h>

namespace openwow::game::actions::bindings::adapters::retail {
namespace {

[[nodiscard]] std::string ModifierPrefix(const std::uint8_t bits) {
  std::string prefix;
  const auto append_group = [&prefix, bits](
                                const std::uint8_t left,
                                const std::uint8_t right,
                                const std::string_view both_name,
                                const std::string_view left_name,
                                const std::string_view right_name) {
    if ((bits & (left | right)) == (left | right)) {
      prefix += both_name;
    } else if ((bits & left) != 0u) {
      prefix += left_name;
    } else if ((bits & right) != 0u) {
      prefix += right_name;
    }
  };
  append_group(0x10u, 0x20u, "ALT-", "LALT-", "RALT-");
  append_group(0x04u, 0x08u, "CTRL-", "LCTRL-", "RCTRL-");
  append_group(0x01u, 0x02u, "SHIFT-", "LSHIFT-", "RSHIFT-");
  return prefix;
}

[[nodiscard]] std::optional<std::uint8_t> ParseButtonIndex(
    std::string_view value) {
  if (value.size() != 7u ||
      !openwow::text::EqualsIgnoreCaseAscii(value.substr(0, 6), "BUTTON")) {
    return std::nullopt;
  }
  const char suffix = value.back();
  return suffix >= '0' && suffix <= '9'
             ? std::optional(static_cast<std::uint8_t>(suffix - '0'))
             : std::optional(std::uint8_t{0});
}

[[nodiscard]] std::optional<std::uint8_t> MouseButtonIndex(
    const std::string_view mouse_button) {
  if (mouse_button.empty()) {
    return std::nullopt;
  }
  if (const auto parsed = ParseButtonIndex(mouse_button)) {
    return parsed;
  }
  const std::string button_name(mouse_button);
  return static_cast<std::uint8_t>(
      openwow::ui::widgets::MouseButtonFlagToScriptOrdinal(
      openwow::ui::widgets::MouseButtonFlag(button_name.c_str())));
}

[[nodiscard]] std::uint8_t ModifierBitsFromPlatform(
    const std::uint16_t state) {
  std::uint8_t bits = 0;
  if ((state & KMOD_LSHIFT) != 0u) bits |= 0x01u;
  if ((state & KMOD_RSHIFT) != 0u) bits |= 0x02u;
  if ((state & KMOD_LCTRL) != 0u) bits |= 0x04u;
  if ((state & KMOD_RCTRL) != 0u) bits |= 0x08u;
  if ((state & KMOD_LALT) != 0u) bits |= 0x10u;
  if ((state & KMOD_RALT) != 0u) bits |= 0x20u;
  return bits;
}

}

std::uint8_t ParseModifierBits(
    const std::string_view binding,
    std::string_view& remainder) {
  std::uint8_t bits = 0;
  std::string_view cursor = binding;
  const auto consume = [&cursor](const std::string_view token) {
    if (cursor.size() < token.size() ||
        !openwow::text::EqualsIgnoreCaseAscii(
            cursor.substr(0, token.size()), token)) {
      return false;
    }
    cursor.remove_prefix(token.size());
    if (!cursor.empty() && cursor.front() == '-') cursor.remove_prefix(1);
    return true;
  };
  while (!cursor.empty()) {
    if (consume("LSHIFT")) bits |= 0x01u;
    else if (consume("RSHIFT")) bits |= 0x02u;
    else if (consume("SHIFT")) bits |= 0x03u;
    else if (consume("LCTRL")) bits |= 0x04u;
    else if (consume("RCTRL")) bits |= 0x08u;
    else if (consume("CTRL")) bits |= 0x0Cu;
    else if (consume("LALT")) bits |= 0x10u;
    else if (consume("RALT")) bits |= 0x20u;
    else if (consume("ALT")) bits |= 0x30u;
    else break;
  }
  remainder = cursor;
  return bits;
}

bool ModifierBitsMatch(
    const std::uint8_t bits,
    const std::uint16_t platform_modifier_state) {
  const auto group_matches =
      [platform_modifier_state](const std::uint8_t group,
                                const std::uint16_t left,
                                const std::uint16_t right) {
        if (group == 0u) return true;
        if (group == 1u) return (platform_modifier_state & left) != 0u;
        if (group == 2u) return (platform_modifier_state & right) != 0u;
        return (platform_modifier_state & (left | right)) != 0u;
      };
  return group_matches(bits & 0x03u, KMOD_LSHIFT, KMOD_RSHIFT) &&
         group_matches((bits >> 2) & 0x03u, KMOD_LCTRL, KMOD_RCTRL) &&
         group_matches((bits >> 4) & 0x03u, KMOD_LALT, KMOD_RALT);
}

bool AnyModifierKeyDown(const std::uint16_t platform_modifier_state) {
  return (platform_modifier_state &
          (KMOD_SHIFT | KMOD_CTRL | KMOD_ALT)) != 0u;
}

std::optional<ModifiedClickBindingState> ParseModifiedClickBinding(
    const std::string_view binding,
    const bool omit_empty_from_serialization) {
  ModifiedClickBindingState state;
  state.skip_serialization = omit_empty_from_serialization;
  if (binding.empty() ||
      openwow::text::EqualsIgnoreCaseAscii(binding, "NONE")) {
    return state;
  }

  std::string_view remainder = binding;
  state.modifier_bits = ParseModifierBits(binding, remainder);
  if (remainder.empty()) {
    state.skip_serialization = false;
    return state;
  }

  const auto button_index = ParseButtonIndex(remainder);
  if (!button_index) {
    return std::nullopt;
  }
  state.button_index = *button_index;
  state.has_button_token = true;
  state.skip_serialization = false;
  return state;
}

std::string FormatModifiedClickBinding(
    const ModifiedClickBindingState& state) {
  std::string value = ModifierPrefix(state.modifier_bits);
  if (state.has_button_token) {
    value += "BUTTON";
    value += std::to_string(state.button_index);
  } else if (!value.empty()) {
    value.pop_back();
  }
  return value.empty() ? std::string("NONE") : value;
}

ModifiedClickInputState BuildModifiedClickInput(
    const std::uint16_t platform_modifier_state,
    const std::string_view mouse_button) {
  return {
      .modifier_bits = ModifierBitsFromPlatform(platform_modifier_state),
      .button_index = MouseButtonIndex(mouse_button),
  };
}

std::optional<ModifiedClickAction> ModifiedClickActionAt(
    const BindingProfiles& profiles,
    const int retail_index) {
  if (retail_index < 1) {
    return std::nullopt;
  }
  return profiles.ModifiedClickActionAt(
      static_cast<std::size_t>(retail_index - 1));
}

std::optional<std::string> GetModifiedClickBinding(
    const BindingProfiles& profiles,
    const std::string_view action,
    const BindingProfileScope scope) {
  const auto state =
      profiles.ModifiedClickBinding(ModifiedClickAction(action), scope);
  return state ? std::optional(FormatModifiedClickBinding(*state))
               : std::nullopt;
}

bool SetModifiedClickBinding(
    BindingProfiles& profiles,
    const std::string_view action,
    const std::string_view binding,
    const BindingProfileScope scope) {
  const auto state = ParseModifiedClickBinding(
      binding, scope == BindingProfileScope::kDefault);
  return state &&
         profiles.AssignModifiedClick(ModifiedClickAction(action), *state,
                                      scope);
}

bool IsModifiedClickActive(
    const BindingProfiles& profiles,
    const char* action,
    const std::uint16_t platform_modifier_state,
    const std::string_view mouse_button) {
  std::string_view requested = action != nullptr ? action : "";
  std::string_view remainder = requested;
  const std::uint8_t explicit_bits =
      ParseModifierBits(requested, remainder);
  if (explicit_bits != 0u) {
    return ModifierBitsMatch(explicit_bits, platform_modifier_state);
  }

  return profiles.IsModifiedClickActive(
      action == nullptr
          ? std::nullopt
          : std::optional(ModifiedClickAction(requested)),
      BuildModifiedClickInput(platform_modifier_state, mouse_button));
}

bool IsModifiedClickActiveNow(
    const BindingProfiles& profiles,
    const char* action,
    const std::string_view mouse_button) {
  return IsModifiedClickActive(
      profiles, action, static_cast<std::uint16_t>(SDL_GetModState()),
      mouse_button);
}

}

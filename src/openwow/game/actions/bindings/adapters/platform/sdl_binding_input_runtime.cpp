#include "openwow/game/actions/bindings/adapters/platform/sdl_binding_input_runtime.h"

#include "openwow/foundation/text/ascii.h"
#include "openwow/game/actions/bindings/adapters/platform/sdl_binding_input.h"
#include "openwow/game/actions/bindings/adapters/retail/modified_click_adapter.h"
#include "openwow/game/actions/bindings/application/binding_profiles.h"

#include <SDL.h>

#include <bit>
#include <cmath>
#include <string>
#include <utility>

namespace openwow::game::actions::bindings::adapters::platform {
namespace {

[[nodiscard]] bool IsModifierKey(const std::string_view key) {
  return openwow::text::EqualsIgnoreCaseAscii(key, "LSHIFT") ||
         openwow::text::EqualsIgnoreCaseAscii(key, "RSHIFT") ||
         openwow::text::EqualsIgnoreCaseAscii(key, "LCTRL") ||
         openwow::text::EqualsIgnoreCaseAscii(key, "RCTRL") ||
         openwow::text::EqualsIgnoreCaseAscii(key, "LALT") ||
         openwow::text::EqualsIgnoreCaseAscii(key, "RALT");
}

[[nodiscard]] std::string ModifierPrefix(const std::uint16_t state) {
  const auto input =
      ::openwow::game::actions::bindings::adapters::retail::
          BuildModifiedClickInput(state, {});
  ModifiedClickBindingState binding;
  binding.modifier_bits = input.modifier_bits;
  const std::string value =
      ::openwow::game::actions::bindings::adapters::retail::
          FormatModifiedClickBinding(binding);
  return value == "NONE" ? std::string{} : value + "-";
}

[[nodiscard]] std::string BaseKey(const std::string_view chord) {
  std::string_view remainder = chord;
  (void)::openwow::game::actions::bindings::adapters::retail::ParseModifierBits(chord, remainder);
  return openwow::text::ToUpperAscii(std::string(remainder));
}

[[nodiscard]] std::string MouseButtonToken(const std::uint32_t flag) {
  switch (flag) {
    case 1u:
      return "BUTTON1";
    case 4u:
      return "BUTTON2";
    case 2u:
      return "BUTTON3";
    default:
      break;
  }
  if (flag == 0u || (flag & (flag - 1u)) != 0u) {
    return {};
  }
  const unsigned number = static_cast<unsigned>(std::countr_zero(flag)) + 1u;
  return number >= 4u && number <= 31u
             ? "BUTTON" + std::to_string(number)
             : std::string{};
}

[[nodiscard]] std::uint16_t WithoutMatchedModifiers(
    std::uint16_t state,
    const std::string_view chord) {
  std::string_view remainder = chord;
  const std::uint8_t bits = ::openwow::game::actions::bindings::adapters::retail::ParseModifierBits(chord, remainder);
  if ((bits & 0x03u) != 0u) state &= ~std::uint16_t(KMOD_SHIFT);
  if ((bits & 0x0Cu) != 0u) state &= ~std::uint16_t(KMOD_CTRL);
  if ((bits & 0x30u) != 0u) state &= ~std::uint16_t(KMOD_ALT);
  return state;
}

}

SdlBindingInputRuntime::SdlBindingInputRuntime(
    BindingProfiles& profiles,
    CommandSink command_sink,
    ModifierStateSink modifier_state_sink)
    : profiles_(profiles),
      command_sink_(std::move(command_sink)),
      modifier_state_sink_(std::move(modifier_state_sink)) {}

bool SdlBindingInputRuntime::DispatchCommand(
    const BindingCommand& command,
    const bool pressed,
    const std::string_view mouse_button,
    const std::optional<std::uint16_t> modifier_state,
    const std::uint32_t current_mouse_button_flag) {
  if (command_sink_) {
    return command_sink_(command, pressed, mouse_button, modifier_state,
                         current_mouse_button_flag);
  }
  if (profiles_.RunNamedBinding(
          command, pressed, pressed ? 1.0f : 0.0f, true, false, false,
          -1.0f, 0.0f, modifier_state)) {
    return true;
  }
  return profiles_.ExecuteUnhandledBindingCommand(command, pressed);
}

bool SdlBindingInputRuntime::KeyDown(const std::string_view key_name) {
  if (key_name.empty()) return false;
  if (IsModifierKey(key_name)) {
    if (modifier_state_sink_) {
      modifier_state_sink_(key_name, true);
    }
    return true;
  }
  const auto resolved =
      profiles_.ResolveChordWithFallbackDetailed(BindingChord(key_name));
  if (!resolved) return false;
  const auto state = static_cast<std::uint16_t>(SDL_GetModState());
  held_keys_.insert_or_assign(
      BindingKey(BaseKey(key_name)),
      HeldBinding{resolved->command, resolved->matched_chord, state});
  return DispatchCommand(
      resolved->command, true, "",
      WithoutMatchedModifiers(state, resolved->matched_chord.value()));
}

bool SdlBindingInputRuntime::KeyUp(const std::string_view key_name) {
  if (key_name.empty()) return false;
  if (IsModifierKey(key_name)) {
    if (modifier_state_sink_) {
      modifier_state_sink_(key_name, false);
    }
    return true;
  }
  const auto it = held_keys_.find(BindingKey(BaseKey(key_name)));
  if (it == held_keys_.end()) {
    const auto state = static_cast<std::uint16_t>(SDL_GetModState());
    const auto resolved = profiles_.ResolveChordWithFallbackDetailed(
        BindingChord(ModifierPrefix(state) + BaseKey(key_name)));
    return resolved &&
           DispatchCommand(
               resolved->command, false, "",
               WithoutMatchedModifiers(
                   state, resolved->matched_chord.value()));
  }
  const HeldBinding held = it->second;
  held_keys_.erase(it);
  const auto state = static_cast<std::uint16_t>(
      held.modifier_state | static_cast<std::uint16_t>(SDL_GetModState()));
  return DispatchCommand(
      held.command, false, "",
      WithoutMatchedModifiers(state, held.matched_chord.value()));
}

bool SdlBindingInputRuntime::MouseButtonDown(
    const std::uint32_t button_flag,
    const std::uint16_t modifier_state) {
  const std::string button = MouseButtonToken(button_flag);
  if (button.empty()) return false;
  const auto resolved = profiles_.ResolveChordWithFallbackDetailed(
      BindingChord(ModifierPrefix(modifier_state) + button));
  if (!resolved) return false;
  held_mouse_buttons_.insert_or_assign(
      BindingKey(std::to_string(button_flag)),
      HeldBinding{resolved->command, resolved->matched_chord, modifier_state});
  return DispatchCommand(
      resolved->command, true, "",
      WithoutMatchedModifiers(modifier_state, resolved->matched_chord.value()),
      button_flag);
}

bool SdlBindingInputRuntime::MouseButtonUp(
    const std::uint32_t button_flag,
    const std::uint16_t modifier_state) {
  if (button_flag == 0u) return false;
  const BindingKey key(std::to_string(button_flag));
  if (const auto it = held_mouse_buttons_.find(key);
      it != held_mouse_buttons_.end()) {
    const HeldBinding held = it->second;
    held_mouse_buttons_.erase(it);
    return DispatchCommand(
        held.command, false, "",
        WithoutMatchedModifiers(
            static_cast<std::uint16_t>(held.modifier_state | modifier_state),
            held.matched_chord.value()),
        button_flag);
  }
  const std::string button = MouseButtonToken(button_flag);
  if (button.empty()) return false;
  const auto resolved = profiles_.ResolveChordWithFallbackDetailed(
      BindingChord(ModifierPrefix(modifier_state) + button));
  return resolved &&
         DispatchCommand(
             resolved->command, false, "",
             WithoutMatchedModifiers(modifier_state,
                                     resolved->matched_chord.value()),
             button_flag);
}

bool SdlBindingInputRuntime::MouseWheel(const std::int32_t wheel_delta) {
  if (wheel_delta == 0) return false;
  const auto state = static_cast<std::uint16_t>(SDL_GetModState());
  const auto resolved = profiles_.ResolveChordWithFallbackDetailed(
      BindingChord(ModifierPrefix(state) +
                   (wheel_delta > 0 ? "MOUSEWHEELUP" : "MOUSEWHEELDOWN")));
  if (!resolved) return false;
  const auto filtered =
      WithoutMatchedModifiers(state, resolved->matched_chord.value());
  const bool down =
      DispatchCommand(resolved->command, true, "", filtered);
  const bool up =
      DispatchCommand(resolved->command, false, "", filtered);
  return down || up;
}

bool SdlBindingInputRuntime::DispatchJoystickAxis(
    const std::string_view chord,
    const bool pressed,
    const float pressure) {
  const BindingKey key(chord);
  if (!pressed) {
    const auto it = held_joystick_axes_.find(key);
    if (it == held_joystick_axes_.end()) return false;
    const BindingCommand command = it->second;
    held_joystick_axes_.erase(it);
    return profiles_.RunNamedBinding(command, false, pressure);
  }
  const auto command =
      profiles_.ResolveChordWithFallback(BindingChord(chord));
  if (!command) {
    held_joystick_axes_.erase(key);
    return false;
  }
  held_joystick_axes_.insert_or_assign(key, *command);
  return profiles_.RunNamedBinding(*command, true, pressure);
}

bool SdlBindingInputRuntime::JoystickAxisMotion(
    const std::uint32_t axis_index,
    const std::int32_t raw_value) {
  const std::string axis = std::to_string(axis_index + 1u);
  const std::string positive = "JOYAXIS" + axis + "POS";
  const std::string negative = "JOYAXIS" + axis + "NEG";
  const float normalized = NormalizeSdlJoystickAxis(raw_value);
  const float pressure = std::fabs(normalized);
  if (normalized > 0.0f) {
    return DispatchJoystickAxis(negative, false, 0.0f) |
           DispatchJoystickAxis(positive, true, pressure);
  }
  if (normalized < 0.0f) {
    return DispatchJoystickAxis(positive, false, 0.0f) |
           DispatchJoystickAxis(negative, true, pressure);
  }
  return DispatchJoystickAxis(positive, false, 0.0f) |
         DispatchJoystickAxis(negative, false, 0.0f);
}

void SdlBindingInputRuntime::Reset() {
  held_keys_.clear();
  held_mouse_buttons_.clear();
  held_joystick_axes_.clear();
}

}

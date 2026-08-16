#pragma once

#include "openwow/game/actions/bindings/model/binding_types.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace openwow::game {

class BindingProfiles;

namespace actions::bindings::adapters::platform {

class SdlBindingInputRuntime {
 public:
  using CommandSink = std::function<bool(
      const BindingCommand& command,
      bool pressed,
      std::string_view mouse_button,
      std::optional<std::uint16_t> modifier_state,
      std::uint32_t current_mouse_button_flag)>;
  using ModifierStateSink =
      std::function<void(std::string_view key, bool pressed)>;

  explicit SdlBindingInputRuntime(BindingProfiles& profiles,
                                  CommandSink command_sink = {},
                                  ModifierStateSink modifier_state_sink = {});

  [[nodiscard]] bool KeyDown(std::string_view key_name);
  [[nodiscard]] bool KeyUp(std::string_view key_name);
  [[nodiscard]] bool MouseButtonDown(std::uint32_t button_flag,
                                     std::uint16_t modifier_state);
  [[nodiscard]] bool MouseButtonUp(std::uint32_t button_flag,
                                   std::uint16_t modifier_state);
  [[nodiscard]] bool MouseWheel(std::int32_t wheel_delta);
  [[nodiscard]] bool JoystickAxisMotion(std::uint32_t axis_index,
                                        std::int32_t raw_value);
  void Reset();

 private:
  struct HeldBinding {
    BindingCommand command;
    BindingChord matched_chord;
    std::uint16_t modifier_state{0};
  };

  [[nodiscard]] bool DispatchJoystickAxis(std::string_view chord,
                                          bool pressed,
                                          float pressure);
  [[nodiscard]] bool DispatchCommand(
      const BindingCommand& command,
      bool pressed,
      std::string_view mouse_button,
      std::optional<std::uint16_t> modifier_state,
      std::uint32_t current_mouse_button_flag = 0u);

  BindingProfiles& profiles_;
  CommandSink command_sink_;
  ModifierStateSink modifier_state_sink_;
  std::unordered_map<BindingKey, HeldBinding> held_keys_;
  std::unordered_map<BindingKey, HeldBinding> held_mouse_buttons_;
  std::unordered_map<BindingKey, BindingCommand> held_joystick_axes_;
};

}
}

#pragma once

#include "openwow/game/actions/bindings/adapters/platform/sdl_binding_input.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace openwow::game::input {

struct AppliedJoystickConfigState {
  std::array<JoystickStickConfig, 4> sticks{};
  std::array<bool, 8> slider_axes{};
  std::array<JoystickHatConfig, 4> hats{};
  std::uint32_t mouse_stick_index = 0;
  int mouse_button_left = 0;
  int mouse_button_right = 1;
  float mouse_speed_scale = 1.5f;
  bool has_active_profile = false;
};

int InputControl_PitchDownStart();

int InputControl_MouselookStart();

void InputControl_StartupInitialize();

void ChatLog_Shutdown();

void ChatLog_Shutdown_Thunk();

void InputControl_SetOverrideBinding(void* inputCtrl, const char* key,
                                      const char* binding);

int InputControl_SetMouselookOverrideBinding(void* luaState);

void InputControl_ApplyMouselookBindings(void* inputCtrl);

[[nodiscard]] bool IsJoystickMouseConfigEnabled();
[[nodiscard]] const AppliedJoystickConfigState& GetAppliedJoystickConfigState();

void ResetJoystickConfigRuntimeForTests();

void SetJoystickMouseConfigEnabledForTests(bool enabled);

using JoystickConfigXmlTextProvider = std::optional<std::string>(*)();
using ActiveJoystickNameProvider = std::optional<std::string>(*)();
void SetJoystickConfigXmlTextProvider(JoystickConfigXmlTextProvider provider);
void SetActiveJoystickNameProvider(ActiveJoystickNameProvider provider);

}

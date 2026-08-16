#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::game {

struct JoystickStickConfig {
  std::uint32_t stick_index = 0;
  std::uint32_t axis_x = 0;
  std::uint32_t axis_y = 1;
};

struct JoystickHatConfig {
  std::uint32_t hat_index = 0;
  std::array<int, 4> buttons = {-1, -1, -1, -1};
};

struct JoystickMouseConfig {
  std::optional<bool> enabled;
  std::uint32_t stick_index = 0;
  int button_left = 0;
  int button_right = 1;
  float speed_scale = 1.5f;
};

struct JoystickConfigProfile {
  std::string name;
  std::string default_bindings;
  std::vector<JoystickStickConfig> sticks;
  std::vector<std::uint32_t> slider_axes;
  std::vector<JoystickHatConfig> hats;
  std::optional<JoystickMouseConfig> mouse;
};

namespace actions::bindings::adapters::platform {

[[nodiscard]] std::string SdlScancodeToBindingChord(
    int sdl_scancode, std::uint16_t modifier_state);
[[nodiscard]] std::string SdlScancodeToBaseKey(int sdl_scancode);
[[nodiscard]] std::string SdlKeyDownToBindingChord(
    int sdl_scancode, std::uint16_t modifier_state, bool is_repeat);
[[nodiscard]] float NormalizeSdlJoystickAxis(std::int32_t raw_value);
[[nodiscard]] std::optional<JoystickConfigProfile> ParseJoystickConfigProfile(
    std::string_view xml_text,
    std::string_view joystick_name);
[[nodiscard]] std::optional<std::string> QueryPrimaryJoystickName();

}
}

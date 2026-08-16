#include "openwow/game/actions/bindings/adapters/platform/sdl_binding_input.h"

#include "openwow/game/actions/bindings/adapters/retail/modified_click_adapter.h"

#include <SDL_keycode.h>
#include <SDL_scancode.h>

#include <optional>
#include <string>

namespace openwow::game::actions::bindings::adapters::platform {
namespace {

[[nodiscard]] std::optional<int> WowKeyCode(const int scan_code) {
  switch (scan_code) {
    case SDL_SCANCODE_LSHIFT: return 0;
    case SDL_SCANCODE_RSHIFT: return 1;
    case SDL_SCANCODE_LCTRL: return 2;
    case SDL_SCANCODE_RCTRL: return 3;
    case SDL_SCANCODE_LALT: return 4;
    case SDL_SCANCODE_RALT: return 5;
    case SDL_SCANCODE_SPACE: return 32;
    case SDL_SCANCODE_MINUS: return '-';
    case SDL_SCANCODE_EQUALS: return '=';
    case SDL_SCANCODE_LEFTBRACKET: return '[';
    case SDL_SCANCODE_RIGHTBRACKET: return ']';
    case SDL_SCANCODE_BACKSLASH: return '\\';
    case SDL_SCANCODE_SEMICOLON: return ';';
    case SDL_SCANCODE_APOSTROPHE: return '\'';
    case SDL_SCANCODE_GRAVE: return '`';
    case SDL_SCANCODE_COMMA: return ',';
    case SDL_SCANCODE_PERIOD: return '.';
    case SDL_SCANCODE_SLASH: return '/';
    case SDL_SCANCODE_ESCAPE: return 512;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER: return 513;
    case SDL_SCANCODE_BACKSPACE: return 514;
    case SDL_SCANCODE_TAB: return 515;
    case SDL_SCANCODE_LEFT: return 516;
    case SDL_SCANCODE_UP: return 517;
    case SDL_SCANCODE_RIGHT: return 518;
    case SDL_SCANCODE_DOWN: return 519;
    case SDL_SCANCODE_INSERT: return 520;
    case SDL_SCANCODE_DELETE: return 521;
    case SDL_SCANCODE_HOME: return 522;
    case SDL_SCANCODE_END: return 523;
    case SDL_SCANCODE_PAGEUP: return 524;
    case SDL_SCANCODE_PAGEDOWN: return 525;
    case SDL_SCANCODE_CAPSLOCK: return 526;
    case SDL_SCANCODE_NUMLOCKCLEAR: return 527;
    case SDL_SCANCODE_SCROLLLOCK: return 528;
    case SDL_SCANCODE_PAUSE: return 529;
    case SDL_SCANCODE_PRINTSCREEN: return 530;
    case SDL_SCANCODE_KP_0: return 256;
    case SDL_SCANCODE_KP_1: return 257;
    case SDL_SCANCODE_KP_2: return 258;
    case SDL_SCANCODE_KP_3: return 259;
    case SDL_SCANCODE_KP_4: return 260;
    case SDL_SCANCODE_KP_5: return 261;
    case SDL_SCANCODE_KP_6: return 262;
    case SDL_SCANCODE_KP_7: return 263;
    case SDL_SCANCODE_KP_8: return 264;
    case SDL_SCANCODE_KP_9: return 265;
    case SDL_SCANCODE_KP_PLUS: return 266;
    case SDL_SCANCODE_KP_MINUS: return 267;
    case SDL_SCANCODE_KP_MULTIPLY: return 268;
    case SDL_SCANCODE_KP_DIVIDE: return 269;
    case SDL_SCANCODE_KP_DECIMAL: return 270;
    case SDL_SCANCODE_KP_EQUALS:
    case SDL_SCANCODE_F13: return 780;
    case SDL_SCANCODE_F1: return 768;
    case SDL_SCANCODE_F2: return 769;
    case SDL_SCANCODE_F3: return 770;
    case SDL_SCANCODE_F4: return 771;
    case SDL_SCANCODE_F5: return 772;
    case SDL_SCANCODE_F6: return 773;
    case SDL_SCANCODE_F7: return 774;
    case SDL_SCANCODE_F8: return 775;
    case SDL_SCANCODE_F9: return 776;
    case SDL_SCANCODE_F10: return 777;
    case SDL_SCANCODE_F11: return 778;
    case SDL_SCANCODE_F12: return 779;
    case SDL_SCANCODE_F14: return 781;
    case SDL_SCANCODE_F15: return 782;
    case SDL_SCANCODE_F16: return 783;
    case SDL_SCANCODE_F17: return 784;
    case SDL_SCANCODE_F18: return 785;
    case SDL_SCANCODE_F19: return 786;
    default: break;
  }
  if (scan_code >= SDL_SCANCODE_A && scan_code <= SDL_SCANCODE_Z) {
    return 'A' + (scan_code - SDL_SCANCODE_A);
  }
  if (scan_code >= SDL_SCANCODE_1 && scan_code <= SDL_SCANCODE_9) {
    return '1' + (scan_code - SDL_SCANCODE_1);
  }
  return scan_code == SDL_SCANCODE_0
             ? std::optional<int>('0')
             : std::nullopt;
}

[[nodiscard]] std::optional<std::string> KeyName(const int key_code) {
  if (key_code >= 33 && key_code <= 255) {
    if (key_code < 0x80) return std::string(1, static_cast<char>(key_code));
    return std::string{
        static_cast<char>((key_code >> 6) | 0xC0),
        static_cast<char>((key_code & 0x3F) | 0x80)};
  }
  switch (key_code) {
    case -1: return "NONE";
    case 0: return "LSHIFT";
    case 1: return "RSHIFT";
    case 2: return "LCTRL";
    case 3: return "RCTRL";
    case 4: return "LALT";
    case 5: return "RALT";
    case 32: return "SPACE";
    case 266: return "NUMPADPLUS";
    case 267: return "NUMPADMINUS";
    case 268: return "NUMPADMULTIPLY";
    case 269: return "NUMPADDIVIDE";
    case 270: return "NUMPADDECIMAL";
    case 512: return "ESCAPE";
    case 513: return "ENTER";
    case 514: return "BACKSPACE";
    case 515: return "TAB";
    case 516: return "LEFT";
    case 517: return "UP";
    case 518: return "RIGHT";
    case 519: return "DOWN";
    case 520: return "INSERT";
    case 521: return "DELETE";
    case 522: return "HOME";
    case 523: return "END";
    case 524: return "PAGEUP";
    case 525: return "PAGEDOWN";
    case 526: return "CAPSLOCK";
    case 527: return "NUMLOCK";
    case 530: return "PRINTSCREEN";
    case 780: return "NUMPADEQUALS";
    default: break;
  }
  if (key_code >= 256 && key_code <= 265) {
    return "NUMPAD" + std::to_string(key_code - 256);
  }
  if (key_code >= 768 && key_code <= 779) {
    return "F" + std::to_string(key_code - 767);
  }
  if (key_code >= 781 && key_code <= 786) {
    return "F" + std::to_string(key_code - 767);
  }
  return std::nullopt;
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

[[nodiscard]] std::string Chord(const int key_code,
                                const std::uint16_t modifier_state) {
  const auto name = KeyName(key_code);
  if (!name) return {};
  return key_code >= 0 && key_code <= 5
             ? *name
             : ModifierPrefix(modifier_state) + *name;
}

}

std::string SdlScancodeToBindingChord(
    const int scan_code,
    const std::uint16_t modifier_state) {
  const auto key_code = WowKeyCode(scan_code);
  return key_code ? Chord(*key_code, modifier_state) : std::string{};
}

std::string SdlScancodeToBaseKey(const int scan_code) {
  const auto key_code = WowKeyCode(scan_code);
  if (!key_code) return "UNKNOWN";
  return KeyName(*key_code).value_or("UNKNOWN");
}

std::string SdlKeyDownToBindingChord(
    const int scan_code,
    const std::uint16_t modifier_state,
    const bool is_repeat) {
  const auto key_code = WowKeyCode(scan_code);
  if (!key_code || (is_repeat && !(*key_code >= 0 && *key_code <= 5))) {
    return {};
  }
  return Chord(*key_code, modifier_state);
}

}

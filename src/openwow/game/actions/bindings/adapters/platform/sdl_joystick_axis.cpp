#include "openwow/game/actions/bindings/adapters/platform/sdl_binding_input.h"

#include <algorithm>

namespace openwow::game::actions::bindings::adapters::platform {

float NormalizeSdlJoystickAxis(const std::int32_t raw_value) {
  if (raw_value >= 0) {
    return std::min(1.0f, static_cast<float>(raw_value) / 32767.0f);
  }
  return std::max(-1.0f, static_cast<float>(raw_value) / 32768.0f);
}

}

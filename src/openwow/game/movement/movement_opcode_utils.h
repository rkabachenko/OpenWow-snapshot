
#pragma once

#include <cstdint>

namespace openwow::game::movement {

[[nodiscard]] constexpr bool MovementOpcode_IsActiveMoverGated(
    std::uint16_t opcode) noexcept {
  switch (opcode) {
  case 202:
  case 203:
  case 217:
  case 728:
  case 729:
  case 838:
  case 1133:
  case 1179:
    return true;
  default:
    return false;
  }
}

}

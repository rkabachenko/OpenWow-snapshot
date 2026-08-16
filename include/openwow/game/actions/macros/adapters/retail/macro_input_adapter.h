#pragma once

#include "openwow/game/actions/macros/application/macro_execution_runtime.h"

#include <optional>

namespace openwow::game::actions::macros::adapters::retail {

[[nodiscard]] inline std::optional<MacroInputButton>
DecodeMacroInputButton(const char* value) {
  return value != nullptr
             ? std::optional(
                   MacroInputButton::FromCompatibilityText(value))
             : std::nullopt;
}

}

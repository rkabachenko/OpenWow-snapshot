#pragma once

#include "openwow/game/actions/held_cursor/held_cursor.h"
#include "openwow/game/actions/macros/model/macro_id.h"

#include <cstdint>

namespace openwow::game {
class MacroCatalog;
}

namespace openwow::game::actions::macros::ui {

enum class MacroCursorPickupResult : std::uint8_t {
  kPickedUp,
  kInvalidMacroId,
  kIconUnavailable,
};

[[nodiscard]] MacroCursorPickupResult PickupMacroCursor(
    MacroCatalog& macros, actions::held_cursor::HeldCursor& cursor,
    MacroId macro_id);

}

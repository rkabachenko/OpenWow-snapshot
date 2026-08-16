#include "openwow/game/actions/macros/adapters/ui/macro_cursor_controller.h"

#include "openwow/game/actions/macros/application/macro_catalog.h"

namespace openwow::game::actions::macros::ui {

MacroCursorPickupResult PickupMacroCursor(
    MacroCatalog& macros, actions::held_cursor::HeldCursor& cursor,
    const MacroId macro_id) {
  if (!macro_id.IsValid()) {
    return MacroCursorPickupResult::kInvalidMacroId;
  }

  const auto icon_path = macros.GetIconPath(macro_id);
  if (icon_path.empty()) {
    return MacroCursorPickupResult::kIconUnavailable;
  }

  cursor.Clear();
  cursor.HoldMacro(
      actions::held_cursor::Macro{.stable_id = macro_id.value()},
      actions::held_cursor::Presentation{
          .texture_path = icon_path,
          .sound = actions::held_cursor::Sound::CursorGrabObject,
          .grid = actions::held_cursor::Grid::ActionBar,
      });
  return MacroCursorPickupResult::kPickedUp;
}

}

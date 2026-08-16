#pragma once

#include "openwow/game/actions/held_cursor/held_cursor.h"

struct lua_State;

namespace openwow::ui::game::lua {

void BindHeldCursor(
    lua_State& state,
    openwow::game::actions::held_cursor::HeldCursor& held_cursor);
[[nodiscard]] openwow::game::actions::held_cursor::HeldCursor*
FindHeldCursor(lua_State& state);

}

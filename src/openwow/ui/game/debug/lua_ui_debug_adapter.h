#pragma once

#include "openwow/ui/game/debug/ui_debug_snapshot.h"

#include <optional>

struct lua_State;

namespace openwow::ui::game::debug {

[[nodiscard]] std::optional<UiDebugLuaState> ReadLuaUiDebugState(
    lua_State* lua, int registry_reference);

}

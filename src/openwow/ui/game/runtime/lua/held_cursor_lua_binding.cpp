#include "openwow/ui/game/runtime/lua/held_cursor_lua_binding.h"

#include "openwow/game/actions/held_cursor/held_cursor.h"

extern "C" {
#include <lua.h>
}

namespace openwow::ui::game::lua {
namespace {

constexpr const char* kHeldCursorRegistryKey = "openwow.held_cursor";

}

void BindHeldCursor(
    lua_State& state,
    openwow::game::actions::held_cursor::HeldCursor& held_cursor) {
  lua_pushlightuserdata(&state, &held_cursor);
  lua_setfield(&state, LUA_REGISTRYINDEX, kHeldCursorRegistryKey);
}

openwow::game::actions::held_cursor::HeldCursor* FindHeldCursor(
    lua_State& state) {
  lua_getfield(&state, LUA_REGISTRYINDEX, kHeldCursorRegistryKey);
  auto* cursor =
      static_cast<openwow::game::actions::held_cursor::HeldCursor*>(
          lua_touserdata(&state, -1));
  lua_pop(&state, 1);
  return cursor;
}

}

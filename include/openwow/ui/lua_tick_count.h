#pragma once

#include "openwow/runtime/time/game_clock.h"

extern "C" {
#include <lua.hpp>
}

namespace openwow::ui {

inline int LuaGetTickCountSeconds(lua_State* state) {
  lua_pushnumber(state,
                 static_cast<lua_Number>(openwow::core::GameClock::GetTickCount32()) / 1000.0);
  return 1;
}

}

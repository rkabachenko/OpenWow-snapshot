#pragma once

#include "openwow/ui/lua_c_api_convenience.h"

extern "C" {
#include <lua.hpp>
}

#include <cstddef>

namespace openwow::ui {

inline std::size_t LuaLegacyLength(lua_State *state, int index) {
  index = lua_absindex(state, index);

  switch (lua_type(state, index)) {
  case LUA_TNUMBER:
  case LUA_TSTRING: {
    std::size_t length = 0;
    (void)lua_tolstring(state, index, &length);
    return length;
  }
  case LUA_TTABLE:
  case LUA_TUSERDATA:
    return lua_rawlen(state, index);
  default:
    return 0;
  }
}

}

#pragma once

#include "openwow/game/localization.h"
#include "openwow/ui/lua_numeric.h"

#include <cstdint>
#include <string>

extern "C" {
#include <lua.hpp>
}

namespace openwow::ui {

inline int LuaGetLocalizedGlobalText(lua_State* state) {
  const char* token = lua_tostring(state, 1);
  if (token == nullptr) {
    return luaL_error(state, "Usage: GetText(\"token\" [,gender] [,ordinal])");
  }

  std::int32_t gender = 0;
  std::int32_t ordinal = -1;
  if (lua_isnumber(state, 2) != 0) {
    gender = TruncateLuaNumberToI32(lua_tonumber(state, 2));
  }
  if (lua_isnumber(state, 3) != 0) {
    ordinal = TruncateLuaNumberToI32(lua_tonumber(state, 3));
  }

  const std::string value = openwow::game::ResolveLocalizedGlobalString(
      state, token, ordinal, gender);
  lua_pushstring(state, value.c_str());
  return 1;
}

}

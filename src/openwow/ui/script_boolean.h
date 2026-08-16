#pragma once

#include "openwow/foundation/text/ascii.h"

#include <cmath>

extern "C" {
#include <lua.hpp>
}

namespace openwow::ui {

[[nodiscard]] inline bool ScriptParseBoolStringOrDefault(
    const char* value, const bool default_value) {
  if (value == nullptr) {
    return default_value;
  }

  switch (value[0]) {
    case '0':
    case 'F':
    case 'N':
    case 'f':
    case 'n':
      return false;
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
    case 'T':
    case 'Y':
    case 't':
    case 'y':
      return true;
    default:
      break;
  }

  if (openwow::text::EqualsIgnoreCaseAscii(value, "off") ||
      openwow::text::EqualsIgnoreCaseAscii(value, "disabled")) {
    return false;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(value, "on") ||
      openwow::text::EqualsIgnoreCaseAscii(value, "enabled")) {
    return true;
  }
  return default_value;
}

[[nodiscard]] inline bool ScriptReadBoolArgOrDefault(
    lua_State* state, const int index, const bool default_value) {
  switch (lua_type(state, index)) {
    case LUA_TNONE:
      return default_value;
    case LUA_TNIL:
      return false;
    case LUA_TBOOLEAN:
      return lua_toboolean(state, index) != 0;
    case LUA_TNUMBER: {

      const lua_Number value = lua_tonumber(state, index);
      return std::isnan(value) || value <= -1.0 || value >= 1.0;
    }
    case LUA_TSTRING:
      return ScriptParseBoolStringOrDefault(lua_tostring(state, index),
                                            default_value);
    default:
      return default_value;
  }
}

}

#pragma once

extern "C" {
#include <lua.hpp>
}

namespace openwow::ui {

[[nodiscard]] inline float ReadOptionalLuaNumberArgument(
    lua_State* lua, const int index, const float default_value) {
  return lua_gettop(lua) >= index && lua_isnoneornil(lua, index) == 0
             ? static_cast<float>(lua_tonumber(lua, index))
             : default_value;
}

}

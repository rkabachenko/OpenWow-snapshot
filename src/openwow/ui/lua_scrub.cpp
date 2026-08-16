extern "C" {
#include <lua.hpp>
}

#include "openwow/ui/lua_scrub.h"

namespace openwow::ui {

namespace {

bool LuaScrubPreservesType(const int value_type) {
  return value_type == LUA_TBOOLEAN || value_type == LUA_TNUMBER ||
         value_type == LUA_TSTRING;
}

}

int LuaScrub(lua_State* state) {
  const int argument_count = lua_gettop(state);
  for (int argument_index = 1; argument_index <= argument_count; ++argument_index) {
    if (LuaScrubPreservesType(lua_type(state, argument_index))) {
      continue;
    }

    lua_pushnil(state);
    lua_replace(state, argument_index);
  }

  return argument_count;
}

}

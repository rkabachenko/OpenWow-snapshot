#pragma once

extern "C" {
#include <lua.hpp>
}

#include <cstddef>
#include <limits>

namespace openwow::ui {

inline int ReturnExistingLuaTopWhen(const bool condition) noexcept {
  return condition ? 1 : 0;
}

inline int ReserveLuaResultCapacity(lua_State* state,
                                    const std::size_t result_count,
                                    const char* result_kind) {
  if (result_count > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
      lua_checkstack(state, static_cast<int>(result_count)) == 0) {
    return luaL_error(state, "too many %s", result_kind);
  }
  return static_cast<int>(result_count);
}

inline int ReserveLuaResultCapacity(lua_State* state,
                                    const std::size_t item_count,
                                    const std::size_t results_per_item,
                                    const char* result_kind) {
  if (results_per_item != 0 &&
      item_count > std::numeric_limits<std::size_t>::max() / results_per_item) {
    return luaL_error(state, "too many %s", result_kind);
  }
  return ReserveLuaResultCapacity(
      state, item_count * results_per_item, result_kind);
}

}

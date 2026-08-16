#pragma once

#include "openwow/ui/lua_taint_api.h"

#include <limits>

extern "C" {
#include <lua.hpp>
}

namespace openwow::ui {

using LuaProtectedCall = int (*)(lua_State* state, int nargs, int nresults, int errfunc);

inline int LuaPlainProtectedCall(lua_State* state,
                                 int nargs,
                                 int nresults,
                                 int errfunc) {
  return lua_pcall(state, nargs, nresults, errfunc);
}

inline constexpr char kGameLuaErrorHandlerRegistryKey[] = "openwow.error_handler";
inline constexpr char kGlueLuaErrorHandlerRegistryKey[] = "openwow.glue_error_handler";

template <LuaProtectedCall ProtectedCall>
int LuaCallOriginalThenHookClosure(lua_State* state) {
  const int argument_count = lua_gettop(state);

  lua_pushvalue(state, lua_upvalueindex(1));
  for (int index = 1; index <= argument_count; ++index) {
    lua_pushvalue(state, index);
  }

  const int original_status = ProtectedCall(state, argument_count, LUA_MULTRET, 0);
  if (original_status != 0) {
    return lua_error(state);
  }

  const int original_result_count = lua_gettop(state) - argument_count;
  const auto after_original = lua_get_execution_taint_state(state);
  auto hook_scope = after_original;
  if (hook_scope.tracking_depth !=
      std::numeric_limits<std::uint32_t>::max()) {
    ++hook_scope.tracking_depth;
  }
  lua_set_execution_taint_state(state, hook_scope);

  const auto* error_handler_key =
      static_cast<const char*>(lua_touserdata(state, lua_upvalueindex(3)));

  int error_handler_index = 0;
  if (error_handler_key != nullptr) {
    lua_getfield(state, LUA_REGISTRYINDEX, error_handler_key);
    if (lua_isfunction(state, -1) != 0) {
      error_handler_index = lua_gettop(state);
    } else {
      lua_pop(state, 1);
    }
  }

  lua_pushvalue(state, lua_upvalueindex(2));
  for (int index = 1; index <= argument_count; ++index) {
    lua_pushvalue(state, index);
  }

  const int hook_status = ProtectedCall(state, argument_count, 0, error_handler_index);
  lua_set_execution_taint_state(state, after_original);
  if (hook_status != 0) {
    lua_pop(state, 1);
  }
  if (error_handler_index != 0) {
    lua_remove(state, error_handler_index);
  }

  return original_result_count;
}

template <LuaProtectedCall ProtectedCall>
void PushLuaCallOriginalThenHookClosure(lua_State* state,
                                        const char* error_handler_registry_key) {
  lua_pushlightuserdata(
      state, const_cast<char*>(error_handler_registry_key));
  lua_pushcclosure(state, &LuaCallOriginalThenHookClosure<ProtectedCall>, 3);
}

}

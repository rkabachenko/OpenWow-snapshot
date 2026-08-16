#pragma once

#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/ui/runtime/lua/lua_binding.h"

#include <string>
#include <utility>

namespace openwow::ui {

inline void LogLuaCallError(lua_State* state, const char* context) {
  const char* error = lua_isstring(state, -1) != 0 ? lua_tostring(state, -1) : nullptr;
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                     std::string(context != nullptr ? context : "Lua call") + ": " +
                         (error != nullptr ? error : "unknown Lua error"));
}

template <typename... Args>
bool CallLuaGlobalIfFunction(lua_State* state, const char* function_name, Args&&... args) {
  if (state == nullptr || function_name == nullptr || function_name[0] == '\0') {
    return false;
  }

  lua::LuaStackRestore restore(state);
  lua_getglobal(state, function_name);
  if (lua_isfunction(state, -1) == 0 && lua_iscfunction(state, -1) == 0) {
    return false;
  }

  (lua::Push(state, std::forward<Args>(args)), ...);
  if (lua_pcall(state, static_cast<int>(sizeof...(Args)), 0, 0) != 0) {
    LogLuaCallError(state, function_name);
    return false;
  }

  return true;
}

template <typename... Args>
bool CallLuaGlobalMethodIfFunction(lua_State* state, const char* global_name,
                                   const char* method_name, Args&&... args) {
  if (state == nullptr || global_name == nullptr || global_name[0] == '\0' ||
      method_name == nullptr || method_name[0] == '\0') {
    return false;
  }

  lua::LuaStackRestore restore(state);
  lua_getglobal(state, global_name);
  if (lua_istable(state, -1) == 0 && lua_isuserdata(state, -1) == 0) {
    return false;
  }

  lua_getfield(state, -1, method_name);
  if (lua_isfunction(state, -1) == 0 && lua_iscfunction(state, -1) == 0) {
    return false;
  }

  lua_pushvalue(state, -2);
  (lua::Push(state, std::forward<Args>(args)), ...);
  if (lua_pcall(state, 1 + static_cast<int>(sizeof...(Args)), 0, 0) != 0) {
    LogLuaCallError(state, method_name);
    return false;
  }

  return true;
}

inline bool PushLuaGlobalCallResult(lua_State* state, const char* function_name,
                                    int result_count) {
  if (state == nullptr || function_name == nullptr || function_name[0] == '\0' ||
      result_count < 0) {
    return false;
  }

  lua::LuaStackRestore restore(state);
  lua_getglobal(state, function_name);
  if (lua_isfunction(state, -1) == 0 && lua_iscfunction(state, -1) == 0) {
    return false;
  }

  if (lua_pcall(state, 0, result_count, 0) != 0) {
    LogLuaCallError(state, function_name);
    return false;
  }
  restore.Dismiss();
  return true;
}

}

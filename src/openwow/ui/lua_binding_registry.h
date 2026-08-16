#pragma once

#include "openwow/ui/lua_c_api_convenience.h"
#include <stdexcept>
#include <string>

namespace openwow::ui {

enum class LuaGlobalScope {
  kUntracked,
  kGameWorld,
};

struct LuaGlobalBinding {
  const char* name;
  lua_CFunction handler;
};

struct LuaIntegerGlobal {
  const char* name;
  lua_Integer value;
};

struct LuaNumberGlobal {
  const char* name;
  lua_Number value;
};

struct LuaStringGlobal {
  const char* name;
  const char* value;
};

struct LuaBooleanGlobal {
  const char* name;
  bool value;
};

inline const char* LuaGlobalName(const char* name) {
  return name;
}

inline const char* LuaGlobalName(const LuaGlobalBinding& binding) {
  return binding.name;
}

inline const char* LuaGlobalName(const LuaIntegerGlobal& global) {
  return global.name;
}

inline const char* LuaGlobalName(const LuaNumberGlobal& global) {
  return global.name;
}

inline const char* LuaGlobalName(const LuaStringGlobal& global) {
  return global.name;
}

inline const char* LuaGlobalName(const LuaBooleanGlobal& global) {
  return global.name;
}

inline void ValidateLuaGlobalName(lua_State* state, const char* name, const char* operation) {
  if (state == nullptr) {
    throw std::invalid_argument(std::string(operation) + ": null lua_State");
  }
  if (name == nullptr || name[0] == '\0') {
    throw std::invalid_argument(std::string(operation) + ": empty global name");
  }
}

inline void ValidateLuaGlobalBinding(lua_State* state, const char* name, lua_CFunction handler) {
  ValidateLuaGlobalName(state, name, "RegisterLuaGlobal");
  if (handler == nullptr) {
    throw std::invalid_argument(std::string("RegisterLuaGlobal(") + name + "): null handler");
  }
}

inline void ValidateLuaGlobalReplacement(lua_State* state, const char* name,
                                         lua_CFunction handler) {
  ValidateLuaGlobalName(state, name, "ReplaceLuaGlobal");
  if (handler == nullptr) {
    throw std::invalid_argument(std::string("ReplaceLuaGlobal(") + name + "): null handler");
  }
}

inline const char* LuaGlobalScopeRegistryKey(const LuaGlobalScope scope) {
  switch (scope) {
    case LuaGlobalScope::kGameWorld:
      return "openwow.lua_globals.game_world";
    case LuaGlobalScope::kUntracked:
    default:
      return nullptr;
  }
}

inline void TrackLuaGlobal(lua_State* state, const char* name, const LuaGlobalScope scope) {
  const char* registry_key = LuaGlobalScopeRegistryKey(scope);
  if (registry_key == nullptr) {
    return;
  }
  ValidateLuaGlobalName(state, name, "TrackLuaGlobal");

  lua_getfield(state, LUA_REGISTRYINDEX, registry_key);
  if (lua_istable(state, -1) == 0) {
    lua_pop(state, 1);
    lua_newtable(state);
    lua_pushvalue(state, -1);
    lua_setfield(state, LUA_REGISTRYINDEX, registry_key);
  }
  const int table_index = lua_absindex(state, -1);

  lua_getfield(state, table_index, name);
  const bool already_tracked = lua_toboolean(state, -1) != 0;
  lua_pop(state, 1);
  if (!already_tracked) {
    const int next_index = static_cast<int>(lua_rawlen(state, table_index)) + 1;
    lua_pushstring(state, name);
    lua_rawseti(state, table_index, next_index);
    lua_pushboolean(state, 1);
    lua_setfield(state, table_index, name);
  }

  lua_pop(state, 1);
}

inline void UnregisterTrackedLuaGlobals(lua_State* state, const LuaGlobalScope scope) {
  const char* registry_key = LuaGlobalScopeRegistryKey(scope);
  if (state == nullptr || registry_key == nullptr) {
    return;
  }

  lua_getfield(state, LUA_REGISTRYINDEX, registry_key);
  if (lua_istable(state, -1) == 0) {
    lua_pop(state, 1);
    return;
  }
  const int table_index = lua_absindex(state, -1);
  const int count = static_cast<int>(lua_rawlen(state, table_index));
  for (int index = 1; index <= count; ++index) {
    lua_rawgeti(state, table_index, index);
    const char* name = lua_tostring(state, -1);
    if (name != nullptr && name[0] != '\0') {
      lua_pushnil(state);
      lua_setglobal(state, name);
    }
    lua_pop(state, 1);
  }
  lua_pop(state, 1);

  lua_pushnil(state);
  lua_setfield(state, LUA_REGISTRYINDEX, registry_key);
}

inline void RegisterLuaGlobal(lua_State* state, const char* name, lua_CFunction handler,
                              const LuaGlobalScope scope = LuaGlobalScope::kUntracked) {
  ValidateLuaGlobalBinding(state, name, handler);

  lua_pushcfunction(state, handler);
  lua_setglobal(state, name);
  TrackLuaGlobal(state, name, scope);
}

inline void ReplaceLuaGlobal(lua_State* state, const char* name, lua_CFunction handler,
                             const LuaGlobalScope scope = LuaGlobalScope::kUntracked) {
  ValidateLuaGlobalReplacement(state, name, handler);
  lua_pushcfunction(state, handler);
  lua_setglobal(state, name);
  TrackLuaGlobal(state, name, scope);
}

inline void ReplaceLuaGlobalValue(lua_State *state, const char *name, int value_index,
                                  const LuaGlobalScope scope = LuaGlobalScope::kUntracked) {
  ValidateLuaGlobalName(state, name, "ReplaceLuaGlobalValue");
  value_index = lua_absindex(state, value_index);
  lua_pushvalue(state, value_index);
  lua_setglobal(state, name);
  TrackLuaGlobal(state, name, scope);
}

inline bool PublishLuaGlobalValueIfNil(lua_State *state, const char *name, int value_index,
                                       const LuaGlobalScope scope = LuaGlobalScope::kUntracked) {
  ValidateLuaGlobalName(state, name, "PublishLuaGlobalValueIfNil");
  value_index = lua_absindex(state, value_index);
  lua_getglobal(state, name);
  const bool vacant = lua_isnil(state, -1) != 0;
  lua_pop(state, 1);
  if (!vacant) {
    return false;
  }
  ReplaceLuaGlobalValue(state, name, value_index, scope);
  return true;
}

inline void ReplaceLuaGlobalInteger(lua_State *state, const char *name, lua_Integer value,
                                    const LuaGlobalScope scope = LuaGlobalScope::kUntracked) {
  ValidateLuaGlobalName(state, name, "ReplaceLuaGlobalInteger");
  lua_pushinteger(state, value);
  lua_setglobal(state, name);
  TrackLuaGlobal(state, name, scope);
}

inline void ReplaceLuaGlobalNumber(lua_State *state, const char *name, lua_Number value,
                                   const LuaGlobalScope scope = LuaGlobalScope::kUntracked) {
  ValidateLuaGlobalName(state, name, "ReplaceLuaGlobalNumber");
  lua_pushnumber(state, value);
  lua_setglobal(state, name);
  TrackLuaGlobal(state, name, scope);
}

inline void ReplaceLuaGlobalString(lua_State* state, const char* name, const char* value,
                                   const LuaGlobalScope scope = LuaGlobalScope::kUntracked) {
  ValidateLuaGlobalName(state, name, "ReplaceLuaGlobalString");
  lua_pushstring(state, value != nullptr ? value : "");
  lua_setglobal(state, name);
  TrackLuaGlobal(state, name, scope);
}

inline void ReplaceLuaGlobalBoolean(lua_State* state, const char* name, bool value,
                                    const LuaGlobalScope scope = LuaGlobalScope::kUntracked) {
  ValidateLuaGlobalName(state, name, "ReplaceLuaGlobalBoolean");
  lua_pushboolean(state, value ? 1 : 0);
  lua_setglobal(state, name);
  TrackLuaGlobal(state, name, scope);
}

inline void UnregisterLuaGlobal(lua_State* state, const char* name) {
  ValidateLuaGlobalName(state, name, "UnregisterLuaGlobal");
  lua_getglobal(state, name);
  if (lua_isnil(state, -1) != 0) {
    lua_pop(state, 1);
    return;
  }
  lua_pop(state, 1);

  lua_pushnil(state);
  lua_setglobal(state, name);
}

template <typename Range>
inline void RegisterLuaGlobals(lua_State* state, const Range& bindings,
                               const LuaGlobalScope scope = LuaGlobalScope::kUntracked) {
  for (const auto& binding : bindings) {
    RegisterLuaGlobal(state, binding.name, binding.handler, scope);
  }
}

template <typename Range>
inline void UnregisterLuaGlobals(lua_State* state, const Range& bindings) {
  for (const auto& binding : bindings) {
    UnregisterLuaGlobal(state, LuaGlobalName(binding));
  }
}

template <typename Range>
inline void ReplaceLuaIntegerGlobals(lua_State* state, const Range& constants,
                                     const LuaGlobalScope scope = LuaGlobalScope::kUntracked) {
  for (const auto& constant : constants) {
    ReplaceLuaGlobalInteger(state, constant.name, constant.value, scope);
  }
}

template <typename Range>
inline void ReplaceLuaNumberGlobals(lua_State* state, const Range& constants,
                                    const LuaGlobalScope scope = LuaGlobalScope::kUntracked) {
  for (const auto& constant : constants) {
    ReplaceLuaGlobalNumber(state, constant.name, constant.value, scope);
  }
}

template <typename Range>
inline void ReplaceLuaStringGlobals(lua_State* state, const Range& constants,
                                    const LuaGlobalScope scope = LuaGlobalScope::kUntracked) {
  for (const auto& constant : constants) {
    ReplaceLuaGlobalString(state, constant.name, constant.value, scope);
  }
}

template <typename Range>
inline void ReplaceLuaBooleanGlobals(lua_State* state, const Range& constants,
                                     const LuaGlobalScope scope = LuaGlobalScope::kUntracked) {
  for (const auto& constant : constants) {
    ReplaceLuaGlobalBoolean(state, constant.name, constant.value, scope);
  }
}

}

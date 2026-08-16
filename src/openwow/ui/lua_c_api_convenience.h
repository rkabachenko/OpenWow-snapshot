#pragma once

extern "C" {
#include "../../../third_party/wow_lua/src/lua.h"
#include "../../../third_party/wow_lua/src/lauxlib.h"
}

#include <cassert>
#include <cstddef>
#include <cstdint>

static_assert(LUA_VERSION_NUM == 501, "OpenWow FrameScript must use the owned Lua 5.1 VM");

#ifndef LUA_OK
#define LUA_OK 0
#endif

#ifndef LUA_OPEQ
#define LUA_OPEQ 0
#define LUA_OPLT 1
#define LUA_OPLE 2
#endif

inline int lua_absindex(lua_State* L, int idx) {
  return (idx > 0 || idx <= LUA_REGISTRYINDEX) ? idx : lua_gettop(L) + idx + 1;
}

inline std::size_t lua_rawlen(lua_State* L, int idx) {
  return lua_objlen(L, idx);
}

inline lua_Integer luaL_len(lua_State* L, int idx) {
  return static_cast<lua_Integer>(lua_objlen(L, idx));
}

inline void* lua_newuserdatauv(lua_State* L, std::size_t size, int user_value_count) {
  assert(user_value_count == 0);
  (void)user_value_count;
  return lua_newuserdata(L, size);
}

inline void* luaL_testudata(lua_State* L, int idx, const char* type_name) {
  void* userdata = lua_touserdata(L, idx);
  if (userdata == nullptr || lua_getmetatable(L, idx) == 0) {
    return nullptr;
  }
  luaL_getmetatable(L, type_name);
  const int matches = lua_rawequal(L, -1, -2);
  lua_pop(L, 2);
  return matches != 0 ? userdata : nullptr;
}

inline void luaL_setfuncs(lua_State* L, const luaL_Reg* funcs, int upvalue_count) {
  assert(upvalue_count == 0);
  (void)upvalue_count;
  for (const luaL_Reg* entry = funcs; entry != nullptr && entry->name != nullptr; ++entry) {
    lua_pushcfunction(L, entry->func);
    lua_setfield(L, -2, entry->name);
  }
}

inline const char* luaL_tolstring(lua_State* L, int idx, std::size_t* len) {
  idx = lua_absindex(L, idx);
  if (luaL_callmeta(L, idx, "__tostring") != 0) {
    if (lua_isstring(L, -1) == 0) {
      luaL_error(L, "'__tostring' must return a string");
    }
  } else {
    switch (lua_type(L, idx)) {
      case LUA_TNIL:
        lua_pushliteral(L, "nil");
        break;
      case LUA_TBOOLEAN:
        lua_pushstring(L, lua_toboolean(L, idx) != 0 ? "true" : "false");
        break;
      case LUA_TNUMBER:
      case LUA_TSTRING:
        lua_pushvalue(L, idx);
        break;
      default:
        lua_pushfstring(L, "%s: %p", luaL_typename(L, idx), lua_topointer(L, idx));
        break;
    }
  }
  return lua_tolstring(L, -1, len);
}

inline void lua_geti(lua_State* L, int idx, lua_Integer n) {
  lua_rawgeti(L, idx, static_cast<int>(n));
}

inline void lua_seti(lua_State* L, int idx, lua_Integer n) {
  lua_rawseti(L, idx, static_cast<int>(n));
}

inline int lua_isinteger(lua_State* L, int idx) {
  if (lua_isnumber(L, idx) == 0) {
    return 0;
  }
  const lua_Number number = lua_tonumber(L, idx);
  const lua_Integer integer = lua_tointeger(L, idx);
  return static_cast<lua_Number>(integer) == number;
}

inline int lua_compare(lua_State* L, int idx1, int idx2, int op) {
  switch (op) {
    case LUA_OPEQ:
      return lua_equal(L, idx1, idx2);
    case LUA_OPLT:
      return lua_lessthan(L, idx1, idx2);
    case LUA_OPLE:
      return lua_lessthan(L, idx1, idx2) || lua_equal(L, idx1, idx2);
    default:
      return 0;
  }
}

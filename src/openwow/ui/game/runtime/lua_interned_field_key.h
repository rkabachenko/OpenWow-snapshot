#pragma once

#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/lua_taint_api.h"

#include <cstddef>
#include <string_view>

namespace openwow::ui::game::runtime {

[[nodiscard]] inline int PushInternedLuaFieldKeyBlock(lua_State* const lua,
                                                      const char* const* names,
                                                      const int count) {
  lua_pushlightuserdata(lua, const_cast<void*>(static_cast<const void*>(names)));
  lua_rawget(lua, LUA_REGISTRYINDEX);
  if (lua_type(lua, -1) == LUA_TTABLE) {
    return lua_gettop(lua);
  }

  lua_pop(lua, 1);
  const openwow::ui::ScopedNeutralLuaExecutionTaint neutral_taint(lua);
  lua_createtable(lua, count, 0);
  const int keys = lua_gettop(lua);
  for (int index = 0; index < count; ++index) {
    lua_pushstring(lua, names[index]);
    lua_rawseti(lua, keys, index + 1);
  }
  lua_pushlightuserdata(lua, const_cast<void*>(static_cast<const void*>(names)));
  lua_pushvalue(lua, keys);
  lua_rawset(lua, LUA_REGISTRYINDEX);
  return keys;
}

inline void RawGetInternedLuaBlockField(lua_State* const lua,
                                        const int table_index,
                                        const int keys_index,
                                        const int key_ordinal) {
  const int table = lua_absindex(lua, table_index);
  const openwow::ui::ScopedNeutralLuaExecutionTaint neutral_native(lua);
  lua_rawgeti(lua, keys_index, key_ordinal);
  lua_rawget(lua, table);
}

inline void GetInternedLuaBlockField(lua_State* const lua,
                                     const int table_index,
                                     const int keys_index,
                                     const int key_ordinal) {
  const int table = lua_absindex(lua, table_index);
  const openwow::ui::ScopedNeutralLuaExecutionTaint neutral_native(lua);
  lua_rawgeti(lua, keys_index, key_ordinal);
  lua_gettable(lua, table);
}

struct InternedLuaFieldKeys {
  int index;
};

template <std::size_t Count>
[[nodiscard]] inline InternedLuaFieldKeys PushInternedLuaFieldKeys(
    lua_State* const lua, const char* const (&names)[Count]) {
  return InternedLuaFieldKeys{
      PushInternedLuaFieldKeyBlock(lua, names, static_cast<int>(Count))};
}

template <std::size_t Count>
consteval int InternedLuaFieldKeyOrdinal(const char* const (&names)[Count],
                                         const std::string_view name) {
  for (std::size_t index = 0; index < Count; ++index) {
    if (name == std::string_view(names[index])) {
      return static_cast<int>(index) + 1;
    }
  }
  throw "InternedLuaFieldKeyOrdinal: field name is not in this key block";
}

inline void PushInternedLuaFieldKey(lua_State* const lua,
                                    const char* const field_name) {
  lua_pushlightuserdata(lua, const_cast<char*>(field_name));
  lua_rawget(lua, LUA_REGISTRYINDEX);
  if (lua_type(lua, -1) == LUA_TSTRING) {
    return;
  }

  lua_pop(lua, 1);
  const openwow::ui::ScopedNeutralLuaExecutionTaint neutral_taint(lua);
  lua_pushlightuserdata(lua, const_cast<char*>(field_name));
  lua_pushstring(lua, field_name);
  lua_pushvalue(lua, -1);
  lua_insert(lua, -3);
  lua_rawset(lua, LUA_REGISTRYINDEX);
}

inline void PushInternedLuaString(lua_State* const lua,
                                  const char* const text) {
  PushInternedLuaFieldKey(lua, text);
}

inline void GetInternedLuaField(lua_State* const lua, const int table_index,
                                const char* const field_name) {
  const int table = lua_absindex(lua, table_index);
  const openwow::ui::ScopedNeutralLuaExecutionTaint neutral_native(lua);
  PushInternedLuaFieldKey(lua, field_name);
  lua_gettable(lua, table);
}

inline void SetInternedLuaField(lua_State* const lua, const int table_index,
                                const char* const field_name) {
  const int table = lua_absindex(lua, table_index);
  const openwow::ui::ScopedNeutralLuaExecutionTaint neutral_native(lua);
  PushInternedLuaFieldKey(lua, field_name);
  lua_insert(lua, -2);
  lua_settable(lua, table);
}

inline void RawGetInternedLuaField(lua_State* const lua, const int table_index,
                                   const char* const field_name) {
  const int table = lua_absindex(lua, table_index);
  const openwow::ui::ScopedNeutralLuaExecutionTaint neutral_native(lua);
  PushInternedLuaFieldKey(lua, field_name);
  lua_rawget(lua, table);
}

[[nodiscard]] inline const char* BorrowInternedLuaStringField(
    lua_State* const lua, int table_index, const char* const field_name) {
  if (lua == nullptr || lua_istable(lua, table_index) == 0) {
    return nullptr;
  }
  table_index = lua_absindex(lua, table_index);
  RawGetInternedLuaField(lua, table_index, field_name);
  const char* const value =
      lua_type(lua, -1) == LUA_TSTRING ? lua_tostring(lua, -1) : nullptr;
  lua_pop(lua, 1);
  return value;
}

}

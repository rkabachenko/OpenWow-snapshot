#pragma once

#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/lua_taint_api.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <string>
#include <string_view>

extern "C" {
#include <lua.hpp>
}

namespace openwow::ui {

inline constexpr const char* kLuaFrameAttributeRegistryKey =
    "openwow.frame_attribute_store";
inline constexpr const char* kLuaFrameAttributeValueField = "value";
inline constexpr const char* kLuaFrameAttributeNilField = "is_nil";

inline void PushLuaFrameAttributeRegistry(lua_State* state) {
  const openwow::ui::ScopedNeutralLuaExecutionTaint neutral_taint(state);
  lua_getfield(state, LUA_REGISTRYINDEX, kLuaFrameAttributeRegistryKey);
  if (lua_istable(state, -1)) {
    return;
  }

  lua_pop(state, 1);
  lua_newtable(state);
  const int registry_index = lua_absindex(state, -1);
  lua_newtable(state);
  lua_pushliteral(state, "k");
  lua_setfield(state, -2, "__mode");
  lua_setmetatable(state, registry_index);
  lua_pushvalue(state, registry_index);
  lua_setfield(state, LUA_REGISTRYINDEX, kLuaFrameAttributeRegistryKey);
}

inline int EnsureLuaFrameAttributeStore(lua_State* state, int frame_index) {
  const openwow::ui::ScopedNeutralLuaExecutionTaint neutral_taint(state);
  frame_index = lua_absindex(state, frame_index);

  PushLuaFrameAttributeRegistry(state);
  const int registry_index = lua_absindex(state, -1);
  lua_pushvalue(state, frame_index);
  lua_rawget(state, registry_index);
  if (lua_istable(state, -1)) {
    lua_remove(state, registry_index);
    return lua_absindex(state, -1);
  }

  lua_pop(state, 1);
  lua_newtable(state);
  lua_pushvalue(state, frame_index);
  lua_pushvalue(state, -2);
  lua_rawset(state, registry_index);
  lua_remove(state, registry_index);
  return lua_absindex(state, -1);
}

inline bool PushLuaFrameAttributeStore(lua_State* state, int frame_index) {
  const openwow::ui::ScopedNeutralLuaExecutionTaint neutral_taint(state);
  frame_index = lua_absindex(state, frame_index);

  PushLuaFrameAttributeRegistry(state);
  const int registry_index = lua_absindex(state, -1);
  lua_pushvalue(state, frame_index);
  lua_rawget(state, registry_index);
  lua_remove(state, registry_index);
  return lua_istable(state, -1) != 0;
}

inline std::string NormalizeFrameAttributeKey(const char* raw_key) {
  std::string normalized = raw_key != nullptr ? raw_key : "";
  for (char& ch : normalized) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return normalized;
}

inline std::string BuildTruncatedFrameAttributeKey(
    std::initializer_list<std::string_view> parts) {
  std::string key;
  key.reserve(255);
  std::size_t remaining = 255;
  for (const auto part : parts) {
    if (remaining == 0) {
      break;
    }
    const auto to_copy = std::min<std::size_t>(remaining, part.size());
    key.append(part.data(), to_copy);
    remaining -= to_copy;
  }
  return key;
}

inline void StoreLuaFrameAttributeValue(lua_State* state,
                                        int frame_index,
                                        std::string_view normalized_key,
                                        int value_index) {

  const openwow::ui::ScopedNeutralLuaExecutionTaint neutral_taint(state);
  frame_index = lua_absindex(state, frame_index);
  value_index = lua_absindex(state, value_index);

  const int store_index = EnsureLuaFrameAttributeStore(state, frame_index);

  lua_newtable(state);
  if (lua_isnil(state, value_index)) {
    lua_pushboolean(state, 1);
    lua_setfield(state, -2, kLuaFrameAttributeNilField);
  } else {
    lua_pushvalue(state, value_index);
    openwow::ui::lua_set_taint(state, -1, 0);
    lua_setfield(state, -2, kLuaFrameAttributeValueField);
  }

  const std::string key(normalized_key);
  lua_setfield(state, store_index, key.c_str());
  lua_pop(state, 1);
}

inline bool PushLuaFrameAttributeValue(lua_State* state,
                                       int frame_index,
                                       std::string_view normalized_key) {

  const auto reader_taint =
      openwow::ui::lua_get_execution_taint_state(state).source;
  const openwow::ui::ScopedNeutralLuaExecutionTaint neutral_taint(state);
  frame_index = lua_absindex(state, frame_index);

  if (!PushLuaFrameAttributeStore(state, frame_index)) {
    lua_pop(state, 1);
    return false;
  }

  const std::string key(normalized_key);
  lua_getfield(state, -1, key.c_str());
  lua_remove(state, -2);
  if (!lua_istable(state, -1)) {
    lua_pop(state, 1);
    return false;
  }

  lua_getfield(state, -1, kLuaFrameAttributeNilField);
  const bool is_nil = lua_toboolean(state, -1) != 0;
  lua_pop(state, 1);
  if (is_nil) {
    lua_pop(state, 1);
    lua_pushnil(state);
    return true;
  }

  lua_getfield(state, -1, kLuaFrameAttributeValueField);
  lua_remove(state, -2);
  if (openwow::ui::lua_get_taint(state, -1) == 0) {
    openwow::ui::lua_set_taint(state, -1, reader_taint);
  }
  return true;
}

}

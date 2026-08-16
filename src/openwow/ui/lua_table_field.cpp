#include "openwow/ui/lua_table_field.h"

#include "openwow/ui/lua_c_api_convenience.h"

#include <lua.hpp>

namespace openwow::ui {

const char* BorrowRawLuaStringField(lua_State* lua, int table_index,
                                    const std::string_view field_name) {
  if (lua == nullptr || lua_istable(lua, table_index) == 0) {
    return nullptr;
  }

  table_index = lua_absindex(lua, table_index);
  lua_pushlstring(lua, field_name.data(), field_name.size());
  lua_rawget(lua, table_index);
  const char* value =
      lua_type(lua, -1) == LUA_TSTRING ? lua_tostring(lua, -1) : nullptr;
  lua_pop(lua, 1);
  return value;
}

bool ReadLuaBooleanFieldOrDefault(lua_State* lua, int table_index,
                                  const std::string_view field_name,
                                  const bool default_value) {
  if (lua == nullptr || lua_istable(lua, table_index) == 0) {
    return default_value;
  }
  table_index = lua_absindex(lua, table_index);
  lua_pushlstring(lua, field_name.data(), field_name.size());
  lua_gettable(lua, table_index);
  const bool value = lua_isboolean(lua, -1) != 0
                         ? lua_toboolean(lua, -1) != 0
                         : default_value;
  lua_pop(lua, 1);
  return value;
}

double ReadLuaNumberFieldOrDefault(lua_State* lua, int table_index,
                                   const std::string_view field_name,
                                   const double default_value) {
  if (lua == nullptr || lua_istable(lua, table_index) == 0) {
    return default_value;
  }
  table_index = lua_absindex(lua, table_index);
  lua_pushlstring(lua, field_name.data(), field_name.size());
  lua_gettable(lua, table_index);
  const double value =
      lua_isnumber(lua, -1) != 0 ? lua_tonumber(lua, -1) : default_value;
  lua_pop(lua, 1);
  return value;
}

void CopyLuaTableField(lua_State* lua, int target_index, int source_index,
                       const std::string_view field_name) {
  if (lua == nullptr || lua_istable(lua, target_index) == 0 ||
      lua_istable(lua, source_index) == 0) {
    return;
  }
  target_index = lua_absindex(lua, target_index);
  source_index = lua_absindex(lua, source_index);
  lua_pushlstring(lua, field_name.data(), field_name.size());
  lua_gettable(lua, source_index);
  lua_pushlstring(lua, field_name.data(), field_name.size());
  lua_insert(lua, -2);
  lua_settable(lua, target_index);
}

std::optional<std::string> ReadLuaStringField(
    lua_State* lua, int table_index, const std::string_view field_name) {
  if (lua == nullptr || lua_istable(lua, table_index) == 0) {
    return std::nullopt;
  }

  table_index = lua_absindex(lua, table_index);
  lua_pushlstring(lua, field_name.data(), field_name.size());
  lua_gettable(lua, table_index);
  std::optional<std::string> value;
  if (lua_isstring(lua, -1) != 0) {
    if (const char* text = lua_tostring(lua, -1); text != nullptr) {
      value = text;
    }
  }
  lua_pop(lua, 1);
  return value;
}

void WriteLuaNumberField(lua_State* lua, int table_index,
                         const std::string_view field_name,
                         const double value) {
  if (lua == nullptr || lua_istable(lua, table_index) == 0) {
    return;
  }
  table_index = lua_absindex(lua, table_index);
  lua_pushlstring(lua, field_name.data(), field_name.size());
  lua_pushnumber(lua, value);
  lua_settable(lua, table_index);
}

}

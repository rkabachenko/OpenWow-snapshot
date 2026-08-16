#pragma once

#include <optional>
#include <string>
#include <string_view>

struct lua_State;

namespace openwow::ui {

[[nodiscard]] const char* BorrowRawLuaStringField(
    lua_State* lua, int table_index, std::string_view field_name);
[[nodiscard]] bool ReadLuaBooleanFieldOrDefault(
    lua_State* lua, int table_index, std::string_view field_name,
    bool default_value);
[[nodiscard]] double ReadLuaNumberFieldOrDefault(
    lua_State* lua, int table_index, std::string_view field_name,
    double default_value);
void CopyLuaTableField(lua_State* lua, int target_index, int source_index,
                       std::string_view field_name);

[[nodiscard]] std::optional<std::string> ReadLuaStringField(
    lua_State* lua, int table_index, std::string_view field_name);
void WriteLuaNumberField(lua_State* lua, int table_index,
                         std::string_view field_name, double value);

}

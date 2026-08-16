#pragma once

struct lua_State;
namespace openwow::ui::game::frame_api {
void ApplyFontStringStateMethods(lua_State* lua, int table_index);
int PushTableJustify(lua_State* lua, const char* field_name, const char* default_value, bool horizontal);
void SetFontStringWrapState(lua_State* lua, int self_index, const char* field_name, bool enabled);
bool GetFontStringWrapState(lua_State* lua, int self_index, const char* field_name, bool default_value);
int SetFontStringIndentedWordWrap(lua_State* lua);
int GetFontStringIndentedWordWrap(lua_State* lua);
}

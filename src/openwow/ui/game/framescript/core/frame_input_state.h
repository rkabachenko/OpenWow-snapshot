#pragma once

struct lua_State;

namespace openwow::ui::game::frame_api {

void NotifyFrameInputMutation(lua_State* lua, int self_index, bool reindex_only);
[[nodiscard]] bool IsLuaTableEffectivelyVisible(lua_State* lua,
                                                int table_index);
bool GetLuaBooleanField(lua_State* lua, int table_index, const char* field);
int SetValidatedFrameShownState(lua_State* lua, bool shown);
int PushValidatedFrameShownState(lua_State* lua);
int SetFrameInputCategoryEnabled(lua_State* lua, const char* field_name);
int PushFrameInputCategoryEnabled(lua_State* lua, const char* field_name,
                                  const char* legacy_field_name = nullptr);

}

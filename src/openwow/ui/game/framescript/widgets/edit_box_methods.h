#pragma once

struct lua_State;

namespace openwow::ui::game::frame_api {

void ApplyEditBoxMethods(lua_State* lua);
void ApplyEditBoxStateMethods(lua_State* lua);
void InitializeEditBoxInstanceDefaults(lua_State* lua, int edit_box_index);
void SyncEditBoxInternalDisplayText(lua_State* lua, int edit_box_index);
void InvokeEditBoxScript(lua_State* lua, int edit_box_index,
                         const char* handler, int argument_count);

}

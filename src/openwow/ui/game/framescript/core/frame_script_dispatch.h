#pragma once

struct lua_State;

namespace openwow::ui::game::frame_api {

void FireScript(lua_State* lua, int frame_index, const char* handler,
                int extra_args = 0);
void FirePendingTooltipMoneyScript(lua_State* lua, int tooltip_index);
void ApplyFrameScriptHandlerMethods(lua_State* lua, int table_index,
                                    bool include_hook_script);
void RefreshLuaFrameOnUpdateRegistration(lua_State* lua, int frame_index);
int LuaFrame_HasScript(lua_State* lua);

}

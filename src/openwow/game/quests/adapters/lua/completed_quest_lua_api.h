#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaQueryQuestsCompleted(lua_State* state);
int LuaGetQuestsCompleted(lua_State* state);

}

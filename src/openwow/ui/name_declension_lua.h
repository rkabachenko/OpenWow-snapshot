#pragma once

struct lua_State;

namespace openwow::ui {

[[nodiscard]] int ReadLuaDeclensionGenderIndex(lua_State* state, int index);

int LuaDeclineName(lua_State* state);

}

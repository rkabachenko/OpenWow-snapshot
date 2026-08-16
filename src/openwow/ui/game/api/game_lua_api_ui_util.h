
#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaRestartGx(lua_State* L);
int LuaGetExistingLocales(lua_State* L);
}

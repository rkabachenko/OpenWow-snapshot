#pragma once

struct lua_State;

namespace openwow::ui::game {

void RegisterStandardWowGlobals(lua_State* L);

}

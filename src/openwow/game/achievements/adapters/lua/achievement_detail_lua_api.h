#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaGetAchievementInfo(lua_State* state);
int LuaGetAchievementInfoFromCriteria(lua_State* state);

}

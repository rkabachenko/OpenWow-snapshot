#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaGetAchievementNumCriteria(lua_State* state);
int LuaGetAchievementCriteriaInfo(lua_State* state);

}

#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaGetAchievementNumRewards(lua_State* state);
int LuaGetAchievementReward(lua_State* state);

}

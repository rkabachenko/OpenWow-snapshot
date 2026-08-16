#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaCanShowAchievementUI(lua_State* state);
int LuaHasCompletedAnyAchievement(lua_State* state);

}

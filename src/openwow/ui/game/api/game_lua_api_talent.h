
#pragma once

#include <string>

struct lua_State;

namespace openwow::ui::game {

bool PreviewTalentsCVarValidationCallback(const std::string& name,
                                          const std::string& old_value,
                                          const std::string& new_value);

}

namespace openwow::ui::game::detail {

int LuaGetNumTalentTabs(lua_State* L);
int LuaGetTalentTabInfo(lua_State* L);
int LuaGetNumTalents(lua_State* L);
int LuaGetTalentInfo(lua_State* L);
int LuaGetTalentPrereqs(lua_State* L);
int LuaLearnTalent(lua_State* L);
int LuaGetActiveTalentGroup(lua_State* L);
int LuaSetActiveTalentGroup(lua_State* L);
int LuaGetNumTalentGroups(lua_State* L);
int LuaGetUnspentTalentPoints(lua_State* L);
int LuaConfirmTalentWipe(lua_State* L);
int LuaAddPreviewTalentPoints(lua_State* L);
int LuaGetPreviewTalentPointsSpent(lua_State* L);
int LuaResetPreviewTalentPoints(lua_State* L);
int LuaResetGroupPreviewTalentPoints(lua_State* L);
int LuaGetGroupPreviewTalentPointsSpent(lua_State* L);
int LuaGetTalentLink(lua_State* L);
int LuaLearnPreviewTalents(lua_State* L);

}

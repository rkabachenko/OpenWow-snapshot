
#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaGetSkillLineInfo(lua_State* L);
int LuaGetNumSkillLines(lua_State* L);
int LuaExpandSkillHeader(lua_State* L);
int LuaCollapseSkillHeader(lua_State* L);
int LuaAbandonSkill(lua_State* L);
int LuaAddSkillUp(lua_State* L);
int LuaRemoveSkillUp(lua_State* L);
int LuaBuySkillTier(lua_State* L);
int LuaAcceptSkillUps(lua_State* L);
int LuaCancelSkillUps(lua_State* L);
int LuaGetAdjustedSkillPoints(lua_State* L);

int GetSelectedProfessionSkillIndex();
void SetSelectedProfessionSkillIndex(int lua_index);
void ResetProfessionSkillUiState();

}

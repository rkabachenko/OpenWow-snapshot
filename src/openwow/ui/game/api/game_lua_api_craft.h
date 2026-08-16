
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

struct lua_State;

namespace openwow::ui::game::detail {

enum class TradeSkillTooltipTargetKind {
  kItem,
  kSpell,
};

struct TradeSkillTooltipTarget {
  TradeSkillTooltipTargetKind kind = TradeSkillTooltipTargetKind::kItem;
  std::uint32_t id = 0;
};

int LuaSelectTradeSkill(lua_State* L);
int LuaGetTradeSkillDescription(lua_State* L);
int LuaGetTradeSkillNumMade(lua_State* L);
int LuaGetTradeSkillItemLink(lua_State* L);
int LuaStopTradeSkillRepeat(lua_State* L);
int LuaSetTradeSkillItemNameFilter(lua_State* L);
int LuaGetTradeSkillItemNameFilter(lua_State* L);
int LuaSetTradeSkillItemLevelFilter(lua_State* L);
int LuaGetTradeSkillItemLevelFilter(lua_State* L);
int LuaTradeSkillOnlyShowMakeable(lua_State* L);
int LuaTradeSkillOnlyShowSkillUps(lua_State* L);

void SetTradeSkillItemNameFilterState(const char* text);
const char* GetTradeSkillItemNameFilterState();
void SetTradeSkillItemLevelFilterState(int min_level, int max_level);
int GetTradeSkillItemLevelFilterMinState();
int GetTradeSkillItemLevelFilterMaxState();
void SetTradeSkillOnlyShowMakeableState(bool enabled);
void SetTradeSkillOnlyShowSkillUpsState(bool enabled);
void SelectTradeSkillByLuaIndexState(int index);
std::uint32_t GetTradeSkillRepeatCountState();
void SetTradeSkillRecipeMaxRepeatCountForTests(std::size_t recipe_index,
                                               std::int32_t max_repeat_count);
void SetTradeSkillQueueStateForTests(std::int32_t selected_spell_id,
                                     std::int32_t npc_spell_id,
                                     std::uint32_t npc_repeat_count,
                                     std::int32_t player_spell_id,
                                     std::uint32_t player_repeat_count);
std::optional<TradeSkillTooltipTarget>
ResolveTradeSkillTooltipTarget(int recipe_index,
                               std::optional<int> reagent_index);

int LuaGetTradeSkillRecipeLink(lua_State* L);
int LuaGetTradeskillRepeatCount(lua_State* L);
int LuaGetTrainerSelectionIndex(lua_State* L);
int LuaGetTrainerServiceAbilityReq(lua_State* L);
int LuaGetTrainerServiceDescription(lua_State* L);
int LuaGetTrainerServiceItemLink(lua_State* L);
int LuaGetTrainerServiceNumAbilityReq(lua_State* L);
int LuaGetTrainerServiceStepIncrease(lua_State* L);
int LuaIsTrainerServiceSkillStep(lua_State* L);
int LuaOpenTrainer(lua_State* L);
int LuaSelectTrainerService(lua_State* L);

int LuaCollapseTrainerSkillLine(lua_State* L);
int LuaExpandTrainerSkillLine(lua_State* L);
int LuaGetSelectedSkill(lua_State* L);
int LuaSetSelectedSkill(lua_State* L);
int LuaSetTrainerSkillLineFilter(lua_State* L);

}

#include "openwow/game/quests/adapters/lua/quest_lua_bindings.h"
#include "openwow/ui/runtime/lua/lua_composition.h"

#include <cstddef>

#include "openwow/ui/lua_binding_registry.h"

extern "C" {
#include <lua.hpp>
}

namespace openwow::ui::game::detail {

int LuaGetNumQuestLogEntries(lua_State* L);
int LuaGetQuestLogTitle(lua_State* L);
int LuaGetQuestLogLeaderBoard(lua_State* L);
int LuaSelectQuestLogEntry(lua_State* L);
int LuaGetQuestLogQuestText(lua_State* L);
int LuaGetQuestLogRewardInfo(lua_State* L);
int LuaGetQuestLogRewardMoney(lua_State* L);
int LuaGetQuestLogRewardHonor(lua_State* L);
int LuaGetQuestLogRewardXP(lua_State* L);
int LuaGetQuestTimers(lua_State* L);
int LuaGetQuestLogTimeLeft(lua_State* L);
int LuaGetQuestLogRequiredMoney(lua_State* L);
int LuaGetQuestLogGroupNum(lua_State* L);
int LuaAcceptQuest(lua_State* L);
int LuaDeclineQuest(lua_State* L);
int LuaCompleteQuest(lua_State* L);
int LuaGetQuestReward(lua_State* L);
int LuaAbandonQuest(lua_State* L);
int LuaSetAbandonQuest(lua_State* L);
int LuaGetTitleText(lua_State* L);
int LuaGetObjectiveText(lua_State* L);
int LuaGetProgressText(lua_State* L);
int LuaGetRewardText(lua_State* L);
int LuaGetGreetingText(lua_State* L);
int LuaGetNumQuestChoices(lua_State* L);
int LuaGetNumQuestRewards(lua_State* L);
int LuaGetQuestItemInfo(lua_State* L);
int LuaGetQuestItemLink(lua_State* L);
int LuaGetQuestSpellLink(lua_State* L);
int LuaGetNumQuestItems(lua_State* L);
int LuaGetNumQuestItemDrops(lua_State* L);
int LuaGetQuestLogSpecialItemInfo(lua_State* L);
int LuaGetQuestLogSelection(lua_State* L);
int LuaGetQuestWatchIndex(lua_State* L);
int LuaAddQuestWatch(lua_State* L);
int LuaRemoveQuestWatch(lua_State* L);
int LuaGetNumQuestWatches(lua_State* L);
int LuaGetQuestLogPushable(lua_State* L);
int LuaQuestLogPushQuest(lua_State* L);
int LuaGetQuestLogChoiceInfo(lua_State* L);
int LuaGetQuestLogRewardTalents(lua_State* L);
int LuaGetQuestLogRewardArenaPoints(lua_State* L);
int LuaGetQuestLogRewardTitle(lua_State* L);
int LuaGetQuestLogRewardSpell(lua_State* L);
int LuaGetNumQuestLogRewards(lua_State* L);
int LuaGetNumQuestLogChoices(lua_State* L);
int LuaGetQuestLogItemDrop(lua_State* L);
int LuaGetQuestLink(lua_State* L);
int LuaGetAbandonQuestName(lua_State* L);
int LuaGetNumQuestLeaderBoards(lua_State* L);
int LuaGetQuestIndexForWatch(lua_State* L);
int LuaGetQuestIndexForTimer(lua_State* L);
int LuaIsQuestWatched(lua_State* L);
int LuaExpandQuestHeader(lua_State* L);
int LuaCollapseQuestHeader(lua_State* L);
int LuaGetDailyQuestsCompleted(lua_State* L);
int LuaGetMaxDailyQuests(lua_State* L);
int LuaGetQuestResetTime(lua_State* L);
int LuaQuestIsDaily(lua_State* L);
int LuaQuestIsWeekly(lua_State* L);
int LuaQuestMapUpdateAllQuests(lua_State* L);
int LuaGetQuestSortIndex(lua_State* L);
int LuaIsActiveQuestTrivial(lua_State* L);
int LuaIsAvailableQuestTrivial(lua_State* L);
int LuaQuestPOIGetIconInfo(lua_State* L);
int LuaQuestPOIGetQuestIDByIndex(lua_State* L);
int LuaQuestPOIGetQuestIDByVisibleIndex(lua_State* L);
int LuaGetQuestPOILeaderBoard(lua_State* L);
int LuaCloseQuest(lua_State* L);
int LuaConfirmAcceptQuest(lua_State* L);
int LuaGMSurveyQuestion(lua_State* L);
int LuaGetAbandonQuestItems(lua_State* L);
int LuaGetActiveLevel(lua_State* L);
int LuaGetActiveTitle(lua_State* L);
int LuaGetAvailableLevel(lua_State* L);
int LuaGetAvailableQuestInfo(lua_State* L);
int LuaGetAvailableTitle(lua_State* L);
int LuaGetNumActiveQuests(lua_State* L);
int LuaGetNumAvailableQuests(lua_State* L);
int LuaGetQuestBackgroundMaterial(lua_State* L);
int LuaGetQuestGreenRange(lua_State* L);
int LuaGetQuestLogCompletionText(lua_State* L);
int LuaGetQuestLogItemLink(lua_State* L);
int LuaGetQuestLogSpecialItemCooldown(lua_State* L);
int LuaGetQuestLogSpellLink(lua_State* L);
int LuaGetQuestMoneyToGet(lua_State* L);
int LuaGetQuestText(lua_State* L);
int LuaGetQuestWorldMapAreaID(lua_State* L);
int LuaGetRewardArenaPoints(lua_State* L);
int LuaGetRewardMoney(lua_State* L);
int LuaGetRewardHonor(lua_State* L);
int LuaGetRewardTalents(lua_State* L);
int LuaGetRewardSpell(lua_State* L);
int LuaGetRewardTitle(lua_State* L);
int LuaGetRewardXP(lua_State* L);
int LuaGetSuggestedGroupNum(lua_State* L);
int LuaIsCurrentQuestFailed(lua_State* L);
int LuaIsQuestCompletable(lua_State* L);
int LuaIsUnitOnQuest(lua_State* L);
int LuaQuestChooseRewardError(lua_State* L);
int LuaQuestFlagsPVP(lua_State* L);
int LuaQuestGetAutoAccept(lua_State* L);
int LuaSelectActiveQuest(lua_State* L);
int LuaSelectAvailableQuest(lua_State* L);
int LuaSortQuestWatches(lua_State* L);
int LuaShiftQuestWatches(lua_State* L);
int LuaUseQuestLogSpecialItem(lua_State* L);
int LuaIsQuestLogSpecialItemInRange(lua_State* L);

}

namespace openwow::ui::game {

using namespace detail;

namespace {

constexpr openwow::ui::LuaGlobalBinding kQuestLuaBindings[] = {
    {"GetNumQuestLogEntries", LuaGetNumQuestLogEntries},
    {"GetQuestLogTitle", LuaGetQuestLogTitle},
    {"GetQuestLogLeaderBoard", LuaGetQuestLogLeaderBoard},
    {"SelectQuestLogEntry", LuaSelectQuestLogEntry},
    {"GetQuestLogQuestText", LuaGetQuestLogQuestText},
    {"GetQuestLogRewardInfo", LuaGetQuestLogRewardInfo},
    {"GetQuestLogRewardMoney", LuaGetQuestLogRewardMoney},
    {"GetQuestLogRewardHonor", LuaGetQuestLogRewardHonor},
    {"GetQuestLogRewardXP", LuaGetQuestLogRewardXP},
    {"GetQuestTimers", LuaGetQuestTimers},
    {"GetQuestLogTimeLeft", LuaGetQuestLogTimeLeft},
    {"GetQuestLogRequiredMoney", LuaGetQuestLogRequiredMoney},
    {"GetQuestLogGroupNum", LuaGetQuestLogGroupNum},
    {"AcceptQuest", LuaAcceptQuest},
    {"DeclineQuest", LuaDeclineQuest},
    {"CompleteQuest", LuaCompleteQuest},
    {"GetQuestReward", LuaGetQuestReward},
    {"AbandonQuest", LuaAbandonQuest},
    {"SetAbandonQuest", LuaSetAbandonQuest},
    {"GetTitleText", LuaGetTitleText},
    {"GetObjectiveText", LuaGetObjectiveText},
    {"GetProgressText", LuaGetProgressText},
    {"GetRewardText", LuaGetRewardText},
    {"GetGreetingText", LuaGetGreetingText},
    {"GetNumQuestChoices", LuaGetNumQuestChoices},
    {"GetNumQuestRewards", LuaGetNumQuestRewards},
    {"GetQuestItemInfo", LuaGetQuestItemInfo},
    {"GetQuestItemLink", LuaGetQuestItemLink},
    {"GetQuestSpellLink", LuaGetQuestSpellLink},
    {"GetNumQuestItems", LuaGetNumQuestItems},
    {"GetNumQuestItemDrops", LuaGetNumQuestItemDrops},
    {"GetQuestLogSpecialItemInfo", LuaGetQuestLogSpecialItemInfo},
    {"GetQuestLogSelection", LuaGetQuestLogSelection},
    {"GetQuestWatchIndex", LuaGetQuestWatchIndex},
    {"AddQuestWatch", LuaAddQuestWatch},
    {"RemoveQuestWatch", LuaRemoveQuestWatch},
    {"GetNumQuestWatches", LuaGetNumQuestWatches},
    {"GetQuestLogPushable", LuaGetQuestLogPushable},
    {"QuestLogPushQuest", LuaQuestLogPushQuest},
    {"GetQuestLogChoiceInfo", LuaGetQuestLogChoiceInfo},
    {"GetQuestLogRewardTalents", LuaGetQuestLogRewardTalents},
    {"GetQuestLogRewardArenaPoints", LuaGetQuestLogRewardArenaPoints},
    {"GetQuestLogRewardTitle", LuaGetQuestLogRewardTitle},
    {"GetQuestLogRewardSpell", LuaGetQuestLogRewardSpell},
    {"GetNumQuestLogRewards", LuaGetNumQuestLogRewards},
    {"GetNumQuestLogChoices", LuaGetNumQuestLogChoices},
    {"GetQuestLogItemDrop", LuaGetQuestLogItemDrop},
    {"GetQuestLink", LuaGetQuestLink},
    {"GetAbandonQuestName", LuaGetAbandonQuestName},
    {"GetNumQuestLeaderBoards", LuaGetNumQuestLeaderBoards},
    {"GetQuestIndexForWatch", LuaGetQuestIndexForWatch},
    {"GetQuestIndexForTimer", LuaGetQuestIndexForTimer},
    {"IsQuestWatched", LuaIsQuestWatched},
    {"ExpandQuestHeader", LuaExpandQuestHeader},
    {"CollapseQuestHeader", LuaCollapseQuestHeader},
    {"GetDailyQuestsCompleted", LuaGetDailyQuestsCompleted},
    {"GetMaxDailyQuests", LuaGetMaxDailyQuests},
    {"GetQuestResetTime", LuaGetQuestResetTime},
    {"QuestIsDaily", LuaQuestIsDaily},
    {"QuestIsWeekly", LuaQuestIsWeekly},
    {"QuestMapUpdateAllQuests", LuaQuestMapUpdateAllQuests},
    {"GetQuestSortIndex", LuaGetQuestSortIndex},
    {"IsActiveQuestTrivial", LuaIsActiveQuestTrivial},
    {"IsAvailableQuestTrivial", LuaIsAvailableQuestTrivial},
    {"QuestPOIGetIconInfo", LuaQuestPOIGetIconInfo},
    {"QuestPOIGetQuestIDByIndex", LuaQuestPOIGetQuestIDByIndex},
    {"QuestPOIGetQuestIDByVisibleIndex", LuaQuestPOIGetQuestIDByVisibleIndex},
    {"GetQuestPOILeaderBoard", LuaGetQuestPOILeaderBoard},
    {"CloseQuest", LuaCloseQuest},
    {"ConfirmAcceptQuest", LuaConfirmAcceptQuest},
    {"GMSurveyQuestion", LuaGMSurveyQuestion},
    {"GetAbandonQuestItems", LuaGetAbandonQuestItems},
    {"GetActiveLevel", LuaGetActiveLevel},
    {"GetActiveTitle", LuaGetActiveTitle},
    {"GetAvailableLevel", LuaGetAvailableLevel},
    {"GetAvailableQuestInfo", LuaGetAvailableQuestInfo},
    {"GetAvailableTitle", LuaGetAvailableTitle},
    {"GetNumActiveQuests", LuaGetNumActiveQuests},
    {"GetNumAvailableQuests", LuaGetNumAvailableQuests},
    {"GetQuestBackgroundMaterial", LuaGetQuestBackgroundMaterial},
    {"GetQuestGreenRange", LuaGetQuestGreenRange},
    {"GetQuestLogCompletionText", LuaGetQuestLogCompletionText},
    {"GetQuestLogItemLink", LuaGetQuestLogItemLink},
    {"GetQuestLogSpecialItemCooldown", LuaGetQuestLogSpecialItemCooldown},
    {"GetQuestLogSpellLink", LuaGetQuestLogSpellLink},
    {"GetQuestMoneyToGet", LuaGetQuestMoneyToGet},
    {"GetQuestText", LuaGetQuestText},
    {"GetQuestWorldMapAreaID", LuaGetQuestWorldMapAreaID},
    {"GetRewardArenaPoints", LuaGetRewardArenaPoints},
    {"GetRewardMoney", LuaGetRewardMoney},
    {"GetRewardHonor", LuaGetRewardHonor},
    {"GetRewardTalents", LuaGetRewardTalents},
    {"GetRewardSpell", LuaGetRewardSpell},
    {"GetRewardTitle", LuaGetRewardTitle},
    {"GetRewardXP", LuaGetRewardXP},
    {"GetSuggestedGroupNum", LuaGetSuggestedGroupNum},
    {"IsCurrentQuestFailed", LuaIsCurrentQuestFailed},
    {"IsQuestCompletable", LuaIsQuestCompletable},
    {"IsUnitOnQuest", LuaIsUnitOnQuest},
    {"QuestChooseRewardError", LuaQuestChooseRewardError},
    {"QuestFlagsPVP", LuaQuestFlagsPVP},
    {"QuestGetAutoAccept", LuaQuestGetAutoAccept},
    {"SelectActiveQuest", LuaSelectActiveQuest},
    {"SelectAvailableQuest", LuaSelectAvailableQuest},
    {"SortQuestWatches", LuaSortQuestWatches},
    {"ShiftQuestWatches", LuaShiftQuestWatches},
    {"UseQuestLogSpecialItem", LuaUseQuestLogSpecialItem},
    {"IsQuestLogSpecialItemInRange", LuaIsQuestLogSpecialItemInRange},
};

}

openwow::ui::lua::NativeBindingCatalog QuestNativeBindingCatalog() {
  return openwow::ui::lua::NativeFunctionCatalog(
      "game.quests", openwow::ui::lua::BindingScope::kWorld, kQuestLuaBindings);
}

}

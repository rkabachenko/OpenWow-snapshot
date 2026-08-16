
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

struct lua_State;

namespace openwow::game {
struct ItemTemplate;
class WorldSession;
struct QuestRewardItem;
}

namespace openwow::ui::game::detail {

int LuaGetNumQuestLogEntries(lua_State *L);
int LuaGetQuestLogTitle(lua_State *L);
int LuaGetQuestLogLeaderBoard(lua_State *L);
int LuaSelectQuestLogEntry(lua_State *L);
int LuaGetQuestLogQuestText(lua_State *L);
int LuaGetQuestLogRewardInfo(lua_State *L);
int LuaGetQuestLogRewardMoney(lua_State *L);
int LuaGetQuestLogRewardHonor(lua_State *L);
int LuaGetNumQuestLogRewardFactions(lua_State *L);
int LuaGetQuestLogRewardFactionInfo(lua_State *L);
int LuaProcessQuestLogRewardFactions(lua_State *L);
int LuaGetQuestLogRewardXP(lua_State *L);
int LuaGetQuestTimers(lua_State *L);
int LuaGetQuestLogTimeLeft(lua_State *L);
int LuaGetQuestLogRequiredMoney(lua_State *L);
int LuaGetQuestLogGroupNum(lua_State *L);

int LuaAcceptQuest(lua_State *L);
int LuaDeclineQuest(lua_State *L);
int LuaCompleteQuest(lua_State *L);
int LuaGetQuestReward(lua_State *L);
int LuaAbandonQuest(lua_State *L);
int LuaSetAbandonQuest(lua_State *L);

int LuaGetTitleText(lua_State *L);
int LuaGetObjectiveText(lua_State *L);
int LuaGetProgressText(lua_State *L);
int LuaGetRewardText(lua_State *L);
int LuaGetGreetingText(lua_State *L);

int LuaGetNumQuestChoices(lua_State *L);
int LuaGetNumQuestRewards(lua_State *L);
int LuaGetQuestItemInfo(lua_State *L);
int LuaGetQuestItemLink(lua_State *L);
int LuaGetQuestSpellLink(lua_State *L);
int LuaGetNumQuestItems(lua_State *L);
int LuaGetNumQuestItemDrops(lua_State *L);
std::optional<::openwow::game::QuestRewardItem>
GetQuestPreviewItem(const ::openwow::game::WorldSession *session, std::string_view item_type,
                    int one_based_index);
const ::openwow::game::ItemTemplate *
GetOrRequestQuestPreviewItemTemplate(::openwow::game::WorldSession *session,
                                     std::uint32_t item_id);
std::uint32_t GetSelectedQuestLogItemId(::openwow::game::WorldSession *session,
                                        std::string_view item_type,
                                        int one_based_index);

int LuaCloseQuest(lua_State *L);
int LuaConfirmAcceptQuest(lua_State *L);
int LuaSelectActiveQuest(lua_State *L);
int LuaSelectAvailableQuest(lua_State *L);
int LuaSortQuestWatches(lua_State *L);
int LuaGetAbandonQuestItems(lua_State *L);
int LuaGetActiveLevel(lua_State *L);
int LuaGetActiveTitle(lua_State *L);
int LuaGetAvailableLevel(lua_State *L);
int LuaGetAvailableQuestInfo(lua_State *L);
int LuaGetAvailableTitle(lua_State *L);
int LuaGetNumActiveQuests(lua_State *L);
int LuaGetNumAvailableQuests(lua_State *L);
int LuaGetQuestBackgroundMaterial(lua_State *L);
int LuaGetQuestGreenRange(lua_State *L);
int LuaGetQuestLogCompletionText(lua_State *L);
int LuaGetQuestLogItemLink(lua_State *L);
int LuaGetQuestLogSpecialItemCooldown(lua_State *L);
int LuaGetQuestLogSpellLink(lua_State *L);
int LuaGetQuestMoneyToGet(lua_State *L);
int LuaGetQuestText(lua_State *L);
int LuaGetQuestWorldMapAreaID(lua_State *L);
int LuaGetRewardMoney(lua_State *L);
int LuaGetRewardArenaPoints(lua_State *L);
int LuaGetRewardHonor(lua_State *L);
int LuaGetRewardTalents(lua_State *L);
int LuaGetRewardSpell(lua_State *L);
int LuaGetRewardTitle(lua_State *L);
int LuaGetRewardXP(lua_State *L);
int LuaGetSuggestedGroupNum(lua_State *L);
int LuaIsCurrentQuestFailed(lua_State *L);
int LuaIsQuestCompletable(lua_State *L);
int LuaIsUnitOnQuest(lua_State *L);
int LuaQuestChooseRewardError(lua_State *L);
int LuaQuestFlagsPVP(lua_State *L);
int LuaQuestGetAutoAccept(lua_State *L);

int LuaGetQuestLogChoiceInfo(lua_State *L);
int LuaGetQuestLogRewardTalents(lua_State *L);
int LuaGetQuestLogRewardArenaPoints(lua_State *L);
int LuaGetQuestLogRewardTitle(lua_State *L);
int LuaGetQuestLogRewardSpell(lua_State *L);

int LuaCGTooltip_SetQuestLogRewardSpell(lua_State *L);

int LuaGetNumQuestLogRewards(lua_State *L);
int LuaGetNumQuestLogChoices(lua_State *L);
int LuaGetQuestLogItemDrop(lua_State *L);
int LuaGetQuestLink(lua_State *L);
int LuaGetAbandonQuestName(lua_State *L);
int LuaGetNumQuestLeaderBoards(lua_State *L);
int LuaGetQuestIndexForWatch(lua_State *L);
int LuaGetQuestIndexForTimer(lua_State *L);
int LuaIsQuestWatched(lua_State *L);
int LuaExpandQuestHeader(lua_State *L);
int LuaCollapseQuestHeader(lua_State *L);
int LuaGetDailyQuestsCompleted(lua_State *L);
int LuaGetMaxDailyQuests(lua_State *L);
int LuaGetQuestResetTime(lua_State *L);
int LuaQuestIsDaily(lua_State *L);
int LuaQuestIsWeekly(lua_State *L);
int LuaGetQuestSortIndex(lua_State *L);
int LuaIsActiveQuestTrivial(lua_State *L);
int LuaIsAvailableQuestTrivial(lua_State *L);
int LuaQuestPOIGetIconInfo(lua_State *L);
int LuaQuestPOIGetQuestIDByIndex(lua_State *L);
int LuaQuestPOIGetQuestIDByVisibleIndex(lua_State *L);
int LuaQuestMapUpdateAllQuests(lua_State *L);
int LuaGetQuestPOILeaderBoard(lua_State *L);

int LuaGetQuestLogSpecialItemInfo(lua_State *L);
int LuaIsQuestLogSpecialItemInRange(lua_State *L);
int LuaGetQuestLogSelection(lua_State *L);
int LuaGetQuestWatchIndex(lua_State *L);
int LuaAddQuestWatch(lua_State *L);
int LuaRemoveQuestWatch(lua_State *L);
int LuaGetNumQuestWatches(lua_State *L);
int LuaGetQuestLogPushable(lua_State *L);
int LuaQuestLogPushQuest(lua_State *L);
int LuaShiftQuestWatches(lua_State *L);
int LuaUseQuestLogSpecialItem(lua_State *L);

}

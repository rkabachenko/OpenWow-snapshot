
#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaGetNumTrackingTypes(lua_State* L);
int LuaGetTrackingInfo(lua_State* L);
int LuaSetTracking(lua_State* L);
int LuaGetTrackingTexture(lua_State* L);
int LuaApplyBarberShopStyle(lua_State* L);
int LuaBarberShopReset(lua_State* L);
int LuaCancelBarberShop(lua_State* L);
int LuaSetNextBarberShopStyle(lua_State* L);
int LuaCanAlterSkin(lua_State* L);
int LuaGetBarberShopInfo(lua_State* L);
int LuaGetBarberShopTotalCost(lua_State* L);
int LuaGetFacialHairCustomization(lua_State* L);
int LuaGetHairCustomization(lua_State* L);
int LuaGetCurrentTitle(lua_State* L);
int LuaGetNumTitles(lua_State* L);
int LuaGetTitleName(lua_State* L);
int LuaIsTitleKnown(lua_State* L);
int LuaSetCurrentTitle(lua_State* L);
int LuaGetCoinText(lua_State* L);
int LuaGetCoinTextureString(lua_State* L);
int LuaExpandCurrencyList(lua_State* L);
int LuaSetCurrencyUnused(lua_State* L);
int LuaGMReportLag(lua_State* L);
int LuaGMRequestPlayerInfo(lua_State* L);
int LuaGMResponseResolve(lua_State* L);
int LuaDeleteGMTicket(lua_State* L);
int LuaGMSurveyAnswer(lua_State* L);
int LuaGMSurveyCommentSubmit(lua_State* L);
int LuaGMSurveyQuestion(lua_State* L);
int LuaGMSurveySubmit(lua_State* L);
int LuaGetGMStatus(lua_State* L);
int LuaGetGMTicketCategories(lua_State* L);
int LuaGetGMTicket(lua_State* L);
int LuaUpdateGMTicket(lua_State* L);
int LuaNewGMTicket(lua_State* L);
int LuaSaveView(lua_State* L);
int LuaSetView(lua_State* L);
int LuaPrevView(lua_State* L);
int LuaPlayDance(lua_State* L);
int LuaMakeMinigameMove(lua_State* L);
int LuaGMResponseNeedMoreHelp(lua_State* L);
int LuaGMSurveyAnswerSubmit(lua_State* L);
int LuaGMSurveyNumAnswers(lua_State* L);
int LuaReportBug(lua_State* L);
int LuaReportSuggestion(lua_State* L);
int LuaClearTutorials(lua_State* L);
int LuaResetTutorials(lua_State* L);
int LuaTriggerTutorial(lua_State* L);
int LuaFlagTutorial(lua_State* L);
int LuaIsTutorialFlagged(lua_State* L);
int LuaGetNextCompleatedTutorial(lua_State* L);
int LuaGetPrevCompleatedTutorial(lua_State* L);
int LuaGetAutoCompletePresenceID(lua_State* L);
int LuaGetBackpackCurrencyInfo(lua_State* L);
int LuaGetEventCPUUsage(lua_State* L);
int LuaGetFrameCPUUsage(lua_State* L);
int LuaGetFunctionCPUUsage(lua_State* L);
int LuaGetNumDeclensionSets(lua_State* L);
int LuaGetPetitionItemInfo(lua_State* L);
int LuaKeyRingButtonIDToInvSlotID(lua_State* L);
int LuaSetCurrencyBackpack(lua_State* L);
int LuaSetSavedInstanceExtend(lua_State* L);

int LuaVoiceChat_RecordLoopbackSound(lua_State* L);

}

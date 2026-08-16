#include "openwow/ui/game/api/framexml_native_bindings.h"
#include "openwow/ui/game/api/game_lua_api_addon.h"
#include "openwow/ui/lua_binding_registry.h"

namespace openwow::ui::game::detail {

int LuaCanPartyLFGBackfill(lua_State* L);
int LuaPlayerIsPVPInactive(lua_State* L);
int LuaGetCurrencyListSize(lua_State* L);
int LuaGetCurrencyListInfo(lua_State* L);
int LuaClearChannelWatch(lua_State* L);
int LuaGetChannelRosterInfo(lua_State* L);
int LuaGetGMTicketCategories(lua_State* L);
int LuaGetNumArenaOpponents(lua_State* L);
int LuaGetNumArenaTeamMembers(lua_State* L);
int LuaApplyBarberShopStyle(lua_State* L);
int LuaBarberShopReset(lua_State* L);
int LuaCanAlterSkin(lua_State* L);
int LuaCanEditMOTD(lua_State* L);
int LuaCanEditOfficerNote(lua_State* L);
int LuaCanEditPublicNote(lua_State* L);
int LuaCanResetTutorials(lua_State* L);
int LuaCanViewOfficerNote(lua_State* L);
int LuaCancelBarberShop(lua_State* L);
int LuaCommentatorSetMode(lua_State* L);
int LuaCommentatorToggleMode(lua_State* L);
int LuaCommentatorGetMode(lua_State* L);
int LuaCommentatorGetNumMaps(lua_State* L);
int LuaCommentatorGetMapInfo(lua_State* L);
int LuaCommentatorGetInstanceInfo(lua_State* L);
int LuaCommentatorEnterInstance(lua_State* L);
int LuaCommentatorExitInstance(lua_State* L);
int LuaCommentatorGetNumPlayers(lua_State* L);
int LuaCommentatorGetPlayerInfo(lua_State* L);
int LuaCommentatorFollowPlayer(lua_State* L);
int LuaCommentatorLookatPlayer(lua_State* L);
int LuaCommentatorZoomIn(lua_State* L);
int LuaCommentatorZoomOut(lua_State* L);
int LuaCommentatorSetCamera(lua_State* L);
int LuaCommentatorGetCamera(lua_State* L);
int LuaCommentatorGetCurrentMapID(lua_State* L);
int LuaCommentatorStartInstance(lua_State* L);
int LuaCommentatorAddPlayer(lua_State* L);
int LuaCommentatorRemovePlayer(lua_State* L);
int LuaCommentatorSetBattlemaster(lua_State* L);
int LuaCommentatorSetMoveSpeed(lua_State* L);
int LuaCommentatorSetCameraCollision(lua_State* L);
int LuaCommentatorSetTargetHeightOffset(lua_State* L);
int LuaCommentatorSetMapAndInstanceIndex(lua_State* L);
int LuaCommentatorSetPlayerIndex(lua_State* L);
int LuaCommentatorUpdatePlayerInfo(lua_State* L);
int LuaCommentatorUpdateMapInfo(lua_State* L);
int LuaCommentatorSetSkirmishMatchmakingMode(lua_State* L);
int LuaCommentatorRequestSkirmishQueueData(lua_State* L);
int LuaCommentatorGetSkirmishQueueCount(lua_State* L);
int LuaCommentatorGetSkirmishQueuePlayerInfo(lua_State* L);
int LuaCommentatorStartSkirmishMatch(lua_State* L);
int LuaCommentatorRequestSkirmishMode(lua_State* L);
int LuaCommentatorGetSkirmishMode(lua_State* L);
int LuaBattlefieldMgrEntryInviteResponse(lua_State* L);
int LuaBattlefieldMgrQueueRequest(lua_State* L);
int LuaBattlefieldMgrQueueInviteResponse(lua_State* L);
int LuaBattlefieldMgrExitRequest(lua_State* L);
int LuaGMReportLag(lua_State* L);
int LuaGMRequestPlayerInfo(lua_State* L);
int LuaGMResponseResolve(lua_State* L);
int LuaGMSurveyAnswer(lua_State* L);
int LuaGMSurveyCommentSubmit(lua_State* L);
int LuaGMSurveySubmit(lua_State* L);
int LuaGetAllowLowLevelRaid(lua_State* L);
int LuaGetBarberShopInfo(lua_State* L);
int LuaGetBarberShopTotalCost(lua_State* L);
int LuaGetCoinIcon(lua_State* L);
int LuaGetCoinText(lua_State* L);
int LuaGetCoinTextureString(lua_State* L);
int LuaGetCurrentTitle(lua_State* L);
int LuaGetFacialHairCustomization(lua_State* L);
int LuaGetGMStatus(lua_State* L);
int LuaGetGMTicket(lua_State* L);
int LuaGetHairCustomization(lua_State* L);
int LuaGetNumTitles(lua_State* L);
int LuaGetPossessInfo(lua_State* L);
int LuaGetTitleName(lua_State* L);
int LuaHasKey(lua_State* L);
int LuaIsPossessBarVisible(lua_State* L);
int LuaIsTitleKnown(lua_State* L);
int LuaSetAllowLowLevelRaid(lua_State* L);
int LuaSetArenaTeamRosterSelection(lua_State* L);
int LuaSetArenaTeamRosterShowOffline(lua_State* L);
int LuaSetChannelOwner(lua_State* L);
int LuaSetChannelPassword(lua_State* L);
int LuaSetChannelWatch(lua_State* L);
int LuaSetCurrentTitle(lua_State* L);
int LuaSetNextBarberShopStyle(lua_State* L);
int LuaDeleteGMTicket(lua_State* L);
int LuaUpdateGMTicket(lua_State* L);
int LuaDoEmote(lua_State* L);
int LuaExpandCurrencyList(lua_State* L);
int LuaSetCurrencyUnused(lua_State* L);
int LuaNewGMTicket(lua_State* L);
int LuaGMResponseNeedMoreHelp(lua_State* L);
int LuaGMSurveyAnswerSubmit(lua_State* L);
int LuaGMSurveyNumAnswers(lua_State* L);
int LuaReportBug(lua_State* L);
int LuaReportSuggestion(lua_State* L);
int LuaGetBackpackCurrencyInfo(lua_State* L);
int LuaSetCurrencyBackpack(lua_State* L);
int LuaClearTutorials(lua_State* L);
int LuaResetTutorials(lua_State* L);
int LuaTriggerTutorial(lua_State* L);
int LuaFlagTutorial(lua_State* L);
int LuaIsTutorialFlagged(lua_State* L);
int LuaGetNextCompleatedTutorial(lua_State* L);
int LuaGetPrevCompleatedTutorial(lua_State* L);

}

namespace openwow::ui::game::detail {

namespace {

constexpr openwow::ui::LuaGlobalBinding kClientLuaBindings[] = {
    {"GetNumAddOns", kGetNumAddOns.handler},
    {"CanPartyLFGBackfill", LuaCanPartyLFGBackfill},
    {"PlayerIsPVPInactive", LuaPlayerIsPVPInactive},
    {"GetCurrencyListSize", LuaGetCurrencyListSize},
    {"GetCurrencyListInfo", LuaGetCurrencyListInfo},
    {"ClearChannelWatch", LuaClearChannelWatch},
    {"GetChannelRosterInfo", LuaGetChannelRosterInfo},
    {"GetGMTicketCategories", LuaGetGMTicketCategories},
    {"GetNumArenaOpponents", LuaGetNumArenaOpponents},
    {"GetNumArenaTeamMembers", LuaGetNumArenaTeamMembers},
    {"ApplyBarberShopStyle", LuaApplyBarberShopStyle},
    {"BarberShopReset", LuaBarberShopReset},
    {"CanAlterSkin", LuaCanAlterSkin},
    {"CanEditMOTD", LuaCanEditMOTD},
    {"CanEditOfficerNote", LuaCanEditOfficerNote},
    {"CanEditPublicNote", LuaCanEditPublicNote},
    {"CanResetTutorials", LuaCanResetTutorials},
    {"CanViewOfficerNote", LuaCanViewOfficerNote},
    {"CancelBarberShop", LuaCancelBarberShop},
    {"CommentatorSetMode", LuaCommentatorSetMode},
    {"CommentatorToggleMode", LuaCommentatorToggleMode},
    {"CommentatorGetMode", LuaCommentatorGetMode},
    {"CommentatorGetNumMaps", LuaCommentatorGetNumMaps},
    {"CommentatorGetMapInfo", LuaCommentatorGetMapInfo},
    {"CommentatorGetInstanceInfo", LuaCommentatorGetInstanceInfo},
    {"CommentatorEnterInstance", LuaCommentatorEnterInstance},
    {"CommentatorExitInstance", LuaCommentatorExitInstance},
    {"CommentatorGetNumPlayers", LuaCommentatorGetNumPlayers},
    {"CommentatorGetPlayerInfo", LuaCommentatorGetPlayerInfo},
    {"CommentatorFollowPlayer", LuaCommentatorFollowPlayer},
    {"CommentatorLookatPlayer", LuaCommentatorLookatPlayer},
    {"CommentatorZoomIn", LuaCommentatorZoomIn},
    {"CommentatorZoomOut", LuaCommentatorZoomOut},
    {"CommentatorSetCamera", LuaCommentatorSetCamera},
    {"CommentatorGetCamera", LuaCommentatorGetCamera},
    {"CommentatorGetCurrentMapID", LuaCommentatorGetCurrentMapID},
    {"CommentatorStartInstance", LuaCommentatorStartInstance},
    {"CommentatorAddPlayer", LuaCommentatorAddPlayer},
    {"CommentatorRemovePlayer", LuaCommentatorRemovePlayer},
    {"CommentatorSetBattlemaster", LuaCommentatorSetBattlemaster},
    {"CommentatorSetMoveSpeed", LuaCommentatorSetMoveSpeed},
    {"CommentatorSetCameraCollision", LuaCommentatorSetCameraCollision},
    {"CommentatorSetTargetHeightOffset", LuaCommentatorSetTargetHeightOffset},
    {"CommentatorSetMapAndInstanceIndex", LuaCommentatorSetMapAndInstanceIndex},
    {"CommentatorSetPlayerIndex", LuaCommentatorSetPlayerIndex},
    {"CommentatorUpdatePlayerInfo", LuaCommentatorUpdatePlayerInfo},
    {"CommentatorUpdateMapInfo", LuaCommentatorUpdateMapInfo},
    {"CommentatorSetSkirmishMatchmakingMode", LuaCommentatorSetSkirmishMatchmakingMode},
    {"CommentatorRequestSkirmishQueueData", LuaCommentatorRequestSkirmishQueueData},
    {"CommentatorGetSkirmishQueueCount", LuaCommentatorGetSkirmishQueueCount},
    {"CommentatorGetSkirmishQueuePlayerInfo", LuaCommentatorGetSkirmishQueuePlayerInfo},
    {"CommentatorStartSkirmishMatch", LuaCommentatorStartSkirmishMatch},
    {"CommentatorRequestSkirmishMode", LuaCommentatorRequestSkirmishMode},
    {"CommentatorGetSkirmishMode", LuaCommentatorGetSkirmishMode},
    {"BattlefieldMgrEntryInviteResponse", LuaBattlefieldMgrEntryInviteResponse},
    {"BattlefieldMgrQueueRequest", LuaBattlefieldMgrQueueRequest},
    {"BattlefieldMgrQueueInviteResponse", LuaBattlefieldMgrQueueInviteResponse},
    {"BattlefieldMgrExitRequest", LuaBattlefieldMgrExitRequest},
    {"GMReportLag", LuaGMReportLag},
    {"GMRequestPlayerInfo", LuaGMRequestPlayerInfo},
    {"GMResponseResolve", LuaGMResponseResolve},
    {"GMSurveyAnswer", LuaGMSurveyAnswer},
    {"GMSurveyCommentSubmit", LuaGMSurveyCommentSubmit},
    {"GMSurveySubmit", LuaGMSurveySubmit},
    {"GetAllowLowLevelRaid", LuaGetAllowLowLevelRaid},

    {"GetBarberShopStyleInfo", LuaGetBarberShopInfo},
    {"GetBarberShopTotalCost", LuaGetBarberShopTotalCost},
    {"GetCoinIcon", LuaGetCoinIcon},
    {"GetCoinText", LuaGetCoinText},
    {"GetCoinTextureString", LuaGetCoinTextureString},
    {"GetCurrentTitle", LuaGetCurrentTitle},
    {"GetFacialHairCustomization", LuaGetFacialHairCustomization},
    {"GetGMStatus", LuaGetGMStatus},
    {"GetGMTicket", LuaGetGMTicket},
    {"GetHairCustomization", LuaGetHairCustomization},
    {"GetNumTitles", LuaGetNumTitles},
    {"GetPossessInfo", LuaGetPossessInfo},
    {"GetTitleName", LuaGetTitleName},
    {"HasKey", LuaHasKey},
    {"IsPossessBarVisible", LuaIsPossessBarVisible},
    {"IsTitleKnown", LuaIsTitleKnown},
    {"SetAllowLowLevelRaid", LuaSetAllowLowLevelRaid},
    {"SetArenaTeamRosterSelection", LuaSetArenaTeamRosterSelection},
    {"SetArenaTeamRosterShowOffline", LuaSetArenaTeamRosterShowOffline},
    {"SetChannelOwner", LuaSetChannelOwner},
    {"SetChannelPassword", LuaSetChannelPassword},
    {"SetChannelWatch", LuaSetChannelWatch},
    {"SetCurrentTitle", LuaSetCurrentTitle},
    {"SetNextBarberShopStyle", LuaSetNextBarberShopStyle},
    {"DeleteGMTicket", LuaDeleteGMTicket},
    {"UpdateGMTicket", LuaUpdateGMTicket},
    {"DoEmote", LuaDoEmote},
    {"ExpandCurrencyList", LuaExpandCurrencyList},
    {"SetCurrencyUnused", LuaSetCurrencyUnused},
    {"NewGMTicket", LuaNewGMTicket},
    {"GMResponseNeedMoreHelp", LuaGMResponseNeedMoreHelp},
    {"GMSurveyAnswerSubmit", LuaGMSurveyAnswerSubmit},
    {"GMSurveyNumAnswers", LuaGMSurveyNumAnswers},
    {"ReportBug", LuaReportBug},
    {"ReportSuggestion", LuaReportSuggestion},
    {"GetBackpackCurrencyInfo", LuaGetBackpackCurrencyInfo},
    {"SetCurrencyBackpack", LuaSetCurrencyBackpack},
    {"ClearTutorials", LuaClearTutorials},
    {"ResetTutorials", LuaResetTutorials},
    {"TriggerTutorial", LuaTriggerTutorial},
    {"FlagTutorial", LuaFlagTutorial},
    {"IsTutorialFlagged", LuaIsTutorialFlagged},
    {"GetNextCompleatedTutorial", LuaGetNextCompleatedTutorial},
    {"GetPrevCompleatedTutorial", LuaGetPrevCompleatedTutorial},
};

}

openwow::ui::lua::NativeBindingCatalog
FrameXmlClientNativeBindings() {
  return openwow::ui::lua::NativeFunctionCatalog(
      "ui.framescript.client",
      openwow::ui::lua::BindingScope::kWorld, kClientLuaBindings);
}

}

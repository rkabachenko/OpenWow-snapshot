#include "openwow/ui/glue/glue_native_binding_catalogs.h"

#include "openwow/audio/adapters/lua/sound_voicechat_lua_registration.h"
#include "openwow/ui/game/api/game_lua_api_addon.h"
#include "openwow/ui/game/api/game_lua_api_misc.h"
#include "openwow/ui/game/api/game_lua_api_misc_ui.h"
#include "openwow/ui/game/api/game_lua_api_system.h"
#include "openwow/ui/game/framescript/core/frame_font_runtime.h"
#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/glue/glue_lua_api_internal.h"
#include "openwow/ui/glue/glue_lua_shared_handlers.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/runtime/lua/lua_composition.h"
#include <vector>

namespace openwow::ui::glue::detail {

int LuaShowChangedOptionWarnings(lua_State *state);
int LuaGetChangedOptionWarnings(lua_State *state);
int LuaAcceptChangedOptionWarnings(lua_State *state);
int LuaSetCurrentScreen(lua_State *state);
int LuaIsScanDLLFinished(lua_State *state);
int LuaScanningAccepted(lua_State *state);
int LuaContestAccepted(lua_State *state);
int LuaSetCharCustomizeFrame(lua_State *state);
int LuaPlayGlueMusic(lua_State *state);
int LuaPlayGlueAmbience(lua_State *state);
int LuaStopGlueAmbience(lua_State *state);
int LuaStopGlueMusic(lua_State *state);
int LuaPlayCreditsMusic(lua_State *state);
int LuaGetTime(lua_State *state);
int LuaGetGameTime(lua_State *state);
int LuaGetScreenWidth(lua_State *state);
int LuaGetScreenHeight(lua_State *state);
int LuaGetCursorPosition(lua_State *state);
int LuaGetBuildInfo(lua_State *state);
int LuaGetClientExpansionLevel(lua_State *state);
int LuaGetNumCharacters(lua_State *state);
int LuaSetCharSelectModelFrame(lua_State *state);
int LuaIsMacClient(lua_State *state);
int LuaGetMovieResolution(lua_State *state);
int LuaGetCVar(lua_State *state);
int LuaSetCVar(lua_State *state);
int LuaGetCVarBool(lua_State *state);
int LuaGetCVarDefault(lua_State *state);
int LuaIsStreamingTrial(lua_State *state);
int LuaGetServerName(lua_State *state);
int LuaGetSavedAccountName(lua_State *state);
int LuaSetSavedAccountName(lua_State *state);
int LuaGetSavedAccountList(lua_State *state);
int LuaSetSavedAccountList(lua_State *state);
int LuaEULAAccepted(lua_State *state);
int LuaTOSAccepted(lua_State *state);
int LuaTerminationWithoutNoticeAccepted(lua_State *state);
int LuaDefaultServerLogin(lua_State *state);
int LuaLaunchURL(lua_State *state);
int LuaGetCreditsText(lua_State *state);
int LuaQuitGame(lua_State *state);
int LuaStopAllSFX(lua_State *state);
int LuaScreenshot(lua_State *state);
int LuaHideCursor(lua_State *state);
int LuaShowCursor(lua_State *state);
int LuaIsShiftKeyDown(lua_State *state);
int LuaAcceptEULA(lua_State *state);
int LuaAcceptTOS(lua_State *state);
int LuaAcceptTerminationWithoutNotice(lua_State *state);
int LuaAcceptScanning(lua_State *state);
int LuaAcceptContest(lua_State *state);
int LuaGetCVarAbsoluteMax(lua_State *state);
int LuaGetCVarAbsoluteMin(lua_State *state);
int LuaIsSystemSupported(lua_State *state);

int LuaCreateFrame(lua_State *state);
int LuaGetNumFrames(lua_State *state);
int LuaEnumerateFrames(lua_State *state);
int LuaGetFramesRegisteredForEvent(lua_State *state);
int LuaGetCurrentKeyBoardFocus(lua_State *state);

int LuaGetNumRealms(lua_State *state);
int LuaGetRealmInfo(lua_State *state);
int LuaGetRealmCategories(lua_State *state);
int LuaRequestRealmList(lua_State *state);
int LuaCancelRealmListQuery(lua_State *state);
int LuaRealmListUpdateRate(lua_State *state);
int LuaChangeRealm(lua_State *state);
int LuaRealmListDialogCancelled(lua_State *state);
int LuaSetRealmSplitState(lua_State *state);
int LuaRequestRealmSplitInfo(lua_State *state);
int LuaIsTournamentRealmCategory(lua_State *state);
int LuaIsInvalidTournamentRealmCategory(lua_State *state);
int LuaGetCharacterInfo(lua_State *state);
int LuaSelectCharacter(lua_State *state);
int LuaEnterWorld(lua_State *state);
int LuaGetCharacterListUpdate(lua_State *state);
int LuaReadyForAccountDataTimes(lua_State *state);
int LuaCreateCharacter(lua_State *state);
int LuaDeclineCharacter(lua_State *state);
int LuaDeclineName(lua_State *state);
int LuaDeleteCharacter(lua_State *state);
int LuaRenameCharacter(lua_State *state);
int LuaGetCharacterCreateFacing(lua_State *state);
int LuaGetCharacterSelectFacing(lua_State *state);
int LuaSetCharacterCreateFacing(lua_State *state);
int LuaSetCharacterSelectFacing(lua_State *state);
int LuaSetCharSelectBackground(lua_State *state);
int LuaGetSelectBackgroundModel(lua_State *state);
int LuaIsConnectedToServer(lua_State *state);
int LuaDisconnectFromServer(lua_State *state);
int LuaCancelLogin(lua_State *state);
int LuaQuitGameAndRunLauncher(lua_State *state);
int LuaGetNumAddOns(lua_State *state);
int LuaGetAddOnInfo(lua_State *state);
int LuaGetAddOnDependencies(lua_State *state);
int LuaGetAddOnEnableState(lua_State *state);
int LuaEnableAddOn(lua_State *state);
int LuaDisableAddOn(lua_State *state);
int LuaGlueEnableAllAddOns(lua_State *state);
int LuaGlueDisableAllAddOns(lua_State *state);
int LuaResetAddOns(lua_State *state);
int LuaSaveAddOns(lua_State *state);
int LuaSetAddonVersionCheck(lua_State *state);
int LuaIsAddonVersionCheckEnabled(lua_State *state);
int LuaLaunchAddOnURL(lua_State *state);
int LuaIsWindowsClient(lua_State *state);
int LuaIsLinuxClient(lua_State *state);
int LuaIsInvalidLocale(lua_State *state);
int LuaGetNumGameAccounts(lua_State *state);
int LuaGetGameAccountInfo(lua_State *state);
int LuaSetGameAccount(lua_State *state);
int LuaGetBillingPlan(lua_State *state);
int LuaGetBillingTimeRemaining(lua_State *state);
int LuaGetBillingTimeRested(lua_State *state);
int LuaGetAvailableRaces(lua_State *state);
int LuaGetAvailableClasses(lua_State *state);
int LuaGetClassesForRace(lua_State *state);
int LuaGetNameForRace(lua_State *state);
int LuaGetFactionForRace(lua_State *state);
int LuaGetSelectedRace(lua_State *state);
int LuaGetSelectedClass(lua_State *state);
int LuaGetSelectedSex(lua_State *state);
int LuaSetSelectedRace(lua_State *state);
int LuaSetSelectedClass(lua_State *state);
int LuaSetSelectedSex(lua_State *state);
int LuaIsRaceClassValid(lua_State *state);
int LuaIsRaceClassRestricted(lua_State *state);
int LuaGetSelectedCategory(lua_State *state);
int LuaUpdateCustomizationScene(lua_State *state);
int LuaUpdateSelectionCustomizationScene(lua_State *state);
int LuaUpdateCustomizationBackground(lua_State *state);
int LuaCycleCharCustomization(lua_State *state);
int LuaRandomizeCharCustomization(lua_State *state);
int LuaResetCharCustomize(lua_State *state);
int LuaGetFacialHairCustomization(lua_State *state);
int LuaGetHairCustomization(lua_State *state);
int LuaSetCharCustomizeBackground(lua_State *state);
int LuaCustomizeExistingCharacter(lua_State *state);
int LuaGetCVarMax(lua_State *state);
int LuaGetCVarMin(lua_State *state);
int LuaRestartGx(lua_State *state);
int LuaRestoreVideoEffectsDefaults(lua_State *state);
int LuaRestoreVideoResolutionDefaults(lua_State *state);
int LuaRestoreVideoStereoDefaults(lua_State *state);
int LuaSetClearConfigData(lua_State *state);
int LuaGetNumDeclensionSets(lua_State *state);
int LuaShowContestNotice(lua_State *state);
int LuaShowEULANotice(lua_State *state);
int LuaShowScanningNotice(lua_State *state);
int LuaShowTOSNotice(lua_State *state);
int LuaShowTerminationWithoutNoticeNotice(lua_State *state);
int LuaScanDLLStart(lua_State *state);
int LuaScanDLLContinueAnyway(lua_State *state);
int LuaSurveyNotificationDone(lua_State *state);
int LuaPINEntered(lua_State *state);
int LuaTokenEntered(lua_State *state);
int LuaMatrixEntered(lua_State *state);
int LuaMatrixCommit(lua_State *state);
int LuaMatrixRevert(lua_State *state);
int LuaGetMatrixCoordinates(lua_State *state);
int LuaStatusDialogClick(lua_State *state);
int LuaPaidChange_GetName(lua_State *state);
int LuaGetRandomName(lua_State *state);
int LuaPaidChange_GetPreviousRaceIndex(lua_State *state);
int LuaPaidChange_GetCurrentRaceIndex(lua_State *state);
int LuaPaidChange_GetCurrentClassIndex(lua_State *state);
int LuaGetCreateBackgroundModel(lua_State *state);
int LuaAccountMsg_GetNumUnreadMsgs(lua_State *state);
int LuaAccountMsg_GetIndexNextUnreadMsg(lua_State *state);
int LuaAccountMsg_GetHeaderSubject(lua_State *state);
int LuaAccountMsg_LoadBody(lua_State *state);
int LuaAccountMsg_LoadHeaders(lua_State *state);
int LuaAccountMsg_SetMsgRead(lua_State *state);
int LuaAccountMsg_GetHeaderPriority(lua_State *state);
int LuaAccountMsg_GetNumTotalMsgs(lua_State *state);
int LuaAccountMsg_GetNumUnreadUrgentMsgs(lua_State *state);
int LuaAccountMsg_GetIndexHighestPriorityUnreadMsg(lua_State *state);
int LuaAccountMsg_GetBody(lua_State *state);
int LuaGetPatchDownloadProgress(lua_State *state);
int LuaPatchDownloadApply(lua_State *state);
int LuaPatchDownloadCancel(lua_State *state);
int LuaGetLocale(lua_State *state);
int LuaGetText(lua_State *state);
int LuaIsTrialAccount(lua_State *state);
int LuaGetAccountExpansionLevel(lua_State *state);
int LuaSetUsesToken(lua_State *state);
int LuaGetUsesToken(lua_State *state);
int LuaSortRealms(lua_State *state);
int LuaSetPreferredInfo(lua_State *state);
namespace {

constexpr openwow::ui::LuaGlobalBinding kGlueAccountMsgBindings[] = {
    {"AccountMsg_LoadHeaders", LuaAccountMsg_LoadHeaders},
    {"AccountMsg_GetNumTotalMsgs", LuaAccountMsg_GetNumTotalMsgs},
    {"AccountMsg_GetNumUnreadMsgs", LuaAccountMsg_GetNumUnreadMsgs},
    {"AccountMsg_GetNumUnreadUrgentMsgs", LuaAccountMsg_GetNumUnreadUrgentMsgs},
    {"AccountMsg_GetIndexHighestPriorityUnreadMsg",
     LuaAccountMsg_GetIndexHighestPriorityUnreadMsg},
    {"AccountMsg_GetIndexNextUnreadMsg", LuaAccountMsg_GetIndexNextUnreadMsg},
    {"AccountMsg_GetHeaderSubject", LuaAccountMsg_GetHeaderSubject},
    {"AccountMsg_GetHeaderPriority", LuaAccountMsg_GetHeaderPriority},
    {"AccountMsg_LoadBody", LuaAccountMsg_LoadBody},
    {"AccountMsg_GetBody", LuaAccountMsg_GetBody},
    {"AccountMsg_SetMsgRead", LuaAccountMsg_SetMsgRead},
};

constexpr openwow::ui::LuaGlobalBinding kGlueSystemExpansionBinding[] = {

    {"GetAccountExpansionLevel",
     openwow::ui::game::detail::LuaGetAccountExpansionLevel},
};

constexpr openwow::ui::LuaGlobalBinding kGlueTimeSystemBindings[] = {
    {"GetTime", LuaGetTime},
    {"GetGameTime", LuaGetGameTime},
};

constexpr openwow::ui::LuaGlobalBinding kGluePreFrameScriptBindings[] = {
    {"ShowChangedOptionWarnings", LuaShowChangedOptionWarnings},
    {"GetChangedOptionWarnings", LuaGetChangedOptionWarnings},
    {"AcceptChangedOptionWarnings", LuaAcceptChangedOptionWarnings},
    {"SetCurrentScreen", LuaSetCurrentScreen},
    {"IsScanDLLFinished", LuaIsScanDLLFinished},
    {"ScanningAccepted", LuaScanningAccepted},
    {"ContestAccepted", LuaContestAccepted},
    {"SetCharCustomizeFrame", LuaSetCharCustomizeFrame},
    {"PlayGlueMusic", LuaPlayGlueMusic},
    {"PlayGlueAmbience", LuaPlayGlueAmbience},
    {"StopGlueAmbience", LuaStopGlueAmbience},
    {"StopGlueMusic", LuaStopGlueMusic},
    {"PlayCreditsMusic", LuaPlayCreditsMusic},
    {"ReadFile", LuaScriptFileAccessDenied},
    {"DeleteFile", LuaScriptFileAccessDenied},
    {"AppendToFile", LuaScriptFileAccessDenied},
    {"GetScreenWidth", LuaGetScreenWidth},
    {"GetScreenHeight", LuaGetScreenHeight},
    {"GetCursorPosition", LuaGetCursorPosition},
    {"GetBuildInfo", LuaGetBuildInfo},
    {"GetClientExpansionLevel", LuaGetClientExpansionLevel},
    {"GetNumCharacters", LuaGetNumCharacters},
    {"SetCharSelectModelFrame", LuaSetCharSelectModelFrame},
    {"IsMacClient", LuaIsMacClient},
    {"GetMovieResolution", LuaGetMovieResolution},
    {"IsConsoleActive", openwow::ui::game::detail::LuaApi_IsConsoleActive},
    {"IsStreamingMode", openwow::ui::game::detail::LuaApi_IsStreamingMode},
    {"GetCVar", LuaGetCVar},
    {"SetCVar", LuaSetCVar},
    {"GetCVarBool", LuaGetCVarBool},
    {"GetCVarDefault", LuaGetCVarDefault},
    {"IsStreamingTrial", LuaIsStreamingTrial},
    {"GetServerName", LuaGetServerName},
    {"GetSavedAccountName", LuaGetSavedAccountName},
    {"SetSavedAccountName", LuaSetSavedAccountName},
    {"GetSavedAccountList", LuaGetSavedAccountList},
    {"SetSavedAccountList", LuaSetSavedAccountList},
    {"EULAAccepted", LuaEULAAccepted},
    {"TOSAccepted", LuaTOSAccepted},
    {"TerminationWithoutNoticeAccepted", LuaTerminationWithoutNoticeAccepted},
    {"DefaultServerLogin", LuaDefaultServerLogin},
    {"LaunchURL", LuaLaunchURL},
    {"GetCreditsText", LuaGetCreditsText},
    {"QuitGame", LuaQuitGame},
    {"StopAllSFX", LuaStopAllSFX},
    {"Screenshot", LuaScreenshot},
    {"HideCursor", LuaHideCursor},
    {"ShowCursor", LuaShowCursor},
    {"IsShiftKeyDown", LuaIsShiftKeyDown},
    {"CreateFrame", LuaCreateFrame},
    {"CreateFont", openwow::ui::game::frame_api::LuaCreateFont},
    {"GetNumFrames", LuaGetNumFrames},
    {"EnumerateFrames", LuaEnumerateFrames},
    {"GetFramesRegisteredForEvent", LuaGetFramesRegisteredForEvent},
    {"GetCurrentKeyBoardFocus", LuaGetCurrentKeyBoardFocus},
    {"GetNumRealms", LuaGetNumRealms},
    {"GetRealmInfo", LuaGetRealmInfo},
    {"GetRealmCategories", LuaGetRealmCategories},
    {"RequestRealmList", LuaRequestRealmList},
    {"CancelRealmListQuery", LuaCancelRealmListQuery},
    {"RealmListUpdateRate", LuaRealmListUpdateRate},
    {"ChangeRealm", LuaChangeRealm},
    {"RealmListDialogCancelled", LuaRealmListDialogCancelled},
    {"SetRealmSplitState", LuaSetRealmSplitState},
    {"RequestRealmSplitInfo", LuaRequestRealmSplitInfo},
    {"IsTournamentRealmCategory", LuaIsTournamentRealmCategory},
    {"IsInvalidTournamentRealmCategory", LuaIsInvalidTournamentRealmCategory},
    {"GetCharacterInfo", LuaGetCharacterInfo},
    {"SelectCharacter", LuaSelectCharacter},
    {"EnterWorld", LuaEnterWorld},
    {"GetCharacterListUpdate", LuaGetCharacterListUpdate},
    {"ReadyForAccountDataTimes", LuaReadyForAccountDataTimes},
    {"CreateCharacter", LuaCreateCharacter},
    {"DeclineCharacter", LuaDeclineCharacter},
    {"DeclineName", LuaDeclineName},
    {"DeleteCharacter", LuaDeleteCharacter},
    {"RenameCharacter", LuaRenameCharacter},
    {"GetCharacterCreateFacing", LuaGetCharacterCreateFacing},
    {"GetCharacterSelectFacing", LuaGetCharacterSelectFacing},
    {"SetCharacterCreateFacing", LuaSetCharacterCreateFacing},
    {"SetCharacterSelectFacing", LuaSetCharacterSelectFacing},
    {"SetCharSelectBackground", LuaSetCharSelectBackground},
    {"GetSelectBackgroundModel", LuaGetSelectBackgroundModel},
    {"IsConnectedToServer", LuaIsConnectedToServer},
    {"DisconnectFromServer", LuaDisconnectFromServer},
    {"CancelLogin", LuaCancelLogin},
    {"AcceptEULA", LuaAcceptEULA},
    {"AcceptTOS", LuaAcceptTOS},
    {"AcceptTerminationWithoutNotice", LuaAcceptTerminationWithoutNotice},
    {"AcceptScanning", LuaAcceptScanning},
    {"AcceptContest", LuaAcceptContest},
    {"QuitGameAndRunLauncher", LuaQuitGameAndRunLauncher},
    {"GetNumAddOns", LuaGetNumAddOns},
    {"GetAddOnInfo", LuaGetAddOnInfo},
    {"GetAddOnDependencies", LuaGetAddOnDependencies},
    {"GetAddOnEnableState", LuaGetAddOnEnableState},
    {"EnableAddOn", LuaEnableAddOn},
    {"DisableAddOn", LuaDisableAddOn},
    {"EnableAllAddOns", LuaGlueEnableAllAddOns},
    {"DisableAllAddOns", LuaGlueDisableAllAddOns},
    {"ResetAddOns", LuaResetAddOns},
    {"SaveAddOns", LuaSaveAddOns},
    {"SetAddonVersionCheck", LuaSetAddonVersionCheck},
    {"IsAddonVersionCheckEnabled", LuaIsAddonVersionCheckEnabled},
    {"LaunchAddOnURL", LuaLaunchAddOnURL},
    {"IsWindowsClient", LuaIsWindowsClient},
    {"IsLinuxClient", LuaIsLinuxClient},
    {"IsInvalidLocale", LuaIsInvalidLocale},
    {"GetNumGameAccounts", LuaGetNumGameAccounts},
    {"GetGameAccountInfo", LuaGetGameAccountInfo},
    {"SetGameAccount", LuaSetGameAccount},
    {"GetBillingPlan", LuaGetBillingPlan},
    {"GetBillingTimeRemaining", LuaGetBillingTimeRemaining},
    {"GetBillingTimeRested", LuaGetBillingTimeRested},
    {"GetAvailableRaces", LuaGetAvailableRaces},
    {"GetAvailableClasses", LuaGetAvailableClasses},
    {"GetClassesForRace", LuaGetClassesForRace},
    {"GetNameForRace", LuaGetNameForRace},
    {"GetFactionForRace", LuaGetFactionForRace},
    {"GetSelectedRace", LuaGetSelectedRace},
    {"GetSelectedClass", LuaGetSelectedClass},
    {"GetSelectedSex", LuaGetSelectedSex},
    {"SetSelectedRace", LuaSetSelectedRace},
    {"SetSelectedClass", LuaSetSelectedClass},
    {"SetSelectedSex", LuaSetSelectedSex},
    {"IsRaceClassValid", LuaIsRaceClassValid},
    {"IsRaceClassRestricted", LuaIsRaceClassRestricted},
    {"GetSelectedCategory", LuaGetSelectedCategory},
    {"UpdateCustomizationScene", LuaUpdateCustomizationScene},
    {"UpdateSelectionCustomizationScene", LuaUpdateSelectionCustomizationScene},
    {"UpdateCustomizationBackground", LuaUpdateCustomizationBackground},
    {"CycleCharCustomization", LuaCycleCharCustomization},
    {"RandomizeCharCustomization", LuaRandomizeCharCustomization},
    {"ResetCharCustomize", LuaResetCharCustomize},
    {"GetFacialHairCustomization", LuaGetFacialHairCustomization},
    {"GetHairCustomization", LuaGetHairCustomization},
    {"SetCharCustomizeBackground", LuaSetCharCustomizeBackground},
    {"CustomizeExistingCharacter", LuaCustomizeExistingCharacter},
    {"GetCVarMax", LuaGetCVarMax},
    {"GetCVarMin", LuaGetCVarMin},
    {"RestartGx", LuaRestartGx},
    {"RestoreVideoEffectsDefaults", LuaRestoreVideoEffectsDefaults},
    {"RestoreVideoResolutionDefaults", LuaRestoreVideoResolutionDefaults},
    {"RestoreVideoStereoDefaults", LuaRestoreVideoStereoDefaults},
    {"RunScript", openwow::ui::game::detail::LuaRunScript},
    {"SetClearConfigData", LuaSetClearConfigData},
    {"GetNumDeclensionSets", LuaGetNumDeclensionSets},
    {"ShowContestNotice", LuaShowContestNotice},
    {"ShowEULANotice", LuaShowEULANotice},
    {"ShowScanningNotice", LuaShowScanningNotice},
    {"ShowTOSNotice", LuaShowTOSNotice},
    {"ShowTerminationWithoutNoticeNotice", LuaShowTerminationWithoutNoticeNotice},
    {"ScanDLLStart", LuaScanDLLStart},
    {"ScanDLLContinueAnyway", LuaScanDLLContinueAnyway},
    {"SurveyNotificationDone", LuaSurveyNotificationDone},
    {"PINEntered", LuaPINEntered},
    {"TokenEntered", LuaTokenEntered},
    {"MatrixEntered", LuaMatrixEntered},
    {"MatrixCommit", LuaMatrixCommit},
    {"MatrixRevert", LuaMatrixRevert},
    {"GetMatrixCoordinates", LuaGetMatrixCoordinates},
    {"StatusDialogClick", LuaStatusDialogClick},
    {"PaidChange_GetName", LuaPaidChange_GetName},
    {"GetRandomName", LuaGetRandomName},
    {"PaidChange_GetPreviousRaceIndex", LuaPaidChange_GetPreviousRaceIndex},
    {"PaidChange_GetCurrentRaceIndex", LuaPaidChange_GetCurrentRaceIndex},
    {"PaidChange_GetCurrentClassIndex", LuaPaidChange_GetCurrentClassIndex},
    {"GetCreateBackgroundModel", LuaGetCreateBackgroundModel},
    {"ConsoleExec", LuaConsoleExec},
    {"PatchDownloadProgress", LuaGetPatchDownloadProgress},
    {"PatchDownloadApply", LuaPatchDownloadApply},
    {"PatchDownloadCancel", LuaPatchDownloadCancel},
    {"GetLocale", LuaGetLocale},
    {"GetText", LuaGetText},
    {"IsTrialAccount", LuaIsTrialAccount},
    {"GetAccountExpansionLevel", LuaGetAccountExpansionLevel},
    {"SetUsesToken", LuaSetUsesToken},
    {"GetUsesToken", LuaGetUsesToken},
    {"SortRealms", LuaSortRealms},
    {"SetPreferredInfo", LuaSetPreferredInfo},
    {"GetCVarAbsoluteMax", LuaGetCVarAbsoluteMax},
    {"GetCVarAbsoluteMin", LuaGetCVarAbsoluteMin},
    {"IsSystemSupported", LuaIsSystemSupported},
};

constexpr openwow::ui::LuaIntegerGlobal kGlueLightConstants[] = {
    {"LIGHT_LIVE", 0},
    {"LIGHT_PREVIEW", 1},
};

}

std::vector<openwow::ui::lua::NativeBindingCatalog>
GlueNativeBindingCatalogs() {
  using openwow::ui::lua::BindingScope;
  using openwow::ui::lua::NativeBindingCatalog;
  std::vector<NativeBindingCatalog> catalogs;
  catalogs.push_back(openwow::ui::lua::NativeFunctionCatalog(
      "ui.surfaces.glue.system", BindingScope::kGlue,
      kGlueSystemExpansionBinding));
  catalogs.push_back(openwow::ui::lua::NativeFunctionCatalog(
      "foundation.time", BindingScope::kShared,
      kGlueTimeSystemBindings));
  catalogs.push_back(openwow::ui::lua::NativeFunctionCatalog(
      "ui.surfaces.glue.runtime", BindingScope::kGlue,
      kGluePreFrameScriptBindings));
  catalogs.push_back(openwow::ui::lua::NativeFunctionCatalog(
      "game.account.messages", BindingScope::kGlue,
      kGlueAccountMsgBindings));
  catalogs.push_back(openwow::ui::lua::NativeConstantCatalog(
      "render.glue.lighting", BindingScope::kGlue, kGlueLightConstants));
  return catalogs;
}

}

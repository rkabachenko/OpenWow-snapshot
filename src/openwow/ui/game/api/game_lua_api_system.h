
#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaGameGetTime(lua_State *L);
int LuaGetGameTime(lua_State *L);

int LuaGetFramerate(lua_State *L);
int LuaGetNetStats(lua_State *L);
int LuaGetDebugStats(lua_State *L);
int LuaGetCombatRating(lua_State *L);
int LuaGetCombatRatingBonus(lua_State *L);
int LuaGetDodgeChance(lua_State *L);
int LuaGetParryChance(lua_State *L);
int LuaGetBlockChance(lua_State *L);
int LuaGetShieldBlock(lua_State *L);
int LuaGetRangedCritChance(lua_State *L);
int LuaGetSpellCritChance(lua_State *L);
int LuaGetSpellBonusDamage(lua_State *L);
int LuaGetSpellBonusHealing(lua_State *L);
int LuaGetExpertise(lua_State *L);
int LuaGetExpertisePercent(lua_State *L);
int LuaGetManaRegen(lua_State *L);

int LuaTargetUnit(lua_State *L);
int LuaClearTarget(lua_State *L);
int LuaAssistUnit(lua_State *L);
int LuaFocusUnit(lua_State *L);
int LuaClearFocus(lua_State *L);

int LuaGameGetCursorPosition(lua_State *L);
int LuaSetCursor(lua_State *L);
int LuaResetCursor(lua_State *L);
int LuaAutoEquipCursorItem(lua_State *L);
int LuaDeleteCursorItem(lua_State *L);

int LuaLogout(lua_State *L);
int LuaForceLogout(lua_State *L);
int LuaCancelLogout(lua_State *L);

int LuaOpeningCinematic(lua_State *L);
int LuaGetBillingTimeRested(lua_State *L);
int LuaGetTimeToWellRestedRetail(lua_State *L);
int LuaGetRestState(lua_State *L);
int LuaGetXPExhaustion(lua_State *L);
int LuaIsResting(lua_State *L);
int LuaIsMounted(lua_State *L);
int LuaDismount(lua_State *L);
int LuaIsSwimming(lua_State *L);
int LuaIsFalling(lua_State *L);
int LuaIsFlying(lua_State *L);
int LuaIsStealthed(lua_State *L);
int LuaGetWeaponEnchantInfo(lua_State *L);

int LuaGetBuildInfo(lua_State *L);
int LuaIsWindowsClient(lua_State *L);
int LuaIsMacClient(lua_State *L);
int LuaIsLinuxClient(lua_State *L);
int LuaGetExpansionLevel(lua_State *L);
int LuaApi_IsConsoleActive(lua_State *L);
int LuaApi_IsDebugBuild(lua_State *L);
int LuaApi_IsStreamingMode(lua_State *L);
int LuaApi_RegisterStaticConstants(lua_State *L);
int LuaApi_SetConsoleKey(lua_State *L);
int LuaApi_SetEuropeanNumbers(lua_State *L);
int LuaGetRealmName(lua_State *L);

int LuaGetScriptCPUUsage(lua_State *L);
int LuaResetCPUUsage(lua_State *L);

int LuaIsShiftKeyDown(lua_State *L);
int LuaIsControlKeyDown(lua_State *L);
int LuaIsAltKeyDown(lua_State *L);
int LuaIsModifierKeyDown(lua_State *L);
int LuaIsLeftShiftKeyDown(lua_State *L);
int LuaIsRightShiftKeyDown(lua_State *L);
int LuaIsLeftControlKeyDown(lua_State *L);
int LuaIsRightControlKeyDown(lua_State *L);
int LuaIsLeftAltKeyDown(lua_State *L);
int LuaIsRightAltKeyDown(lua_State *L);

int LuaIsModifiedClick(lua_State *L);
int LuaSetModifiedClick(lua_State *L);
int LuaGetModifiedClick(lua_State *L);

int LuaRunScript(lua_State *L);

int LuaIsLoggedIn(lua_State *L);
int LuaGameMovieFinished(lua_State *L);
int LuaStopCinematic(lua_State *L);
int LuaIsOutOfBounds(lua_State *L);
int LuaScreenshot(lua_State *L);
int LuaMovieRecordingToggle(lua_State *L);
int LuaMovieRecordingCancel(lua_State *L);
int LuaMovieRecordingIsRecording(lua_State *L);
int LuaMovieRecordingIsCompressing(lua_State *L);
int LuaMovieRecordingGetProgress(lua_State *L);
int LuaMovieRecordingGetViewportWidth(lua_State *L);
int LuaMovieRecordingGetAspectRatio(lua_State *L);
int LuaMovieRecordingIsSupported(lua_State *L);
int LuaMovieRecordingIsCodecSupported(lua_State *L);
int LuaMovieRecordingIsCursorRecordingSupported(lua_State *L);
int LuaMovieRecordingMaxLength(lua_State *L);
int LuaMovieRecordingDataRate(lua_State *L);
int LuaMovieRecordingGetTime(lua_State *L);
int LuaMovieRecordingGetMovieFullPath(lua_State *L);
int LuaMovieRecordingSearchUncompressedMovie(lua_State *L);
int LuaMovieRecordingQueueMovieToCompress(lua_State *L);
int LuaMovieRecordingDeleteMovie(lua_State *L);
int LuaMovieRecordingToggleGUI(lua_State *L);

int LuaGetMapInfo(lua_State *L);

int LuaSetPortraitTexture(lua_State *L);
int LuaSetPortraitToTexture(lua_State *L);
int LuaQuit(lua_State *L);
int LuaForceQuit(lua_State *L);
int LuaAcceptResurrect(lua_State *L);
int LuaDeclineResurrect(lua_State *L);
int LuaRepopMe(lua_State *L);
int LuaRetrieveCorpse(lua_State *L);
int LuaGetCorpseRecoveryDelay(lua_State *L);
int LuaGetReleaseTimeRemaining(lua_State *L);
int LuaResurrectGetOfferer(lua_State *L);
int LuaResurrectHasSickness(lua_State *L);
int LuaResurrectHasTimer(lua_State *L);

int LuaRegisterForSave(lua_State *L);
int LuaReloadUI(lua_State *L);
int LuaStuck(lua_State *L);
int LuaCameraZoomIn(lua_State *L);
int LuaCameraZoomOut(lua_State *L);
int LuaGetFramesRegisteredForEvent(lua_State *L);
int LuaGetWaterDetailRetail(lua_State *L);
int LuaGetScreenHeight(lua_State *L);
int LuaGetScreenWidth(lua_State *L);
int LuaInCinematic(lua_State *L);
int LuaRequestTimePlayed(lua_State *L);
int LuaUpdateAddOnCPUUsage(lua_State *L);
int LuaUpdateAddOnMemoryUsage(lua_State *L);
int LuaRetailReleaseDebugCommand(lua_State *L);

}

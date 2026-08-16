
#include "openwow/ui/game/cvar_system.h"

#include "openwow/audio/adapters/ui/sound_cvar_defaults.h"
#include "openwow/core/console.h"
#include "openwow/core/cvar.h"
#include "openwow/core/decimal_parse.h"
#include "openwow/platform/system/os_system_info.h"
#include "openwow/core/screenshot_system.h"
#include "openwow/data/formats/dbc/dbc_table_registry.h"
#include "openwow/data/streaming_init.h"
#include "openwow/debug/client_error_display_cvars.h"
#include "openwow/debug/diagnostics/debug_console.h"
#include "openwow/game/calendar/calendar_system.h"
#include "openwow/game/camera_view_presets.h"
#include "openwow/game/client_config.h"
#include "openwow/game/minimap_terrain.h"
#include "openwow/game/player_name_desc.h"
#include "openwow/game/world_session.h"
#include "openwow/platform/window/system_mouse_speed.h"
#include "openwow/render/m2/m2_cvar_callbacks.h"
#include "openwow/render/world/environment/sky_cvar_callbacks.h"
#include "openwow/ui/game/api/game_lua_api_guild_roster_view.h"
#include "openwow/ui/game/api/game_lua_api_talent.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/script_boolean.h"
#include "openwow/ui/runtime/security/protected_action_gate.h"
#include "openwow/ui/game/secure_execution.h"
#include "openwow/platform/filesystem/filesystem.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/text/ascii.h"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>

namespace openwow::ui::game {

CVarValidationCallbackRegistration::CVarValidationCallbackRegistration(
    CVarSystem& owner, std::string name, const std::uint64_t handle)
    : owner_(&owner), name_(std::move(name)), handle_(handle) {}

CVarValidationCallbackRegistration::~CVarValidationCallbackRegistration() {
  Reset();
}

CVarValidationCallbackRegistration::CVarValidationCallbackRegistration(
    CVarValidationCallbackRegistration&& other) noexcept
    : owner_(std::exchange(other.owner_, nullptr)),
      name_(std::move(other.name_)),
      handle_(std::exchange(other.handle_, 0)) {}

CVarValidationCallbackRegistration&
CVarValidationCallbackRegistration::operator=(
    CVarValidationCallbackRegistration&& other) noexcept {
  if (this != &other) {
    Reset();
    owner_ = std::exchange(other.owner_, nullptr);
    name_ = std::move(other.name_);
    handle_ = std::exchange(other.handle_, 0);
  }
  return *this;
}

void CVarValidationCallbackRegistration::Reset() {
  if (owner_ != nullptr) {
    owner_->RemoveValidationCallback(name_, handle_);
    owner_ = nullptr;
    name_.clear();
    handle_ = 0;
  }
}

namespace {

constexpr std::uint32_t kScriptRegisterClearedFlagMask = 0x30u;
constexpr std::uint32_t kPersistenceScopeMask = 0x30u;
constexpr char kCombatLogRetentionTimeCVarName[] = "combatLogRetentionTime";
constexpr int kDefaultCVarConsoleCategory = CVarSystem::kFallbackConsoleCategory;

constexpr std::array<std::pair<std::string_view, int>, 464>
    kStockConsoleCategories = {{
    {"accountList", 4},
    {"accountName", 4},
    {"accounttype", 4},
    {"addFriendInfoShown", 4},
    {"advancedWatchFrame", 4},
    {"advancedWorldMap", 4},
    {"AllowMultisampleFBO", 1},
    {"alwaysCompareItems", 4},
    {"alwaysShowActionBars", 4},
    {"assistAttack", 4},
    {"asyncHandlerTimeout", 0},
    {"asyncThreadSleep", 0},
    {"auctionDisplayOnCharacter", 4},
    {"autoClearAFK", 4},
    {"autoCompleteResortNamesOnRecency", 4},
    {"autoCompleteUseContext", 4},
    {"autoCompleteWhenEditingFromCenter", 4},
    {"autoDismount", 4},
    {"autoDismountFlying", 4},
    {"autoFilledMultiCastSlots", 4},
    {"autoInteract", 4},
    {"autojoinBGVoice", 4},
    {"autojoinPartyVoice", 4},
    {"autoLootDefault", 4},
    {"autoQuestProgress", 4},
    {"autoQuestWatch", 4},
    {"autoRangedCombat", 4},
    {"autoSelfCast", 4},
    {"autoStand", 4},
    {"autoUnshift", 4},
    {"baseMip", 1},
    {"blockTrades", 4},
    {"bspcache", 1},
    {"buffDurations", 4},
    {"calendarShowBattlegrounds", 4},
    {"calendarShowDarkmoon", 4},
    {"calendarShowLockouts", 4},
    {"calendarShowResets", 4},
    {"calendarShowWeeklyHolidays", 4},
    {"cameraBobbing", 5},
    {"cameraBobbingSmoothSpeed", 5},
    {"cameraCustomViewSmoothing", 5},
    {"cameraDistanceMax", 5},
    {"cameraDistanceMaxFactor", 5},
    {"cameraDistanceMoveSpeed", 5},
    {"cameraDistanceSmoothSpeed", 5},
    {"cameraDive", 5},
    {"cameraFlyingMountHeightSmoothSpeed", 5},
    {"cameraFoVSmoothSpeed", 5},
    {"cameraGroundSmoothSpeed", 5},
    {"cameraHeightIgnoreStandState", 5},
    {"cameraHeightSmoothSpeed", 5},
    {"cameraPitchMoveSpeed", 5},
    {"cameraPitchSmoothMax", 5},
    {"cameraPitchSmoothMin", 5},
    {"cameraPitchSmoothSpeed", 5},
    {"cameraPivot", 5},
    {"cameraPivotDXMax", 5},
    {"cameraPivotDYMin", 5},
    {"cameraSavedDistance", 5},
    {"cameraSavedPitch", 5},
    {"cameraSavedVehicleDistance", 5},
    {"camerasmooth", 5},
    {"cameraSmoothPitch", 5},
    {"cameraSmoothStyle", 5},
    {"cameraSmoothTimeMax", 5},
    {"cameraSmoothTimeMin", 5},
    {"cameraSmoothTrackingStyle", 5},
    {"cameraSmoothYaw", 5},
    {"cameraSubmergeFinalPitch", 5},
    {"cameraSubmergePitch", 5},
    {"cameraSurfaceFinalPitch", 5},
    {"cameraSurfacePitch", 5},
    {"cameraTargetSmoothSpeed", 5},
    {"cameraTerrainTilt", 5},
    {"cameraTerrainTiltTimeMax", 5},
    {"cameraTerrainTiltTimeMin", 5},
    {"cameraView", 5},
    {"cameraViewBlendStyle", 5},
    {"cameraWaterCollision", 5},
    {"cameraYawMoveSpeed", 5},
    {"cameraYawSmoothMax", 5},
    {"cameraYawSmoothMin", 5},
    {"cameraYawSmoothSpeed", 5},
    {"ChatAmbienceVolume", 7},
    {"chatBubbles", 4},
    {"chatBubblesParty", 4},
    {"chatMouseScroll", 4},
    {"ChatMusicVolume", 7},
    {"ChatSoundVolume", 7},
    {"chatStyle", 4},
    {"checkAddonVersion", 4},
    {"CinematicJoystick", 5},
    {"colorblindMode", 4},
    {"colorChatNamesByClass", 4},
    {"CombatDamage", 4},
    {"CombatHealing", 4},
    {"combatLogOn", 4},
    {"CombatLogPeriodicSpells", 4},
    {"combatLogRetentionTime", 4},
    {"combatTextFloatMode", 4},
    {"componentCompress", 0},
    {"componentTextureLevel", 0},
    {"componentThread", 0},
    {"consolidateBuffs", 4},
    {"conversationMode", 4},
    {"converted", 4},
    {"currencyTokensBackpack1", 4},
    {"currencyTokensBackpack2", 4},
    {"currencyTokensUnused1", 4},
    {"currencyTokensUnused2", 4},
    {"dbCompress", 5},
    {"decorateAccountName", 6},
    {"deselectOnClick", 4},
    {"DesktopGamma", 1},
    {"displayFreeBagSlots", 4},
    {"displayWorldPVPObjectives", 4},
    {"dontShowEquipmentSetsOnItems", 4},
    {"enableCombatText", 4},
    {"EnableMicrophone", 5},
    {"EnableMultiTouch", 4},
    {"enablePVPNotifyAFK", 4},
    {"EnableVoiceChat", 5},
    {"enableWowMouse", 5},
    {"environmentDetail", 1},
    {"equipmentManager", 4},
    {"ErrorFilter", 0},
    {"ErrorLevelMax", 0},
    {"ErrorLevelMin", 0},
    {"Errors", 0},
    {"expansionMovie", 4},
    {"extShadowQuality", 1},
    {"farclip", 1},
    {"farClipOverride", 1},
    {"fctAllSpellMechanics", 4},
    {"fctAuras", 4},
    {"fctCombatState", 4},
    {"fctComboPoints", 4},
    {"fctDamageReduction", 4},
    {"fctDodgeParryMiss", 4},
    {"fctEnergyGains", 4},
    {"fctFriendlyHealers", 4},
    {"fctHonorGains", 4},
    {"fctLowManaHealth", 4},
    {"fctPeriodicEnergyGains", 4},
    {"fctReactives", 4},
    {"fctRepChanges", 4},
    {"fctSpellMechanics", 4},
    {"fctSpellMechanicsOther", 4},
    {"ffx", 1},
    {"ffxDeath", 1},
    {"ffxGlow", 1},
    {"ffxNetherWorld", 1},
    {"ffxRectangle", 1},
    {"ffxSpecial", 1},
    {"fixedFunction", 1},
    {"FixVBOBug", 1},
    {"flaggedTutorials", 4},
    {"footstepBias", 1},
    {"FootstepSounds", 5},
    {"forceEnglishNames", 5},
    {"friendsSmallView", 4},
    {"friendsViewButtons", 4},
    {"fullSizeFocusFrame", 4},
    {"g_accountUsesToken", 4},
    {"gameTip", 5},
    {"Gamma", 1},
    {"GLFaster", 1},
    {"groundEffectDensity", 1},
    {"groundEffectDist", 1},
    {"guildMemberNotify", 4},
    {"guildRecruitmentChannel", 4},
    {"guildShowOffline", 4},
    {"gxApi", 1},
    {"gxAspect", 1},
    {"gxColorBits", 1},
    {"gxCursor", 1},
    {"gxDepthBits", 1},
    {"gxFixLag", 1},
    {"gxMaximize", 1},
    {"gxMultisample", 1},
    {"gxMultisampleQuality", 1},
    {"gxOverride", 1},
    {"gxRefresh", 1},
    {"gxResolution", 1},
    {"gxStereoConvergence", 1},
    {"gxStereoEnabled", 1},
    {"gxStereoSeparation", 1},
    {"gxTextureCacheSize", 1},
    {"gxTripleBuffer", 1},
    {"gxVSync", 1},
    {"gxWindow", 1},
    {"heapAllocTracking", 4},
    {"hidePartyInRaid", 4},
    {"horizonFarclipScale", 1},
    {"horizonNearclipScale", 1},
    {"hwDetect", 1},
    {"hwPCF", 1},
    {"InboundChatVolume", 5},
    {"iTunesRemoteFeedback", 7},
    {"iTunesTrackDisplay", 7},
    {"Joystick", 5},
    {"lastCharacterIndex", 4},
    {"lastTalkedToGM", 4},
    {"lfdCollapsedHeaders", 4},
    {"lfdSelectedDungeons", 4},
    {"lfgAutoFill", 4},
    {"lfgAutoJoin", 4},
    {"lfgSelectedRoles", 4},
    {"locale", 5},
    {"lockActionBars", 4},
    {"lod", 1},
    {"lootUnderMouse", 4},
    {"M2BatchDoodads", 1},
    {"M2BatchParticles", 1},
    {"M2Faster", 1},
    {"M2FasterDebug", 1},
    {"M2ForceAdditiveParticleSort", 1},
    {"M2UseClipPlanes", 1},
    {"M2UseThreads", 1},
    {"M2UseZFill", 1},
    {"mapObjLightLOD", 1},
    {"mapQuestDifficulty", 4},
    {"mapShadows", 1},
    {"maxFPS", 1},
    {"maxFPSBk", 1},
    {"MaxLights", 1},
    {"minimapInsideZoom", 4},
    {"minimapPortalMax", 4},
    {"minimapTrackedInfo", 4},
    {"minimapZoom", 4},
    {"miniWorldMap", 4},
    {"mouseInvertPitch", 5},
    {"mouseInvertYaw", 5},
    {"mouseSpeed", 4},
    {"movie", 4},
    {"MovieRecordingAutoCompress", 1},
    {"MovieRecordingCompression", 1},
    {"MovieRecordingCursor", 1},
    {"MovieRecordingForceEnable", 1},
    {"MovieRecordingFramerate", 1},
    {"MovieRecordingGetTexImage", 1},
    {"MovieRecordingGUI", 1},
    {"MovieRecordingIcon", 1},
    {"MovieRecordingPath", 1},
    {"MovieRecordingQuality", 1},
    {"MovieRecordingRecover", 1},
    {"MovieRecordingSound", 1},
    {"MovieRecordingWidth", 1},
    {"movieSubtitle", 4},
    {"MultiTouchAutoRunInvertY", 4},
    {"MultiTouchAutoRunSensitivityX", 4},
    {"MultiTouchAutoRunSensitivityY", 4},
    {"nameplateAllowOverlap", 4},
    {"nameplateShowEnemies", 4},
    {"nameplateShowEnemyGuardians", 4},
    {"nameplateShowEnemyPets", 4},
    {"nameplateShowEnemyTotems", 4},
    {"nameplateShowFriendlyGuardians", 4},
    {"nameplateShowFriendlyPets", 4},
    {"nameplateShowFriendlyTotems", 4},
    {"nameplateShowFriends", 4},
    {"nearclip", 1},
    {"NvidiaViewportFix", 1},
    {"objectFade", 1},
    {"objectFadeZFill", 1},
    {"ObjectSelectionCircle", 0},
    {"occlusion", 1},
    {"OutboundChatVolume", 5},
    {"particleDensity", 1},
    {"partyBackgroundOpacity", 4},
    {"partyStatusText", 4},
    {"pathDistTol", 4},
    {"pendingInviteInfoShown", 4},
    {"PetMeleeDamage", 4},
    {"PetSpellDamage", 4},
    {"petStatusText", 4},
    {"playerStatLeftDropdown", 4},
    {"playerStatRightDropdown", 4},
    {"playerStatusText", 4},
    {"POIShiftComplete", 0},
    {"portal", 6},
    {"predictedHealth", 4},
    {"predictedPower", 4},
    {"preferredFullscreenMode", 1},
    {"previewTalents", 4},
    {"processAffinityMask", 0},
    {"profanityFilter", 4},
    {"projectedTextures", 1},
    {"PushToTalkButton", 5},
    {"PushToTalkSound", 4},
    {"questFadingDisable", 4},
    {"questLogCollapseFilter", 4},
    {"questPOI", 4},
    {"readContest", 4},
    {"readEULA", 4},
    {"readScanning", 4},
    {"readTerminationWithoutNotice", 4},
    {"readTOS", 4},
    {"realmList", 6},
    {"realmListbn", 6},
    {"realmName", 6},
    {"removeChatDelay", 4},
    {"rotateMinimap", 4},
    {"screenEdgeFlash", 4},
    {"screenshotFormat", 1},
    {"screenshotQuality", 1},
    {"scriptErrors", 5},
    {"scriptProfile", 5},
    {"secureAbilityToggle", 4},
    {"serverAlert", 6},
    {"serviceTypeFilter", 4},
    {"shadowCull", 5},
    {"shadowInstancing", 5},
    {"shadowLevel", 1},
    {"shadowLOD", 1},
    {"shadowScissor", 5},
    {"ShowAllSpellRanks", 4},
    {"showArenaEnemyCastbar", 4},
    {"showArenaEnemyFrames", 4},
    {"showArenaEnemyPets", 4},
    {"showBattlefieldMinimap", 4},
    {"showCastableBuffs", 4},
    {"showCastableDebuffs", 4},
    {"ShowClassColorInNameplate", 4},
    {"showClock", 4},
    {"showDispelDebuffs", 4},
    {"ShowErrors", 0},
    {"showfootprintparticles", 1},
    {"showfootprints", 1},
    {"showGameTips", 5},
    {"showItemLevel", 4},
    {"showKeyring", 4},
    {"showLootSpam", 4},
    {"showNewbieTips", 4},
    {"showPartyBackground", 4},
    {"showPartyPets", 4},
    {"showQuestObjectivesOnMap", 4},
    {"showQuestTrackingTooltips", 4},
    {"showRaidRange", 4},
    {"showTargetCastbar", 4},
    {"showTargetOfTarget", 4},
    {"showTimestamps", 4},
    {"showToastBroadcast", 4},
    {"showToastConversation", 4},
    {"showToastFriendRequest", 4},
    {"showToastOffline", 4},
    {"showToastOnline", 4},
    {"showToastWindow", 4},
    {"showTokenFrame", 4},
    {"showTokenFrameHonor", 4},
    {"showToolsUI", 4},
    {"showTutorials", 4},
    {"showVKeyCastbar", 4},
    {"SkyCloudLOD", 1},
    {"Sound_AmbienceVolume", 7},
    {"Sound_ChaosMode", 7},
    {"Sound_DSPBufferSize", 7},
    {"Sound_EnableAllSound", 7},
    {"Sound_EnableAmbience", 7},
    {"Sound_EnableArmorFoleySoundForOthers", 7},
    {"Sound_EnableArmorFoleySoundForSelf", 7},
    {"Sound_EnableDSPEffects", 7},
    {"Sound_EnableEmoteSounds", 7},
    {"Sound_EnableErrorSpeech", 7},
    {"Sound_EnableHardware", 7},
    {"Sound_EnableMixMode2", 7},
    {"Sound_EnableMode2", 7},
    {"Sound_EnableMusic", 7},
    {"Sound_EnablePetSounds", 7},
    {"Sound_EnableReverb", 7},
    {"Sound_EnableSFX", 7},
    {"Sound_EnableSoftwareHRTF", 7},
    {"Sound_EnableSoundWhenGameIsInBG", 7},
    {"Sound_ListenerAtCharacter", 7},
    {"Sound_MasterVolume", 7},
    {"Sound_MaxCacheableSizeInBytes", 7},
    {"Sound_MaxCacheSizeInBytes", 7},
    {"Sound_MusicVolume", 7},
    {"Sound_NumChannels", 7},
    {"Sound_OutputDriverIndex", 7},
    {"Sound_OutputDriverName", 7},
    {"Sound_OutputQuality", 7},
    {"Sound_SFXVolume", 7},
    {"Sound_VoiceChatInputDriverIndex", 7},
    {"Sound_VoiceChatInputDriverName", 7},
    {"Sound_VoiceChatOutputDriverIndex", 7},
    {"Sound_VoiceChatOutputDriverName", 7},
    {"Sound_ZoneMusicNoDelay", 7},
    {"SoundMemoryCache", 7},
    {"spamFilter", 4},
    {"specular", 1},
    {"spellEffectLevel", 1},
    {"SplineOpt", 0},
    {"StartTalkingDelay", 5},
    {"StartTalkingTime", 5},
    {"statusTextPercentage", 4},
    {"stopAutoAttackOnTargetChange", 4},
    {"StopTalkingDelay", 5},
    {"StopTalkingTime", 5},
    {"synchronizeSettings", 5},
    {"taintLog", 5},
    {"talentFrameShown", 4},
    {"targetOfTargetMode", 4},
    {"targetStatusText", 4},
    {"terrainAlphaBitDepth", 1},
    {"texLodBias", 1},
    {"textureCacheSize", 1},
    {"textureFilteringMode", 1},
    {"threatPlaySounds", 4},
    {"threatShowNumeric", 4},
    {"threatWarning", 4},
    {"threatWorldText", 4},
    {"timeMgrAlarmEnabled", 4},
    {"timeMgrAlarmMessage", 4},
    {"timeMgrAlarmTime", 4},
    {"timeMgrUseLocalTime", 4},
    {"timeMgrUseMilitaryTime", 4},
    {"toastDuration", 4},
    {"trackedAchievements", 4},
    {"trackedQuests", 4},
    {"trackerFilter", 4},
    {"trackerSorting", 4},
    {"UberTooltips", 4},
    {"UIFaster", 1},
    {"uiScale", 4},
    {"unitHighlights", 4},
    {"UnitNameEnemyGuardianName", 4},
    {"UnitNameEnemyPetName", 4},
    {"UnitNameEnemyPlayerName", 4},
    {"UnitNameEnemyTotemName", 4},
    {"UnitNameFriendlyGuardianName", 4},
    {"UnitNameFriendlyPetName", 4},
    {"UnitNameFriendlyPlayerName", 4},
    {"UnitNameFriendlyTotemName", 4},
    {"UnitNameNonCombatCreatureName", 4},
    {"UnitNameNPC", 4},
    {"UnitNameOwn", 4},
    {"UnitNamePlayerGuild", 4},
    {"UnitNamePlayerPVPTitle", 4},
    {"useDesktopMouseSpeed", 4},
    {"useEnglishAudio", 5},
    {"UseNVShaders", 1},
    {"UsePboSubImage", 1},
    {"UsePboSubImageZeroOffset", 1},
    {"useUiScale", 4},
    {"useWeatherShaders", 5},
    {"videoOptionsVersion", 1},
    {"violenceLevel", 4},
    {"VoiceActivationSensitivity", 5},
    {"VoiceChatMode", 5},
    {"VoiceChatSelfMute", 5},
    {"watchFrameBaseAlpha", 4},
    {"watchFrameIgnoreCursor", 4},
    {"watchFrameState", 4},
    {"watchFrameWidth", 4},
    {"waterLOD", 1},
    {"weatherDensity", 5},
    {"wholeChatWindowClickable", 4},
    {"widescreen", 1},
    {"windowResizeLock", 1},
    {"worldMapOpacity", 4},
    {"worldPoolUsage", 1},
    {"xpBarText", 4},
    }};

std::optional<int> StockConsoleCategory(std::string_view name) {
  for (const auto& [stock_name, category] : kStockConsoleCategories) {
    if (openwow::text::EqualsIgnoreCaseAscii(stock_name, name)) {
      return category;
    }
  }
  return std::nullopt;
}

std::string HandleCVarConsoleCommand(const std::string& registered_name,
                                     std::string_view raw_args) {
  while (!raw_args.empty() && raw_args.front() == ' ') {
    raw_args.remove_prefix(1);
  }

  auto& cvars = CVarSystem::Instance();
  const auto snapshot = cvars.GetCVarSnapshot(registered_name);
  if (!snapshot.has_value()) {
    return {};
  }

  if (!raw_args.empty()) {
    if (HasFlag(snapshot->flags, CVarFlags::ConsoleReadOnly)) {
      openwow::core::ida::ConsoleAddLine(
          snapshot->registered_name + " is read only.",
          openwow::core::ida::COLOR_DEFAULT);
      return {};
    }

    CVarSystem::SetRegisteredValueOptions options;

    options.populate_startup_if_missing = true;
    (void)cvars.SetRegisteredCVarValue(snapshot->registered_name,
                                      std::string(raw_args), options);
    return {};
  }

  const auto write_line = [](std::string line) {
    openwow::core::ida::ConsoleAddLine(std::move(line),
                                      openwow::core::ida::COLOR_DEFAULT);
  };
  write_line("CVar \"" + snapshot->registered_name + "\" is \"" +
             snapshot->value + "\"");
  if (snapshot->has_default_value &&
      !openwow::text::EqualsIgnoreCaseAscii(snapshot->value,
                                            snapshot->default_value)) {
    write_line("  default value \"" + snapshot->default_value + "\"");
  }
  if (snapshot->has_startup_value &&
      !openwow::text::EqualsIgnoreCaseAscii(snapshot->value,
                                            snapshot->startup_value)) {
    write_line("  reset value \"" + snapshot->startup_value + "\"");
  }
  if (snapshot->has_pending_value &&
      !openwow::text::EqualsIgnoreCaseAscii(snapshot->value,
                                            snapshot->pending_value)) {
    write_line("  pending value \"" + snapshot->pending_value + "\"");
  }
  return {};
}

bool FfxEnabledValidationCallback(const std::string&,
                                  const std::string&,
                                  const std::string& new_value) {

  openwow::core::ida::ConsoleAddLine(std::atoi(new_value.c_str()) != 0
                                        ? "enabled"
                                        : "disabled",
                                    openwow::core::ida::COLOR_DEFAULT);
  return true;
}

bool ShadowLevelValidationCallback(const std::string&,
                                   const std::string&,
                                   const std::string& new_value) {

  const auto parsed = std::bit_cast<std::int32_t>(
      openwow::core::ParseSignedDecimalLikeSub76F0D0(new_value));
  if (parsed > 1) {
    openwow::core::ida::ConsoleAddLine(
        "Shadow mip level must be in range 0 - 1.",
        openwow::core::ida::COLOR_DEFAULT);
    return false;
  }

  openwow::core::ida::ConsoleAddLine(
      "Shadow mip level changed upon restart.",
      openwow::core::ida::COLOR_DEFAULT);
  return true;
}

bool TraceDirtyCVarsEnabled() {
  static const bool enabled = [] {
    const char* value = std::getenv("OPENWOW_CVAR_TRACE_DIRTY");
    return value != nullptr && *value != '\0' && std::string_view(value) != "0";
  }();
  return enabled;
}

void TraceDirtyCVar(const std::string_view name) {
  if (!TraceDirtyCVarsEnabled()) {
    return;
  }

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "CVarSystem: dirty value changed: " + std::string(name));
}

std::uint32_t PersistenceScopeBits(const CVarSerializationScope scope) {
  return static_cast<std::uint32_t>(scope);
}

bool ParseCalendarFilterEnabled(std::string_view value) {
  return openwow::core::ParseSignedDecimalLikeSub76F0D0(value) != 0u;
}

std::string FormatSignedIntCVarValue(const int value) {
  std::array<char, 32> buffer{};
  const auto [ptr, error] = std::to_chars(buffer.data(),
                                          buffer.data() + buffer.size(),
                                          value);
  if (error != std::errc{}) {
    return "0";
  }

  return std::string(buffer.data(), ptr);
}

void SyncActiveCombatLogRetentionFromCVar() {
  auto &cvars = CVarSystem::Instance();
  if (!cvars.Exists(kCombatLogRetentionTimeCVarName)) {
    return;
  }

  auto* const manager = runtime::WorldUiRuntimeContext::FromActiveLua();
  if (auto* const session = manager != nullptr ? manager->world_session() : nullptr;
      session != nullptr) {
    session->combat_log().SetRetentionTime(
        static_cast<float>(cvars.GetCVarInt(kCombatLogRetentionTimeCVarName)));
  }
}

void InstallCombatLogRetentionCallback(CVarSystem &cvars) {
  static std::uint32_t callback_handle = 0;

  if (callback_handle != 0) {
    cvars.RemoveCallback(kCombatLogRetentionTimeCVarName, callback_handle);
  }

  callback_handle = cvars.AddCallback(
      kCombatLogRetentionTimeCVarName,
      [](const std::string &, const std::string &) {
        SyncActiveCombatLogRetentionFromCVar();
      });

  SyncActiveCombatLogRetentionFromCVar();
}

void SyncCalendarVisibilityFiltersFromCVars(CVarSystem &cvars) {
  openwow::game::CalendarVisibilityFilters filters;
  filters.show_weekly_holidays =
      ParseCalendarFilterEnabled(cvars.GetCVar("calendarShowWeeklyHolidays"));
  filters.show_darkmoon = ParseCalendarFilterEnabled(cvars.GetCVar("calendarShowDarkmoon"));
  filters.show_battleground_holidays =
      ParseCalendarFilterEnabled(cvars.GetCVar("calendarShowBattlegrounds"));
  filters.show_lockouts = ParseCalendarFilterEnabled(cvars.GetCVar("calendarShowLockouts"));
  filters.show_resets = ParseCalendarFilterEnabled(cvars.GetCVar("calendarShowResets"));
  openwow::game::CalendarSystem::Get().SetVisibilityFilters(filters);
}

void InstallCalendarVisibilityFilterCallbacks(CVarSystem &cvars) {
  static bool installed = false;
  if (!installed) {
    const auto on_changed = [&cvars](const std::string &, const std::string &) {
      SyncCalendarVisibilityFiltersFromCVars(cvars);
    };
    cvars.AddCallback("calendarShowWeeklyHolidays", on_changed);
    cvars.AddCallback("calendarShowDarkmoon", on_changed);
    cvars.AddCallback("calendarShowBattlegrounds", on_changed);
    cvars.AddCallback("calendarShowLockouts", on_changed);
    cvars.AddCallback("calendarShowResets", on_changed);
    installed = true;
  }

  SyncCalendarVisibilityFiltersFromCVars(cvars);
}

std::uint32_t ParseMscrtAtol32(std::string_view text) {
  if (text.empty()) {
    return 0;
  }

  errno = 0;
  std::string owned(text);
  char *end = nullptr;
  long long parsed = std::strtoll(owned.c_str(), &end, 10);
  if (end == owned.c_str()) {
    return 0;
  }

  if (parsed < std::numeric_limits<std::int32_t>::min()) {
    parsed = std::numeric_limits<std::int32_t>::min();
  } else if (parsed > std::numeric_limits<std::int32_t>::max()) {
    parsed = std::numeric_limits<std::int32_t>::max();
  }

  return static_cast<std::uint32_t>(static_cast<std::int32_t>(parsed));
}

CVarValidationCallback PlayerNameDisplayFlagCallback(const std::uint32_t flag) {
  return [flag](const std::string &, const std::string &,
                const std::string &new_value) {
    openwow::game::PlayerName_SetDisplayFlag(
        flag, ParseMscrtAtol32(new_value) != 0);
    return true;
  };
}

std::uint32_t GetProcessAffinityMaskLimit() {
  auto &detector = openwow::core::OsSystemInfoDetector::Instance();
  detector.Init();

  std::uint32_t processor_count = detector.GetInfo().processorCount;
  if (processor_count == 0) {
    processor_count = 1;
  }

  const std::uint32_t shift = (32u - processor_count) & 31u;
  return 0xFFFFFFFFu >> shift;
}

const char *GetSoundEnableDspEffectsDefaultValue() {
  auto &detector = openwow::core::OsSystemInfoDetector::Instance();
  detector.Init();
  return detector.GetInfo().processorCount > 1 ? "1" : "0";
}

std::string GetCurrentGameUiLocaleCode() {
  std::string locale = openwow::game::ClientConfig::Get().GetLocale();
  if (locale.empty() || locale == "****") {
    locale = "enUS";
  }
  return locale;
}

const char *GetAutoInteractDefaultValue() {
  return GetCurrentGameUiLocaleCode() == "koKR" ? "0" : "1";
}

const char *GetTimeMgrUseMilitaryTimeDefaultValue() {
  const int locale_delta =
      openwow::data::FindLocaleRingIndexOrEnUSFallback(GetCurrentGameUiLocaleCode().c_str()) - 2;
  if (locale_delta == 0 || locale_delta == 3 || locale_delta == 8) {
    return "0";
  }
  return "1";
}

bool ProcessAffinityMaskValidationCallback(const std::string &, const std::string &,
                                           const std::string &new_value) {
  const std::uint32_t requested_mask = ParseMscrtAtol32(new_value);
  const std::uint32_t max_mask = GetProcessAffinityMaskLimit();
  if (requested_mask <= max_mask) {
    return true;
  }

  openwow::core::ida::ConsoleLog(
      "Specified mask %08x is greater than the maximum allowable value of %08x", requested_mask,
      max_mask);
  return false;
}

bool MouseSpeedValidationCallback(const std::string &, const std::string &,
                                  const std::string &new_value) {
  return openwow::platform::SystemMouseSpeedController::Instance().ApplyCVarValue(new_value);
}

bool ScreenshotFormatValidationCallback(const std::string &, const std::string &,
                                        const std::string &new_value) {
  openwow::core::ScreenshotSystem::Instance().SetFormatCVarValue(new_value);
  return true;
}

bool ScreenshotQualityValidationCallback(const std::string &, const std::string &,
                                         const std::string &new_value) {
  openwow::core::ScreenshotSystem::Instance().SetQualityCVarValue(new_value);
  return true;
}

bool ParticleDensityValidationCallback(const std::string &, const std::string &,
                                       const std::string &new_value) {
  const auto density =
      static_cast<float>(openwow::core::ParseFloatLikeSub76FB80(new_value));

  if (!(density >= 0.1f && density <= 1.0f)) {
    openwow::core::ida::ConsoleAddLine("Value must be between 0.1 and 1.0.",
                                       openwow::core::ida::COLOR_DEFAULT);
    return false;
  }

  return true;
}

bool ValidateCameraFloatRange(const std::string& new_value,
                              const double min_value,
                              const double max_value) {
  const double value = openwow::core::ParseFloatLikeSub76FB80(new_value);
  if (value >= min_value && value <= max_value) {
    return true;
  }

  openwow::core::ida::ConsoleLogColored(
      "Value out of range (%f - %f)\n",
      openwow::core::ida::COLOR_DEFAULT, min_value, max_value);
  return false;
}

bool CameraRate360ValidationCallback(const std::string&, const std::string&,
                                     const std::string& new_value) {
  return ValidateCameraFloatRange(new_value, 0.1000000003926438, 360.0);
}

bool CameraMoveSpeedValidationCallback(const std::string&, const std::string&,
                                       const std::string& new_value) {
  return ValidateCameraFloatRange(new_value, 0.002777777845039964, 50.0);
}

bool CameraSmoothingStyleValidationCallback(const std::string&,
                                            const std::string&,
                                            const std::string& new_value) {
  return ValidateCameraFloatRange(new_value, 0.0, 5.0);
}

bool CameraTimeRangeValidationCallback(const std::string&, const std::string&,
                                       const std::string& new_value) {
  return ValidateCameraFloatRange(new_value, 0.001000000047497451, 300.0);
}

bool CameraPitchDegreesValidationCallback(const std::string&,
                                          const std::string&,
                                          const std::string& new_value) {
  constexpr double kRadiansToDegrees = 57.29578;
  constexpr float kRetailPitchLimitRadians = 1.5533429f;
  return ValidateCameraFloatRange(
      new_value,
      -static_cast<double>(kRetailPitchLimitRadians) * kRadiansToDegrees,
      static_cast<double>(kRetailPitchLimitRadians) * kRadiansToDegrees);
}

bool CameraDistanceValidationCallback(const std::string&, const std::string&,
                                      const std::string& new_value) {
  return ValidateCameraFloatRange(new_value, 0.0, 50.0);
}

bool CameraYawDegreesValidationCallback(const std::string&, const std::string&,
                                        const std::string& new_value) {
  return ValidateCameraFloatRange(new_value, 0.0, 360.0);
}

bool CameraViewValidationCallback(const std::string&, const std::string&,
                                  const std::string& new_value) {
  return ValidateCameraFloatRange(new_value, 0.0, 7.0);
}

}

CVarSystem &CVarSystem::Instance() {
  static CVarSystem instance;
  return instance;
}

CVarSystem::EntryIterator CVarSystem::FindEntryLocked(std::string_view name) {
  return cvars_.find(FoldLookupKey(name));
}

CVarSystem::ConstEntryIterator CVarSystem::FindEntryLocked(std::string_view name) const {
  return cvars_.find(FoldLookupKey(name));
}

std::string CVarSystem::FoldLookupKey(const std::string_view name) {
  std::string key;
  key.reserve(name.size());
  for (const unsigned char ch : name) {
    key.push_back(static_cast<char>(ch >= 'A' && ch <= 'Z'
                                        ? ch + ('a' - 'A')
                                        : ch));
  }
  return key;
}

void CVarSystem::RegisterConsoleCommandForEntry(
    const std::string& name, const std::string& description,
    const int console_category) {
  auto& console = openwow::debug::DebugConsole::Get();
  const std::uint64_t registration_id = console.RegisterRawCommand(
      name, description,
      [name](const std::string_view raw_args) {
        return HandleCVarConsoleCommand(name, raw_args);
      },
      description, console_category);

  bool retained = false;
  {
    std::lock_guard lock(mutex_);
    const auto it = FindEntryLocked(name);
    if (it != cvars_.end() &&
        it->second.console_command_registration_id == 0) {
      it->second.console_command_registration_id = registration_id;
      retained = true;
    }
  }

  if (!retained) {
    (void)console.UnregisterCommandIfCurrent(name, registration_id);
  }
}

void CVarSystem::RegisterCVar(const std::string &name, const std::string &default_value,
                              CVarFlags flags, const std::string &description, float min_value,
                              float max_value, const int console_category) {
  RegisterNativeCVar(name, default_value, flags, description, {}, min_value,
                     max_value, console_category);
}

void CVarSystem::RegisterNativeCVar(
    const std::string &name, const std::string &default_value,
    CVarFlags flags, const std::string &description,
    CVarValidationCallback validation_callback, const float min_value,
    const float max_value, const int console_category_argument) {
  if (name.empty()) {
    return;
  }

  const int console_category =
      StockConsoleCategory(name).value_or(console_category_argument);

  bool reconcile = false;
  bool register_console_command = false;
  {
    std::lock_guard lock(mutex_);
    flags = flags | CVarFlags::Registered;
    const std::string key = FoldLookupKey(name);
    auto it = cvars_.find(key);
    if (it == cvars_.end()) {
      CVarEntry entry;
      entry.registered_name = name;
      entry.value = default_value;
      entry.default_value = default_value;
      entry.has_default_value = true;
      entry.flags = flags;
      entry.description = description;
      entry.min_value = min_value;
      entry.max_value = max_value;
      entry.has_limits = (min_value != 0.0f || max_value != 0.0f);
      entry.is_native_registered = true;
      entry.console_category = console_category;
      entry.validation_callback = std::move(validation_callback);
      reconcile = static_cast<bool>(entry.validation_callback);
      cvars_.emplace(key, std::move(entry));
      stable_keys_.push_back(key);
      register_console_command = true;
    } else {

      auto &entry = it->second;
      const bool was_registered =
          HasFlag(entry.flags, CVarFlags::Registered);
      const std::uint32_t preserved_flags =
          static_cast<std::uint32_t>(entry.flags) &
          ~kScriptRegisterClearedFlagMask;
      if (!entry.has_startup_value) {
        entry.startup_value = default_value;
        entry.has_startup_value = true;
      }
      if (!entry.has_default_value) {
        entry.default_value = default_value;
        entry.has_default_value = true;
      }
      entry.flags = static_cast<CVarFlags>(
          preserved_flags | static_cast<std::uint32_t>(flags));
      entry.description = description;
      entry.min_value = min_value;
      entry.max_value = max_value;
      entry.has_limits = (min_value != 0.0f || max_value != 0.0f);
      entry.is_native_registered = true;
      if (!was_registered) {
        entry.console_category = console_category;
      }
      register_console_command = !was_registered;
      if (validation_callback) {
        entry.validation_callback = std::move(validation_callback);
        entry.validation_callback_handle = 0;
        entry.validation_reconciled = false;
        reconcile = true;
      }
    }
  }

  if (reconcile) {
    (void)ReconcileValueAgainstValidationCallback(name);
  }
  if (register_console_command) {
    RegisterConsoleCommandForEntry(name, description, console_category);
  }
}

void CVarSystem::RegisterScriptCVar(const std::string &name, const std::string &initial_value) {
  if (name.empty()) {
    return;
  }

  bool register_console_command = false;
  {
    std::lock_guard lock(mutex_);
    const std::string key = FoldLookupKey(name);
    auto it = FindEntryLocked(name);
    if (it == cvars_.end()) {
      CVarEntry entry;
      entry.registered_name = name;
      entry.value = initial_value;
      entry.startup_value = initial_value;
      entry.has_startup_value = true;
      entry.flags = CVarFlags::Registered | CVarFlags::NoSave;
      cvars_.emplace(key, std::move(entry));
      stable_keys_.push_back(key);
      register_console_command = true;
    } else {
      auto &entry = it->second;
      const bool was_registered =
          HasFlag(entry.flags, CVarFlags::Registered);
      const std::uint32_t preserved_flags =
          static_cast<std::uint32_t>(entry.flags) &
          ~kScriptRegisterClearedFlagMask;
      entry.flags = static_cast<CVarFlags>(
          preserved_flags |
          static_cast<std::uint32_t>(CVarFlags::Registered) |
          static_cast<std::uint32_t>(CVarFlags::NoSave));
      entry.validation_callback = {};
      entry.validation_callback_handle = 0;
      entry.validation_reconciled = false;

      if (!entry.has_startup_value) {
        entry.startup_value = initial_value;
        entry.has_startup_value = true;
      }
      if (!entry.has_default_value) {
        entry.default_value = initial_value;
        entry.has_default_value = true;
      }
      register_console_command = !was_registered;
    }
  }

  if (register_console_command) {
    RegisterConsoleCommandForEntry(name, {}, kDefaultCVarConsoleCategory);
  }
}

CVarSystem::CVarSnapshot CVarSystem::MakeSnapshot(const std::string_view registered_name,
                                                  const CVarEntry &entry) {
  CVarSnapshot snapshot;
  snapshot.registered_name = std::string(registered_name);
  snapshot.value = entry.value;
  snapshot.current_float_value =
      static_cast<float>(openwow::core::ParseFloatLikeSub76FB80(entry.value));
  snapshot.current_int_value = static_cast<std::int32_t>(
      openwow::core::ParseSignedDecimalLikeSub76F0D0(entry.value));
  snapshot.default_value = entry.default_value;
  snapshot.has_default_value = entry.has_default_value;
  snapshot.startup_value = entry.startup_value;
  snapshot.has_startup_value = entry.has_startup_value;
  snapshot.pending_value = entry.pending_value;
  snapshot.has_pending_value = entry.has_pending_value;
  snapshot.flags = entry.flags;
  snapshot.description = entry.description;
  snapshot.min_value = entry.min_value;
  snapshot.max_value = entry.max_value;
  snapshot.has_limits = entry.has_limits;
  snapshot.change_counter = entry.change_counter;
  if (HasFlag(entry.flags, CVarFlags::Account)) {
    snapshot.info_bits |= 0x1u;
  }
  if (HasFlag(entry.flags, CVarFlags::Character)) {
    snapshot.info_bits |= 0x2u;
  }
  snapshot.has_validation_callback =
      static_cast<bool>(entry.validation_callback);
  snapshot.console_category = entry.console_category;
  return snapshot;
}

std::string CVarSystem::GetCVar(const std::string &name) const {
  std::lock_guard lock(mutex_);
  auto it = FindEntryLocked(name);
  if (it != cvars_.end())
    return it->second.value;
  return {};
}

bool CVarSystem::GetCVarBool(const std::string &name) const {
  const std::string value = GetCVar(name);
  return openwow::ui::ScriptParseBoolStringOrDefault(value.c_str(), false);
}

float CVarSystem::GetCVarFloat(const std::string &name) const {
  std::string val = GetCVar(name);
  if (val.empty())
    return 0.0f;
  try {
    return std::stof(val);
  } catch (...) {
    return 0.0f;
  }
}

int CVarSystem::GetCVarInt(const std::string &name) const {

  return static_cast<int>(std::bit_cast<std::int32_t>(
      openwow::core::ParseSignedDecimalLikeSub76F0D0(GetCVar(name).c_str())));
}

auto CVarSystem::ApplyRegisteredSetValueLocked(const std::string &name, CVarEntry &entry,
                                               const std::string &value,
                                               const SetRegisteredValueOptions &options)
    -> RegisteredSetResult {
  RegisteredSetResult result;

  if (!options.force && !options.validate_before_immutable &&
      HasFlag(entry.flags, CVarFlags::ServerSent)) {
    return result;
  }

  if (options.update_current_value && !options.force &&
      !options.bypass_validation && entry.validation_callback) {
    if (!entry.validation_callback(name, entry.value, value)) {
      result.accepted = true;
      return result;
    }
  }

  bool changed_any_state = false;

  if (options.update_current_value) {
    if (options.increment_change_counter) {
      ++entry.change_counter;
    }

    if (!options.force && !options.bypass_read_only &&
        HasFlag(entry.flags, CVarFlags::ReadOnly)) {
      entry.pending_value = value;
      entry.has_pending_value = true;
      result.accepted = true;
      result.should_mark_dirty = true;
      return result;
    }

    if (!options.force && options.validate_before_immutable &&
        HasFlag(entry.flags, CVarFlags::ServerSent)) {
      result.accepted = true;
      return result;
    }

    if (!openwow::text::EqualsIgnoreCaseAscii(entry.value, value)) {
      result.old_value = entry.value;

      result.new_value = value;
      if (entry.value != result.new_value) {
        entry.value = result.new_value;
        result.current_value_changed = true;
        changed_any_state = true;
      }
    }
  }

  if (options.populate_startup_if_missing && !entry.has_startup_value) {
    entry.startup_value = value;
    entry.has_startup_value = true;
    result.metadata_changed = true;
    changed_any_state = true;
  }

  if (options.populate_default_if_missing && !entry.has_default_value) {
    entry.default_value = value;
    entry.has_default_value = true;
    result.metadata_changed = true;
    changed_any_state = true;
  }

  result.accepted =
      options.update_current_value || result.current_value_changed || result.metadata_changed;
  result.should_mark_dirty = options.mark_dirty && changed_any_state;

  if (result.current_value_changed) {
    for (const auto &cb : entry.callbacks) {
      result.callbacks_to_fire.push_back(cb.fn);
    }
  }

  return result;
}

bool CVarSystem::SetCVar(const std::string &name, const std::string &value, bool force) {

  if (name.empty()) {
    return false;
  }

  std::vector<CVarCallback> callbacks_to_fire;
  std::string old_value;
  std::string new_value = value;
  bool result = false;
  bool should_mark_dirty = false;
  bool register_console_command = false;

  {
    std::lock_guard lock(mutex_);
    auto it = FindEntryLocked(name);
    if (it != cvars_.end()) {
      auto &entry = it->second;
      const bool was_registered =
          HasFlag(entry.flags, CVarFlags::Registered);
      if (!was_registered) {
        entry.flags = entry.flags | CVarFlags::Registered;
      }

      SetRegisteredValueOptions options;
      options.force = force;
      options.update_current_value = true;
      options.populate_startup_if_missing = !was_registered;
      options.populate_default_if_missing = false;
      options.mark_dirty = true;

      RegisteredSetResult set_result =
          ApplyRegisteredSetValueLocked(name, entry, value, options);

      old_value = std::move(set_result.old_value);
      if (!set_result.new_value.empty()) {
        new_value = std::move(set_result.new_value);
      }
      callbacks_to_fire = std::move(set_result.callbacks_to_fire);
      result = set_result.accepted;
      should_mark_dirty = set_result.should_mark_dirty;
      register_console_command = !was_registered;
    } else {

      CVarEntry entry;
      entry.registered_name = name;
      entry.value = value;
      entry.startup_value = value;
      entry.has_startup_value = true;
      entry.flags = CVarFlags::Registered;
      const std::string key = FoldLookupKey(name);
      cvars_.emplace(key, std::move(entry));
      stable_keys_.push_back(key);
      old_value = value;
      new_value = value;
      result = true;
      should_mark_dirty = false;
      register_console_command = true;
    }
  }

  if (register_console_command) {
    RegisterConsoleCommandForEntry(name, {}, kDefaultCVarConsoleCategory);
  }

  if (should_mark_dirty) {
    TraceDirtyCVar(name);
    openwow::core::ida::CVar_MarkValueDirty();
  }

  for (const auto &cb : callbacks_to_fire) {
    if (cb)
      cb(old_value, new_value);
  }
  return result;
}

bool CVarSystem::SetRegisteredCVarValue(const std::string &name,
                                        const std::string &value,
                                        SetRegisteredValueOptions options) {
  RegisteredSetResult set_result;

  {
    std::lock_guard lock(mutex_);
    auto it = FindEntryLocked(name);
    if (it == cvars_.end()) {
      return false;
    }
    set_result = ApplyRegisteredSetValueLocked(name, it->second, value, options);
  }

  if (set_result.should_mark_dirty) {
    TraceDirtyCVar(name);
    openwow::core::ida::CVar_MarkValueDirty();
  }

  for (const auto &cb : set_result.callbacks_to_fire) {
    cb(set_result.old_value, set_result.new_value);
  }

  return set_result.accepted;
}

bool CVarSystem::SetRegisteredCVarIntValue(const std::string &name,
                                           const int value,
                                           SetRegisteredValueOptions options) {
  return SetRegisteredCVarValue(name, FormatSignedIntCVarValue(value), options);
}

bool CVarSystem::SetRegisteredCVarValueDirect(const std::string& name,
                                              const std::string& value) {
  SetRegisteredValueOptions options;
  options.validate_before_immutable = true;
  return SetRegisteredCVarValue(name, value, options);
}

std::string CVarSystem::GetCVarDefault(const std::string &name) const {
  std::lock_guard lock(mutex_);
  auto it = FindEntryLocked(name);
  if (it != cvars_.end())
    return it->second.default_value;
  return {};
}

bool CVarSystem::HasCVarDefault(const std::string &name) const {
  std::lock_guard lock(mutex_);
  auto it = FindEntryLocked(name);
  if (it == cvars_.end())
    return false;
  return it->second.has_default_value;
}

CVarFlags CVarSystem::GetCVarFlags(const std::string &name) const {
  std::lock_guard lock(mutex_);
  auto it = FindEntryLocked(name);
  if (it != cvars_.end())
    return it->second.flags;
  return CVarFlags::None;
}

std::string CVarSystem::GetCVarDescription(const std::string &name) const {
  std::lock_guard lock(mutex_);
  auto it = FindEntryLocked(name);
  if (it != cvars_.end())
    return it->second.description;
  return {};
}

std::uint8_t CVarSystem::GetCVarInfoBits(const std::string &name) const {
  std::lock_guard lock(mutex_);
  auto it = FindEntryLocked(name);
  if (it == cvars_.end()) {
    return 0;
  }

  std::uint8_t bits = 0;
  if (HasFlag(it->second.flags, CVarFlags::Account)) {
    bits |= 0x1u;
  }
  if (HasFlag(it->second.flags, CVarFlags::Character)) {
    bits |= 0x2u;
  }
  return bits;
}

void CVarSystem::ResetCVarValue(const std::string& name,
                               const bool prefer_startup_value) {
  std::string registered_name;
  std::string current_value;
  std::string reset_value;
  CVarValidationCallback validation_callback;
  {
    std::lock_guard lock(mutex_);
    const auto it = FindEntryLocked(name);
    if (it == cvars_.end()) {
      return;
    }

    const auto& entry = it->second;
    registered_name = entry.registered_name;
    current_value = entry.value;
    validation_callback = entry.validation_callback;
    if (prefer_startup_value && entry.has_startup_value) {
      reset_value = entry.startup_value;
    } else if (!prefer_startup_value && entry.has_default_value) {
      reset_value = entry.default_value;
    } else if (entry.has_default_value) {
      reset_value = it->second.default_value;
    } else if (entry.has_startup_value) {
      reset_value = it->second.startup_value;
    } else {
      return;
    }
  }

  if (validation_callback &&
      !validation_callback(registered_name, current_value, reset_value)) {
    return;
  }

  SetRegisteredValueOptions options;
  options.bypass_read_only = true;
  options.bypass_validation = true;
  options.increment_change_counter = false;
  (void)SetRegisteredCVarValue(registered_name, reset_value, options);
}

void CVarSystem::ResetCVar(const std::string &name) {
  ResetCVarValue(name, false);
}

bool CVarSystem::Exists(const std::string &name) const {
  std::lock_guard lock(mutex_);
  return FindEntryLocked(name) != cvars_.end();
}

bool CVarSystem::UnregisterCVar(const std::string &name) {
  std::string registered_name;
  std::uint64_t command_registration_id = 0;
  {
    std::lock_guard lock(mutex_);
    const auto it = FindEntryLocked(name);
    if (it == cvars_.end()) {
      return false;
    }
    const std::string key = it->first;
    registered_name = it->second.registered_name;
    command_registration_id =
        it->second.console_command_registration_id;
    cvars_.erase(it);
    stable_keys_.erase(
        std::remove(stable_keys_.begin(), stable_keys_.end(), key),
        stable_keys_.end());
  }

  if (command_registration_id != 0) {
    (void)openwow::debug::DebugConsole::Get().UnregisterCommandIfCurrent(
        registered_name, command_registration_id);
  }
  return true;
}

bool CVarSystem::IsHidden(const std::string &name) const {
  std::lock_guard lock(mutex_);
  auto it = FindEntryLocked(name);
  if (it == cvars_.end())
    return false;
  return HasFlag(it->second.flags, CVarFlags::Hidden);
}

std::string CVarSystem::GetStartupValue(const std::string &name) const {
  std::lock_guard lock(mutex_);
  auto it = FindEntryLocked(name);
  if (it != cvars_.end())
    return it->second.startup_value;
  return {};
}

bool CVarSystem::HasStartupValue(const std::string &name) const {
  std::lock_guard lock(mutex_);
  auto it = FindEntryLocked(name);
  return it != cvars_.end() && it->second.has_startup_value;
}

std::string CVarSystem::GetPendingValue(const std::string &name) const {
  std::lock_guard lock(mutex_);
  auto it = FindEntryLocked(name);
  if (it != cvars_.end())
    return it->second.pending_value;
  return {};
}

std::uint32_t CVarSystem::GetChangeCounter(const std::string &name) const {
  std::lock_guard lock(mutex_);
  auto it = FindEntryLocked(name);
  if (it != cvars_.end())
    return it->second.change_counter;
  return 0;
}

std::optional<CVarSystem::CVarSnapshot> CVarSystem::GetCVarSnapshot(
    const std::string_view name) const {
  std::lock_guard lock(mutex_);
  auto it = FindEntryLocked(name);
  if (it == cvars_.end()) {
    return std::nullopt;
  }
  return MakeSnapshot(it->second.registered_name, it->second);
}

std::optional<CVarSystem::CVarSnapshot> CVarSystem::LookupCVarByName(
    const std::string_view name) const {
  std::lock_guard lock(mutex_);
  auto it = FindEntryLocked(name);
  if (it == cvars_.end()) {
    return std::nullopt;
  }

  const auto &entry = it->second;
  if (!entry.is_native_registered && !HasFlag(entry.flags, CVarFlags::NoSave)) {
    return std::nullopt;
  }

  return MakeSnapshot(entry.registered_name, entry);
}

void CVarSystem::SetValidationCallback(const std::string &name, CVarValidationCallback cb) {
  std::lock_guard lock(mutex_);
  auto it = FindEntryLocked(name);
  if (it != cvars_.end()) {
    it->second.validation_callback = std::move(cb);
    it->second.validation_callback_handle = 0;
    it->second.validation_reconciled = false;
  }
}

CVarValidationCallbackRegistration CVarSystem::RegisterValidationCallback(
    const std::string& name, CVarValidationCallback cb) {
  std::lock_guard lock(mutex_);
  auto it = FindEntryLocked(name);
  if (it == cvars_.end()) {
    return {};
  }

  const std::uint64_t handle = next_validation_callback_handle_++;
  it->second.validation_callback = std::move(cb);
  it->second.validation_callback_handle = handle;
  it->second.validation_reconciled = false;
  return CVarValidationCallbackRegistration(*this, name, handle);
}

void CVarSystem::RemoveValidationCallback(const std::string& name,
                                          const std::uint64_t handle) {
  std::lock_guard lock(mutex_);
  auto it = FindEntryLocked(name);
  if (it == cvars_.end() || it->second.validation_callback_handle != handle) {
    return;
  }

  it->second.validation_callback = {};
  it->second.validation_callback_handle = 0;
  it->second.validation_reconciled = false;
}

void CVarSystem::SetCVarInfoBits(const std::string &name, const std::uint8_t bits) {
  std::lock_guard lock(mutex_);
  auto it = FindEntryLocked(name);
  if (it != cvars_.end()) {
    constexpr std::uint32_t kScopeMask =
        static_cast<std::uint32_t>(CVarFlags::Account) |
        static_cast<std::uint32_t>(CVarFlags::Character);
    std::uint32_t flags = static_cast<std::uint32_t>(it->second.flags) & ~kScopeMask;
    if ((bits & 0x1u) != 0) {
      flags |= static_cast<std::uint32_t>(CVarFlags::Account);
    }
    if ((bits & 0x2u) != 0) {
      flags |= static_cast<std::uint32_t>(CVarFlags::Character);
    }
    it->second.flags = static_cast<CVarFlags>(flags);
  }
}

void CVarSystem::AddFlags(const std::string &name, const CVarFlags flags) {
  std::lock_guard lock(mutex_);
  auto it = FindEntryLocked(name);
  if (it != cvars_.end()) {
    it->second.flags = static_cast<CVarFlags>(
        static_cast<std::uint32_t>(it->second.flags) |
        static_cast<std::uint32_t>(flags));
  }
}

bool CVarSystem::ReconcileValueAgainstValidationCallback(const std::string &name) {
  std::string current_value;
  std::string default_value;
  CVarValidationCallback validation_callback;
  {
    std::lock_guard lock(mutex_);
    auto it = FindEntryLocked(name);
    if (it == cvars_.end()) {
      return false;
    }

    current_value = it->second.value;
    if (!it->second.has_default_value) {
      return false;
    }
    default_value = it->second.default_value;
    validation_callback = it->second.validation_callback;
    if (it->second.validation_reconciled) {
      return false;
    }
    it->second.validation_reconciled = true;
  }

  if (!validation_callback || validation_callback(name, current_value, current_value)) {
    return false;
  }

  return SetCVar(name, default_value);
}

void CVarSystem::ApplyClientRegisterCVarsValueFixups() {
  std::string show_tools_ui_value;
  {
    std::lock_guard lock(mutex_);
    auto it = FindEntryLocked("showToolsUI");
    if (it == cvars_.end()) {
      return;
    }

    show_tools_ui_value = it->second.value;
  }

  if (ParseMscrtAtol32(show_tools_ui_value) >= 2u) {
    (void)SetCVar("showToolsUI", "1");
  }
}

void CVarSystem::ResetToStartup(const std::string &name) {
  ResetCVarValue(name, true);
}

bool CVarSystem::ApplyPendingValue(const std::string &name) {
  std::string pending;
  {
    std::lock_guard lock(mutex_);
    auto it = FindEntryLocked(name);
    if (it == cvars_.end())
      return false;
    if (!HasFlag(it->second.flags, CVarFlags::ReadOnly))
      return false;
    if (!it->second.has_pending_value)
      return false;
    pending = it->second.pending_value;
    it->second.pending_value.clear();
    it->second.has_pending_value = false;
  }
  SetRegisteredValueOptions options;
  options.bypass_read_only = true;
  options.bypass_validation = true;
  options.increment_change_counter = false;
  (void)SetRegisteredCVarValue(name, pending, options);
  return true;
}

std::uint32_t CVarSystem::AddCallback(const std::string &name, CVarCallback cb) {
  std::lock_guard lock(mutex_);
  auto it = FindEntryLocked(name);
  if (it == cvars_.end()) {

    CVarEntry entry;
    entry.registered_name = name;
    entry.value = "";
    const std::string key = FoldLookupKey(name);
    cvars_.emplace(key, std::move(entry));
    stable_keys_.push_back(key);
    it = cvars_.find(key);
  }
  auto handle = next_callback_handle_++;
  it->second.callbacks.push_back({handle, std::move(cb)});
  return handle;
}

void CVarSystem::RemoveCallback(const std::string &name, std::uint32_t handle) {
  std::lock_guard lock(mutex_);
  auto it = FindEntryLocked(name);
  if (it == cvars_.end())
    return;
  auto &cbs = it->second.callbacks;
  cbs.erase(std::remove_if(cbs.begin(), cbs.end(),
                           [handle](const auto &e) { return e.handle == handle; }),
            cbs.end());
}

std::vector<std::string> CVarSystem::GetAllNames() const {
  std::lock_guard lock(mutex_);
  std::vector<std::string> names;
  names.reserve(cvars_.size());
  for (const auto &key : stable_keys_) {
    if (const auto it = cvars_.find(key); it != cvars_.end()) {
      names.push_back(it->second.registered_name);
    }
  }
  return names;
}

std::size_t CVarSystem::Count() const {
  std::lock_guard lock(mutex_);
  return cvars_.size();
}

void CVarSystem::Clear() {
  std::vector<std::pair<std::string, std::uint64_t>> console_commands;
  {
    std::lock_guard lock(mutex_);
    console_commands.reserve(cvars_.size());
    for (const auto& [_, entry] : cvars_) {
      if (entry.console_command_registration_id != 0) {
        console_commands.emplace_back(
            entry.registered_name,
            entry.console_command_registration_id);
      }
    }
    cvars_.clear();
    stable_keys_.clear();
    next_callback_handle_ = 1;
  }

  auto& console = openwow::debug::DebugConsole::Get();
  for (const auto& [name, registration_id] : console_commands) {
    (void)console.UnregisterCommandIfCurrent(name, registration_id);
  }
}

void CVarSystem::LoadFromFile(const std::string &path) {

  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo, "CVarSystem: no config file at " + path);
    return;
  }

  std::string content{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};

  const char *data = content.data();
  std::size_t size = content.size();
  if (size >= 3 && static_cast<unsigned char>(data[0]) == 0xEF &&
      static_cast<unsigned char>(data[1]) == 0xBB && static_cast<unsigned char>(data[2]) == 0xBF) {
    data += 3;
    size -= 3;
  }

  int count = 0;
  const char *p = data;
  const char *end = data + size;
  while (p < end) {

    const char *eol = p;
    while (eol < end && *eol != '\r' && *eol != '\n')
      ++eol;

    std::string line(p, eol);

    p = eol;
    if (p < end && *p == '\r')
      ++p;
    if (p < end && *p == '\n')
      ++p;

    if (line.size() < 4)
      continue;
    if (!(std::toupper(static_cast<unsigned char>(line[0])) == 'S' &&
          std::toupper(static_cast<unsigned char>(line[1])) == 'E' &&
          std::toupper(static_cast<unsigned char>(line[2])) == 'T' && line[3] == ' '))
      continue;

    std::string rest = line.substr(4);
    std::istringstream iss(rest);
    std::string name;
    iss >> name;
    if (name.empty())
      continue;

    std::string value;
    iss >> std::ws;
    if (iss.peek() == '"') {
      iss.get();
      std::getline(iss, value, '"');
    } else {
      iss >> value;
    }

    SetCVar(name, value, true);
    ++count;
  }

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "CVarSystem: loaded " + std::to_string(count) + " CVars from " + path);
}

bool CVarSystem::SaveToFile(const std::string &path, bool save_all) const {
  int count = 0;
  std::string serialized;
  {
    std::lock_guard lock(mutex_);
    serialized = SerializeConfigLocked(CVarSerializationScope::kConfigFile,
                                       CVarFlags::None, save_all, &count);
  }

  if (!openwow::platform::filesystem::AtomicWriteFile(path, serialized)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, "CVarSystem: write failed for " + path);
    return false;
  }

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "CVarSystem: saved " + std::to_string(count) + " CVars to " + path);
  return true;
}

std::string CVarSystem::SerializeConfig(bool save_all) const {
  return SerializeConfig(CVarSerializationScope::kConfigFile, save_all);
}

std::string CVarSystem::SerializeConfig(const CVarSerializationScope scope,
                                        const bool save_all) const {
  return SerializeConfig(scope, CVarFlags::None, save_all);
}

std::string CVarSystem::SerializeConfig(const CVarSerializationScope scope,
                                        const CVarFlags excluded_flags,
                                        const bool save_all) const {
  std::lock_guard lock(mutex_);
  return SerializeConfigLocked(scope, excluded_flags, save_all, nullptr);
}

std::string CVarSystem::SerializeConfigLocked(const CVarSerializationScope scope,
                                              const CVarFlags excluded_flags,
                                              const bool save_all,
                                              int* const count) const {
  int local_count = 0;
  std::string output;
  const std::uint32_t required_scope = PersistenceScopeBits(scope);
  const std::uint32_t excluded = static_cast<std::uint32_t>(excluded_flags);

  for (const auto& key : stable_keys_) {
    const auto entry_it = cvars_.find(key);
    if (entry_it == cvars_.end()) {
      continue;
    }
    const auto& entry = entry_it->second;
    const auto& name = entry.registered_name;
    const std::uint32_t flags = static_cast<std::uint32_t>(entry.flags);
    if ((flags & static_cast<std::uint32_t>(CVarFlags::Registered)) == 0
        || (flags & static_cast<std::uint32_t>(CVarFlags::NoSave)) != 0
        || (flags & kPersistenceScopeMask) != required_scope
        || (flags & excluded) != 0) {
      continue;
    }

    const std::string& save_val =
        entry.has_pending_value ? entry.pending_value : entry.value;

    if (!save_all && entry.has_default_value
        && openwow::text::EqualsIgnoreCaseAscii(save_val, entry.default_value)) {
      continue;
    }

    std::array<char, 260> line{};
    std::snprintf(line.data(), line.size(), "SET %s \"%s\"\n",
                  name.c_str(), save_val.c_str());
    output.append(line.data());
    ++local_count;
  }

  if (count != nullptr) {
    *count = local_count;
  }
  return output;
}

void CVarSystem::RegisterDefaults() {
  using F = CVarFlags;

  const auto register_gx_fallback = [this](
                                        const char *name,
                                        const char *default_value,
                                        const F flags,
                                        const char *description) {
    if (!Exists(name)) {
      RegisterCVar(name, default_value, flags, description, 0.0f, 0.0f, 1);
    }
  };
  const auto pending_display = F::Archive | F::ReadOnly;
  register_gx_fallback("widescreen", "1", F::Archive,
                       "Allow widescreen support");
  register_gx_fallback("gxWindow", "0", pending_display,
                       "toggle fullscreen/window");
  register_gx_fallback("gxMaximize", "0", pending_display,
                       "maximize game window");
  register_gx_fallback("gxColorBits", "24", pending_display, "color bits");
  register_gx_fallback("gxDepthBits", "24", pending_display, "depth bits");
  register_gx_fallback("gxResolution", "1024x768", pending_display,
                       "resolution");
  register_gx_fallback("gxRefresh", "75", pending_display, "refresh rate");
  register_gx_fallback("gxTripleBuffer", "0", pending_display,
                       "triple buffer");
  register_gx_fallback("gxApi", "D3D9", pending_display, "graphics api");
  register_gx_fallback("gxVSync", "1", pending_display, "vsync on or off");
  register_gx_fallback("gxAspect", "1", pending_display,
                       "constrain window aspect");
  register_gx_fallback("gxCursor", "1", pending_display,
                       "toggle hardware cursor");
  register_gx_fallback("gxMultisample", "1", pending_display,
                       "multisample");
  register_gx_fallback("gxMultisampleQuality", "0.0", pending_display,
                       "multisample quality");
  register_gx_fallback("gxFixLag", "0", pending_display,
                       "prevent cursor lag");
  register_gx_fallback("gxStereoEnabled", "0", F::Archive,
                       "Enable stereoscopic rendering");
  register_gx_fallback("gxOverride", "", F::Archive, "gx overrides");
  register_gx_fallback("maxFPS", "200", F::Archive, "Set FPS limit");
  register_gx_fallback("maxFPSBk", "30", F::Archive,
                       "Set background FPS limit");
  register_gx_fallback("videoOptionsVersion", "0", pending_display,
                       "Video options version");
  register_gx_fallback("windowResizeLock", "0", F::Archive,
                       "prevent resizing in windowed mode");
  register_gx_fallback("AllowMultisampleFBO", "1", F::None,
                       "Allow use of FBO's when rendering to multisampled back buffer");
  register_gx_fallback(
      "UseNVShaders", "1", F::Archive,
      "Enable/Disable use of nvvp3 and nvfp2 shaders. Only relevant when using the GLL gxApi.");
  register_gx_fallback("fixedFunction", "0", pending_display,
                       "Force fixed function rendering");

  RegisterCVar("farclip", "350", F::Archive, "View distance");
  RegisterCVar("nearclip", "0.2", F::Archive, "Near clip plane distance");
  RegisterCVar("horizonFarclipScale", "4.0", F::Archive, "Far clip plane scale for horizon");
  RegisterCVar("horizonNearclipScale", "0.7", F::Archive, "Near clip plane scale for horizon");
  RegisterCVar("texLodBias", "0.0", F::Archive, "Texture LOD Bias");
  RegisterCVar("MaxLights", "4", F::Archive, "Max number of hardware lights");
  RegisterCVar("groundEffectDensity", "16", F::Archive, "Ground detail density", 16.0f,
               256.0f);
  RegisterCVar("groundEffectDist", "70.0", F::Archive, "Ground effect distance");

  RegisterNativeCVar("shadowLevel", "1", F::Archive,
                     "Terrain shadow map mip level",
                     ShadowLevelValidationCallback);
  RegisterCVar("particleDensity", "1.0", F::Archive, "Particle density", 0.0f, 1.0f);
  SetValidationCallback("particleDensity", ParticleDensityValidationCallback);
  (void)ReconcileValueAgainstValidationCallback("particleDensity");
  RegisterCVar("environmentDetail", "1.0", F::Archive, "Environment detail level", 0.5f, 1.5f);
  RegisterCVar("baseMip", "0", F::Archive, "Texture resolution (0=high, 1=low)", 0.0f, 1.0f);
  RegisterCVar("waterLOD", "0", F::Archive, "Water geometry LOD");
  RegisterCVar("componentTextureLevel", "8", F::Archive,
               "Number of mip levels used for character component textures");
  RegisterCVar("componentThread", "1", F::Archive, "Multi thread character component processing");
  RegisterCVar("componentCompress", "1", F::Archive, "Character component texture compression");
  RegisterCVar("weatherDensity", "2", F::Archive, "Weather density", 0.0f, 3.0f);
  RegisterCVar("projectedTextures", "0", F::Archive, "Projected textures");
  RegisterNativeCVar("ffx", "1", F::Archive, "full screen effects",
                     FfxEnabledValidationCallback, 0.0f, 0.0f, 1);
  RegisterNativeCVar("ffxRectangle", "1", F::Archive,
                     "use rectangle texture for full screen effects",
                     FfxEnabledValidationCallback, 0.0f, 0.0f, 1);
  RegisterNativeCVar("ffxGlow", "1", F::Archive,
                     "full screen glow effect",
                     FfxEnabledValidationCallback, 0.0f, 0.0f, 1);

  RegisterCVar("ffxDeath", "1", F::Archive, "full screen death effect");
  RegisterCVar("ffxNetherWorld", "1", F::Archive,
               "full screen nether world effect (for invisibility)");
  RegisterCVar("ffxSpecial", "1", F::Archive, "full screen test effect");
  RegisterCVar("spellEffectLevel", "9", F::Archive, "Spell effect detail level");
  RegisterCVar("textureFilteringMode", "1", F::Archive, "Texture filtering mode");
  RegisterCVar("UIFaster", "3", F::Archive, "UI acceleration option");
  RegisterCVar("textureCacheSize", "32", F::Archive, "Texture cache size in bytes");
  RegisterCVar("gxTextureCacheSize", "0", F::Archive, "GX Texture Cache Size");
  RegisterCVar("extShadowQuality", "0", F::Archive, "Extended shadow quality (0-5)", 0.0f, 5.0f);
  RegisterCVar("hwDetect", "1", F::Archive, "Hardware detection enable",
               0.0f, 0.0f, 1);
  RegisterCVar("mapShadows", "1", F::Archive, "Map shadows");
  RegisterCVar("gxStereoConvergence", "1", F::Archive,
               "Stereo convergence distance", 0.0f, 0.0f, 1);
  RegisterCVar("gxStereoSeparation", "25", F::Archive,
               "Stereo eye separation", 0.0f, 0.0f, 1);

  RegisterCVar("Joystick", "0", F::None, "enable joystick control");
  RegisterCVar("CinematicJoystick", "0", F::None, "enable cinematic joystick control");
  RegisterCVar("enableWowMouse", "0", F::Archive, "Enable Steelseries World of Warcraft Mouse");

  openwow::render::RegisterM2CVarDefaults(*this);
  RegisterCVar("specular", "0", F::Archive, "Specular lighting");

  RegisterCVar("Sound_EnableSFX", "1", F::Archive, "");
  RegisterCVar("Sound_EnableMusic", "1", F::Archive, "Enables music");
  RegisterCVar("Sound_EnableAmbience", "1", F::Archive, "Enable Ambience");
  RegisterCVar("Sound_EnableEmoteSounds", "1", F::Archive, "");
  RegisterCVar("Sound_EnableErrorSpeech", "1", F::Archive, "error speech");
  RegisterCVar("Sound_MasterVolume", "1.0", F::Archive,
               "master volume (0.0 to 1.0)");

  RegisterCVar("Sound_SFXVolume", "1.0", F::Archive,
               "sound volume (0.0 to 1.0)");

  RegisterCVar("Sound_MusicVolume", "0.4", F::Archive,
               "music volume (0.0 to 1.0)");

  RegisterCVar("Sound_AmbienceVolume", "0.6", F::Archive,
               "Ambience Volume (0.0 to 1.0)");

  RegisterCVar("SoundMemoryCache", "4", F::Archive | F::ReadOnly,
               "Sound memory cache size in MB");

  RegisterCVar("Sound_MaxCacheSizeInBytes", "16777216", F::Archive, "Max cache size in bytes",
               4194304.0f, 134217728.0f);

  RegisterCVar("Sound_MaxCacheableSizeInBytes", "1048576", F::Archive,
               "Max sound size that will be cached, larger files will be streamed instead");
  RegisterCVar("Sound_EnablePetSounds", "1", F::Archive, "Enables pet sounds");
  RegisterCVar("Sound_ListenerAtCharacter", "1", F::Archive, "lock listener at character");
  openwow::audio::RegisterSoundInterfaceCVarDefaults(*this);
  RegisterCVar("Sound_EnableDSPEffects", GetSoundEnableDspEffectsDefaultValue(), F::Archive, "");
  RegisterCVar("Sound_ZoneMusicNoDelay", "0", F::Archive, "");

  RegisterCVar("Sound_EnableSoundWhenGameIsInBG", "1", F::Archive,
               "Enable Sound When Game Is In Background");

  RegisterCVar("Sound_EnableArmorFoleySoundForSelf", "1", F::Archive, "");
  RegisterCVar("Sound_EnableArmorFoleySoundForOthers", "1", F::Archive, "");

  const auto register_camera_cvar =
      [this](const std::string& name, const std::string& default_value,
             const std::uint8_t info_bits,
             CVarValidationCallback validation_callback = {},
             const bool hidden = false) {
        RegisterNativeCVar(
            name, default_value,
            hidden ? F::Archive | F::Hidden : F::Archive, {},
            std::move(validation_callback));
        SetCVarInfoBits(name, info_bits);
      };
  constexpr std::uint8_t kAccountCameraCVar = 0x1u;
  constexpr std::uint8_t kCharacterCameraCVar = 0x2u;
  register_camera_cvar("cameraSavedDistance", "5.55",
                       kCharacterCameraCVar);
  register_camera_cvar("cameraSavedVehicleDistance", "-1.0",
                       kCharacterCameraCVar);
  register_camera_cvar("cameraSavedPitch", "10.0",
                       kCharacterCameraCVar);
  register_camera_cvar("mouseInvertYaw", "0", kAccountCameraCVar);
  register_camera_cvar("mouseInvertPitch", "0", kAccountCameraCVar);
  register_camera_cvar("cameraBobbing", "0", 0);
  register_camera_cvar("cameraDistanceMoveSpeed", "8.33",
                       kAccountCameraCVar,
                       CameraMoveSpeedValidationCallback);
  register_camera_cvar("cameraPitchMoveSpeed", "90", kAccountCameraCVar,
                       CameraRate360ValidationCallback);
  register_camera_cvar("cameraYawMoveSpeed", "180", kAccountCameraCVar,
                       CameraRate360ValidationCallback);
  register_camera_cvar("cameraBobbingSmoothSpeed", "0.8",
                       kAccountCameraCVar,
                       CameraMoveSpeedValidationCallback);
  register_camera_cvar("cameraFoVSmoothSpeed", "0.5", kAccountCameraCVar,
                       CameraRate360ValidationCallback);
  register_camera_cvar("cameraDistanceSmoothSpeed", "8.33",
                       kAccountCameraCVar,
                       CameraMoveSpeedValidationCallback);
  register_camera_cvar("cameraGroundSmoothSpeed", "7.5",
                       kAccountCameraCVar,
                       CameraRate360ValidationCallback);
  register_camera_cvar("cameraHeightSmoothSpeed", "1.2",
                       kAccountCameraCVar,
                       CameraMoveSpeedValidationCallback);
  register_camera_cvar("cameraPitchSmoothSpeed", "45", kAccountCameraCVar,
                       CameraRate360ValidationCallback);
  register_camera_cvar("cameraTargetSmoothSpeed", "90", kAccountCameraCVar,
                       CameraRate360ValidationCallback);
  register_camera_cvar("cameraYawSmoothSpeed", "180", kAccountCameraCVar,
                       CameraRate360ValidationCallback);
  register_camera_cvar("cameraFlyingMountHeightSmoothSpeed", "2.0",
                       kAccountCameraCVar,
                       CameraMoveSpeedValidationCallback);
  register_camera_cvar("cameraViewBlendStyle", "1", kAccountCameraCVar);
  register_camera_cvar("cameraView", "2", kAccountCameraCVar,
                       CameraViewValidationCallback);

  constexpr std::array<const char*, 3> kCameraViewAxes{
      "Distance", "Pitch", "Yaw"};
  const std::array<CVarValidationCallback, 3> camera_view_validators{
      CameraDistanceValidationCallback, CameraPitchDegreesValidationCallback,
      CameraYawDegreesValidationCallback};
  for (std::size_t view = 0;
       view < openwow::game::camera::kRetailCameraViewSuffixes.size();
       ++view) {
    const auto& defaults =
        openwow::game::camera::kRetailCameraViewDefaults[view];
    const std::array<const char*, 3> values{
        defaults.distance, defaults.pitch_degrees, defaults.yaw_degrees};
    for (std::size_t axis = 0; axis < kCameraViewAxes.size(); ++axis) {
      register_camera_cvar(
          std::string("camera") + kCameraViewAxes[axis] +
              openwow::game::camera::kRetailCameraViewSuffixes[view],
          values[axis], kAccountCameraCVar, camera_view_validators[axis], true);
    }
  }

  register_camera_cvar("camerasmooth", "1", kAccountCameraCVar);
  register_camera_cvar("cameraSmoothPitch", "1", kAccountCameraCVar);
  register_camera_cvar("cameraSmoothYaw", "1", kAccountCameraCVar);
  register_camera_cvar("cameraSmoothStyle", "4", kAccountCameraCVar,
                       CameraSmoothingStyleValidationCallback);
  register_camera_cvar("cameraSmoothTrackingStyle", "4",
                       kAccountCameraCVar,
                       CameraSmoothingStyleValidationCallback);
  register_camera_cvar("cameraCustomViewSmoothing", "0",
                       kAccountCameraCVar);

  for (std::size_t style = 0;
       style < openwow::game::camera::kRetailCameraSmoothStyleSuffixes.size();
       ++style) {
    const std::string style_suffix =
        openwow::game::camera::kRetailCameraSmoothStyleSuffixes[style];
    for (std::size_t event = 0;
         event < openwow::game::camera::kRetailCameraSmoothEventSuffixes.size();
         ++event) {
      const auto& defaults =
          openwow::game::camera::kRetailCameraSmoothEventDefaults
              [style * openwow::game::camera::kRetailCameraSmoothEventSuffixes
                           .size() +
               event];
      const std::string base =
          "cameraSmooth" + style_suffix +
          openwow::game::camera::kRetailCameraSmoothEventSuffixes[event];
      register_camera_cvar(base + "Delay", defaults.delay, kAccountCameraCVar);
      register_camera_cvar(base + "Factor", defaults.factor,
                           kAccountCameraCVar);
    }
    for (std::size_t axis = 0;
         axis < openwow::game::camera::kRetailCameraSmoothAxisSuffixes.size();
         ++axis) {
      const auto& defaults =
          openwow::game::camera::kRetailCameraSmoothViewDataDefaults
              [style *
                   openwow::game::camera::kRetailCameraSmoothAxisSuffixes
                       .size() +
               axis];
      const std::string base =
          "cameraSmoothViewData" + style_suffix +
          openwow::game::camera::kRetailCameraSmoothAxisSuffixes[axis];
      register_camera_cvar(base + "Delay", defaults.delay, kAccountCameraCVar);
      register_camera_cvar(base + "Factor", defaults.factor,
                           kAccountCameraCVar);
    }
  }
  register_camera_cvar("cameraTerrainTilt", "0", kAccountCameraCVar);
  register_camera_cvar("cameraTerrainTiltTimeMin", "3.0",
                       kAccountCameraCVar,
                       CameraTimeRangeValidationCallback);
  register_camera_cvar("cameraTerrainTiltTimeMax", "10.0",
                       kAccountCameraCVar,
                       CameraTimeRangeValidationCallback);
  register_camera_cvar("cameraWaterCollision", "1", kAccountCameraCVar);
  register_camera_cvar("cameraHeightIgnoreStandState", "0",
                       kAccountCameraCVar);
  register_camera_cvar("cameraPivot", "1", kAccountCameraCVar);
  register_camera_cvar("cameraPivotDXMax", "0.05", kAccountCameraCVar);
  register_camera_cvar("cameraPivotDYMin", "0.00", kAccountCameraCVar);
  register_camera_cvar("cameraDive", "1", kAccountCameraCVar);
  register_camera_cvar("cameraSurfacePitch", "0.0", kAccountCameraCVar,
                       CameraPitchDegreesValidationCallback);
  register_camera_cvar("cameraSubmergePitch", "18.0", kAccountCameraCVar,
                       CameraPitchDegreesValidationCallback);
  register_camera_cvar("cameraSurfaceFinalPitch", "5.0",
                       kAccountCameraCVar,
                       CameraPitchDegreesValidationCallback);
  register_camera_cvar("cameraSubmergeFinalPitch", "5.0",
                       kAccountCameraCVar,
                       CameraPitchDegreesValidationCallback);
  register_camera_cvar("cameraDistanceMax", "15.0", kAccountCameraCVar,
                       CameraDistanceValidationCallback);
  register_camera_cvar("cameraDistanceMaxFactor", "1.0",
                       kAccountCameraCVar);
  register_camera_cvar("cameraPitchSmoothMin", "0.0", kAccountCameraCVar,
                       CameraPitchDegreesValidationCallback);
  register_camera_cvar("cameraPitchSmoothMax", "30.0", kAccountCameraCVar,
                       CameraPitchDegreesValidationCallback);
  register_camera_cvar("cameraYawSmoothMin", "0.0", kAccountCameraCVar,
                       CameraYawDegreesValidationCallback);
  register_camera_cvar("cameraYawSmoothMax", "0.0", kAccountCameraCVar,
                       CameraYawDegreesValidationCallback);
  register_camera_cvar("cameraSmoothTimeMin", "0.1", kAccountCameraCVar,
                       CameraTimeRangeValidationCallback);
  register_camera_cvar("cameraSmoothTimeMax", "2.0", kAccountCameraCVar,
                       CameraTimeRangeValidationCallback);

  const auto register_unit_name_cvar =
      [this](const char *name, const char *default_value,
             const std::uint32_t display_flag) {
        RegisterNativeCVar(name, default_value, F::Account, "",
                           PlayerNameDisplayFlagCallback(display_flag),
                           0.0f, 0.0f, 4);
      };
  register_unit_name_cvar("UnitNameOwn", "0", 0x0001);
  register_unit_name_cvar("UnitNameNPC", "0", 0x0002);
  register_unit_name_cvar("UnitNamePlayerGuild", "1", 0x0004);
  register_unit_name_cvar("UnitNamePlayerPVPTitle", "1", 0x0008);
  register_unit_name_cvar("UnitNameEnemyPlayerName", "1", 0x0010);
  register_unit_name_cvar("UnitNameEnemyPetName", "1", 0x0020);
  register_unit_name_cvar("UnitNameEnemyGuardianName", "0", 0x1000);
  register_unit_name_cvar("UnitNameEnemyTotemName", "0", 0x0040);
  register_unit_name_cvar("UnitNameFriendlyPlayerName", "1", 0x0080);
  register_unit_name_cvar("UnitNameFriendlyPetName", "1", 0x0100);
  register_unit_name_cvar("UnitNameFriendlyGuardianName", "0", 0x2000);
  register_unit_name_cvar("UnitNameFriendlyTotemName", "0", 0x0200);
  register_unit_name_cvar("UnitNameNonCombatCreatureName", "0", 0x0400);

  RegisterCVar("combatTextFloatMode", "1", F::Account, "Floating combat text mode");

  RegisterCVar("autoLootDefault", "0", F::Account, "Auto-loot by default");
  RegisterCVar("autoSelfCast", "1", F::Character, "Auto self-cast");
  RegisterCVar("autoUnshift", "1", F::Account, "Auto-cancel shapeshift forms");
  RegisterCVar("deselectOnClick", "1", F::Account, "Deselect target on click");
  RegisterCVar("showTargetOfTarget", "0", F::Account,
               "Whether the target of target frame should be shown");
  RegisterCVar("showTargetCastbar", "1", F::Account, "Show target cast bar");
  RegisterCVar("showVKeyCastbar", "1", F::Account, "Show V-key cast bar");

  RegisterCVar("buffDurations", "1", F::Account, "Whether to show buff durations");
  RegisterCVar("questFadingDisable", "1", F::Account,
               "Whether to disable quest text slowly fading in");
  RegisterCVar("autoInteract", GetAutoInteractDefaultValue(), F::Account,
               "Toggles auto-move to interact target");
  RegisterCVar("lootUnderMouse", "1", F::Account,
               "Whether the loot window should open under the mouse");
  RegisterCVar("stopAutoAttackOnTargetChange", "0", F::Character,
               "Stop auto-attack on target change");
  RegisterCVar("showLootSpam", "1", F::Account, "Show loot spam in chat");
  RegisterCVar("showNewbieTips", "1", F::Account, "Show newbie tips");
  RegisterCVar("UberTooltips", "1", F::Account, "Enhanced tooltips");
  RegisterCVar("screenEdgeFlash", "1", F::Account, "Low-health screen edge flash");

  RegisterCVar("autoStand", "1", F::Account, "Automatically stand when needed");
  RegisterCVar("autoClearAFK", "1", F::Account, "Automatically clear AFK when moving or chatting");
  RegisterCVar("blockTrades", "0", F::Character, "Whether to automatically block trade requests");
  RegisterCVar("alwaysCompareItems", "0", F::Account, "Always show item comparison tooltips");
  RegisterCVar("equipmentManager", "0", F::Character, "Enables the equipment management UI");
  RegisterCVar("targetOfTargetMode", "5", F::Account,
               "The conditions under which target of target should be shown");
  RegisterCVar("minimapPortalMax", "99", F::Character,
               "Max Number of Portals to traverse for minimap");
  RegisterCVar("displayFreeBagSlots", "0", F::Account,
               "Whether or not the backpack button should indicate how many inventory slots you've "
               "got free");
  RegisterCVar("displayWorldPVPObjectives", "1", F::Account,
               "Whether to show world PvP objectives");
  RegisterCVar("autoQuestProgress", "1", F::Account,
               "Whether to automatically watch all quests when they are updated");
  RegisterCVar("showQuestTrackingTooltips", "1", F::Character,
               "Displays quest tracking information in unit and object tooltips");
  RegisterCVar("mapQuestDifficulty", "0", F::Character,
               "Whether to color quest titles by difficulty in the World Map");
  RegisterCVar("questLogCollapseFilter", "0", F::Character,
               "bit field for saving off the state of the headers in Quest Log");
  SetCVar("questLogCollapseFilter", "-1", true);
  RegisterCVar("advancedWatchFrame", "0", F::Account,
               "Enables advanced Objectives tracking features");
  RegisterCVar("watchFrameIgnoreCursor", "0", F::Account,
               "Disables Objectives frame mouseover and title dropdown");
  RegisterCVar("watchFrameBaseAlpha", "0", F::Account, "Objectives frame opacity");
  RegisterCVar("watchFrameState", "0", F::Account,
               "Stores Objectives frame locked and collapsed states");
  RegisterCVar("showQuestObjectivesOnMap", "1", F::Character, "Shows quest POIs on the main map");

  RegisterCVar("trackedQuests", "", F::Character | F::ConsoleReadOnly, "Internal cvar for saving tracked quests in order");
  RegisterCVar("trackedAchievements", "", F::Character | F::ConsoleReadOnly,
               "Internal cvar for saving tracked achievements in order");
  RegisterCVar("flaggedTutorials", "", F::Account | F::ConsoleReadOnly,
               "Internal cvar for saving completed tutorials in order");
  RegisterCVar("advancedWorldMap", "0", F::Character, "Enables advanced World Map features");
  RegisterCVar("worldMapOpacity", "0", F::Character, "Opacity for the world map when sized down");
  RegisterCVar("watchFrameWidth", "0", F::None, "Controls objectives frame width");
  RegisterCVar("trackerSorting", "0", F::Character, "sorting option for the objectives tracker");
  RegisterCVar("trackerFilter", "7", F::Character, "filter option for the objectives tracker");
  RegisterCVar("guildShowOffline", "1", F::Account, "Show offline guild members in the guild UI");
  detail::EnsureGuildRosterShowOfflineCVarBehavior(*this);
  RegisterCVar("guildRecruitmentChannel", "1", F::Account,
               "Whether to automatically join the guild recruitment channel when not in a guild");
  RegisterCVar("lfgAutoFill", "0", F::Account,
               "Whether to automatically add party members while looking for a group");
  RegisterCVar("lfgAutoJoin", "0", F::Account,
               "Whether to automatically join a party while looking for a group");
  RegisterCVar("friendsViewButtons", "0", F::Character,
               "Whether to show the friends list view buttons");
  RegisterCVar("friendsSmallView", "0", F::Character,
               "Whether to use smaller buttons in the friends list");
  RegisterCVar("wholeChatWindowClickable", "1", F::Account,
               "Whether the user may click anywhere on a chat window to change EditBox focus");
  RegisterCVar("conversationMode", "popout", F::Account,
               "The action new Real ID Conversations take by default: popout, inline");
  RegisterCVar("secureAbilityToggle", "1", F::Account,
               "Whether you should be protected against accidentally double-clicking an aura");
  RegisterCVar("CombatDamage", "1", F::Account,
               "Display damage numbers over hostile creatures when damaged");
  RegisterCVar("CombatLogPeriodicSpells", "1", F::Account,
               "Display damage caused by periodic effects");
  RegisterCVar("PetMeleeDamage", "1", F::Account, "Display pet melee damage in the world");
  RegisterCVar("PetSpellDamage", "1", F::Account, "Display pet spell damage in the world");
  RegisterCVar("CombatHealing", "1", F::Account, "Display amount of healing you did to the target");
  RegisterCVar("enableCombatText", "1", F::Account, "Whether to show floating combat text");
  RegisterCVar("fctCombatState", "0", F::Account, "Floating combat text: combat state");
  RegisterCVar("fctDodgeParryMiss", "0", F::Account, "Floating combat text: dodge/parry/miss");
  RegisterCVar("fctDamageReduction", "0", F::Account, "Floating combat text: damage reduction");
  RegisterCVar("fctRepChanges", "0", F::Account, "Floating combat text: reputation changes");
  RegisterCVar("fctReactives", "0", F::Account, "Floating combat text: reactives");
  RegisterCVar("fctFriendlyHealers", "0", F::Account, "Floating combat text: friendly healers");
  RegisterCVar("fctComboPoints", "0", F::Account, "Floating combat text: combo points");
  RegisterCVar("fctLowManaHealth", "1", F::Account, "Floating combat text: low mana/health");
  RegisterCVar("fctEnergyGains", "0", F::Account, "Floating combat text: energy gains");
  RegisterCVar("fctPeriodicEnergyGains", "0", F::Account,
               "Floating combat text: periodic energy gains");
  RegisterCVar("fctHonorGains", "0", F::Account, "Floating combat text: honor gains");
  RegisterCVar("fctAuras", "0", F::Account, "Floating combat text: auras");
  RegisterCVar("fctAllSpellMechanics", "0", F::Account,
               "Floating combat text: all spell mechanics");
  RegisterCVar("fctSpellMechanics", "0", F::Account, "Floating combat text: spell mechanics");
  RegisterCVar("fctSpellMechanicsOther", "0", F::Account,
               "Floating combat text: spell mechanics other");
  RegisterCVar("xpBarText", "0", F::Account,
               "Whether the XP bar shows the numeric experience value");
  RegisterCVar("playerStatusText", "0", F::Account,
               "Whether the player portrait shows numeric health/mana values");
  RegisterCVar("petStatusText", "0", F::Account,
               "Whether the pet portrait shows numeric health/mana values");
  RegisterCVar("partyStatusText", "0", F::Account,
               "Whether the party portraits shows numeric health/mana values");
  RegisterCVar("targetStatusText", "0", F::Account,
               "Whether the target portrait shows numeric health/mana values");
  RegisterCVar("statusTextPercentage", "0", F::Account,
               "Whether numeric health/mana values are shown as raw values or percentages");
  RegisterCVar("showPartyBackground", "0", F::Account, "Show a background behind party members");
  RegisterCVar("partyBackgroundOpacity", "0.5", F::Account, "The opacity of the party background");
  RegisterCVar("hidePartyInRaid", "0", F::Account, "Whether to hide the party UI while in a raid");
  RegisterCVar("showPartyPets", "1", F::Character, "Whether to show pets in the party UI");
  RegisterCVar("showRaidRange", "0", F::Character, "Show range indicator in raid UI");
  RegisterCVar("showArenaEnemyFrames", "1", F::Character,
               "Show arena enemy frames while in an Arena");
  RegisterCVar("showArenaEnemyCastbar", "1", F::Character,
               "Show the spell enemies are casting on the Arena Enemy frames");
  RegisterCVar("showArenaEnemyPets", "1", F::Character,
               "Show the enemy team's pets on the ArenaEnemy frames");
  RegisterCVar("fullSizeFocusFrame", "0", F::Character,
               "Increases the size of the focus frame to that of the target frame");
  RegisterCVar("showDispelDebuffs", "0", F::Character,
               "Show only Debuffs that the player can dispel");
  RegisterCVar("showCastableBuffs", "0", F::Character, "Show only Buffs the player can cast");
  RegisterCVar("consolidateBuffs", "0", F::Character, "Consolidates buffs displayed for the player");
  RegisterCVar("showCastableDebuffs", "0", F::Character, "Show only debuffs the player can apply");
  RegisterCVar("showToastOnline", "1", F::Account,
               "Whether to show Battle.net message for friend coming online");
  RegisterCVar("showToastOffline", "1", F::Account,
               "Whether to show Battle.net message for friend going offline");
  RegisterCVar("showToastBroadcast", "1", F::Account,
               "Whether to show Battle.net message for broadcasts");
  RegisterCVar("showToastFriendRequest", "1", F::Account,
               "Whether to show Battle.net message for friend requests");
  RegisterCVar("showToastConversation", "1", F::Account,
               "Whether to show Battle.net message for conversations");
  RegisterCVar("showToastWindow", "1", F::Account,
               "Whether to show Battle.net system messages in a toast window");
  RegisterCVar("toastDuration", "4", F::Account,
               "How long to display Battle.net toast windows, in seconds");
  RegisterCVar("calendarShowWeeklyHolidays", "1", F::Character,
               "Whether weekly holidays should appear in the calendar");
  RegisterCVar("calendarShowDarkmoon", "1", F::Character,
               "Whether Darkmoon Faire holidays should appear in the calendar");
  RegisterCVar("calendarShowBattlegrounds", "0", F::Character,
               "Whether Battleground holidays should appear in the calendar");
  RegisterCVar("calendarShowLockouts", "1", F::Character,
               "Whether raid lockouts should appear in the calendar");
  RegisterCVar("calendarShowResets", "0", F::Character,
               "Whether raid resets should appear in the calendar");
  InstallCalendarVisibilityFilterCallbacks(*this);

  RegisterCVar("nameplateShowEnemies", "0", F::Character, "");
  RegisterCVar("nameplateShowEnemyPets", "1", F::Character, "");
  RegisterCVar("nameplateShowEnemyGuardians", "1", F::Character, "");
  RegisterCVar("nameplateShowEnemyTotems", "1", F::Character, "");
  RegisterCVar("nameplateShowFriends", "0", F::Character, "");
  RegisterCVar("nameplateShowFriendlyPets", "1", F::Character, "");
  RegisterCVar("nameplateShowFriendlyGuardians", "1", F::Character, "");
  RegisterCVar("nameplateShowFriendlyTotems", "1", F::Character, "");
  RegisterCVar(
      "nameplateAllowOverlap", "1", F::Character,
      "switches between overlapping nameplates or the (old) never overlapping version");
  RegisterCVar("unitHighlights", "1", F::Account,
               "Whether the highlight circle around units should be displayed");
  RegisterCVar("enablePVPNotifyAFK", "1", F::Account,
               "The ability to shutdown the AFK notification system");
  RegisterCVar("serviceTypeFilter", "3", F::Account, "Which trainer services to show");
  RegisterCVar("autojoinPartyVoice", "1", F::Account,
               "Automatically join the voice session in party/raid chat");
  RegisterCVar("autojoinBGVoice", "0", F::Account,
               "Automatically join the voice session in battleground chat");
  RegisterCVar("PushToTalkSound", "0", F::Account,
               "Play a sound when voice recording activates and deactivates");
  RegisterCVar("combatLogOn", "1", F::Character, "Whether or not the combat log is shown");
  RegisterCVar("showKeyring", "0", F::Character, "Whether or not the keyring is shown");
  RegisterCVar("showBattlefieldMinimap", "0", F::Character,
               "Whether or not the battlefield minimap is shown");
  RegisterCVar("playerStatLeftDropdown", "", F::Character,
               "The player stat selected in the left dropdown");
  RegisterCVar("playerStatRightDropdown", "", F::Character,
               "The player stat selected in the right dropdown");
  RegisterCVar("talentFrameShown", "0", F::Account, "The talent UI has been shown");
  RegisterCVar("auctionDisplayOnCharacter", "0", F::Account,
               "Show auction items on the dress-up paperdoll");
  RegisterCVar("addFriendInfoShown", "0", F::Account, "The info for Add Friend has been shown");
  RegisterCVar("pendingInviteInfoShown", "0", F::Account,
               "The info for pending invites has been shown");
  RegisterCVar("timeMgrUseMilitaryTime", GetTimeMgrUseMilitaryTimeDefaultValue(), F::Account,
               "Toggles the display of either 12 or 24 hour time");
  RegisterCVar("timeMgrAlarmMessage", "", F::Account, "The time manager's alarm message");
  RegisterCVar(kCombatLogRetentionTimeCVarName, "300", F::Archive,
               "The maximum duration in seconds to retain combat log entries");
  InstallCombatLogRetentionCallback(*this);
  RegisterCVar("currencyTokensUnused1", "0", F::Character, "Currency token types marked as unused");
  RegisterCVar("currencyTokensUnused2", "0", F::Character, "Currency token types marked as unused");
  RegisterCVar("currencyTokensBackpack1", "0", F::Character,
               "Currency token types shown on backpack");
  RegisterCVar("currencyTokensBackpack2", "0", F::Character,
               "Currency token types shown on backpack");
  RegisterCVar("showTokenFrame", "0", F::Character, "The token UI has been shown");
  RegisterCVar("showTokenFrameHonor", "0", F::Character, "The token UI has shown Honor");
  RegisterCVar("predictedHealth", "1", F::Account,
               "Whether or not to use predicted health values in the UI");
  RegisterCVar("predictedPower", "1", F::Account,
               "Whether or not to use predicted power values in the UI");
  RegisterCVar("threatWorldText", "1", F::Account,
               "Whether or not to show threat floaters in combat");
  RegisterCVar("threatShowNumeric", "0", F::Account,
               "Whether or not to show numeric threat on the target and focus frames");
  RegisterCVar("ShowAllSpellRanks", "1", F::Account,
               "show either all spell ranks, or only the highest rank");

  RegisterCVar("ShowClassColorInNameplate", "0", F::Character,
               "use this to display the class color in the nameplate health bar");
  RegisterCVar("previewTalents", "0", F::Character,
               "Toggles the ability to preview talents before spending talent points");
  SetValidationCallback("previewTalents", PreviewTalentsCVarValidationCallback);
  RegisterCVar("lfgSelectedRoles", "0", F::Character | F::ConsoleReadOnly,
               "Stores what roles the player is willing to take on");
  RegisterCVar("lfdCollapsedHeaders", "", F::Character | F::ConsoleReadOnly, "Stores which LFD headers are collapsed");
  RegisterCVar("lfdSelectedDungeons", "", F::Character | F::ConsoleReadOnly,
               "Stores the archived enabled-state bits for LFD dungeon choices");
  RegisterCVar("lastTalkedToGM", "", F::Account, "Stores the last GM someone was talking to");
  RegisterCVar("autoCompleteResortNamesOnRecency", "1", F::Account,
               "Shows people you recently spoke with higher up on the AutoComplete list");
  RegisterCVar("autoCompleteWhenEditingFromCenter", "1", F::Account,
               "If you edit a name by inserting characters into the center, a smarter "
               "auto-complete will occur");
  RegisterCVar("autoCompleteUseContext", "1", F::Account,
               "The system will only show people in your guild when you are typing /gpromote");
  RegisterCVar(
      "colorChatNamesByClass", "0", F::Account,
      "If enabled, the name of a player speaking in chat will be colored according to his class");
  RegisterCVar("autoFilledMultiCastSlots", "0", F::Character,
               "Bitfield that saves whether multi-cast slots have been automatically filled");
  RegisterCVar("minimapTrackedInfo", "", F::Character,
               "Stores the minimap tracking that was active last session");
  RegisterCVar("questPOI", "1", F::Character, "If enabled, the quest POI system will be used");

  RegisterCVar("POIShiftComplete", "0.6", F::None, "");
  RegisterCVar("miniWorldMap", "0", F::Character,
               "Whether or not the world map has been toggled to smaller size");
  RegisterCVar("dontShowEquipmentSetsOnItems", "0", F::Account,
               "Don't show which equipment sets an item is associated with");
  RegisterCVar("scriptProfile", "0", F::Archive, "Whether or not script profiling is enabled");

  RegisterCVar("chatBubbles", "1", F::Account, "Show chat bubbles");
  RegisterCVar("chatBubblesParty", "1", F::Account, "Show party chat bubbles");
  RegisterCVar("chatStyle", "im", F::Account,
               "The style of Edit Boxes for the ChatFrame. Valid values: classic, im");
  RegisterCVar("profanityFilter", "1", F::Account, "Enable profanity filter");
  RegisterCVar("spamFilter", "1", F::Account, "Whether to enable spam filtering");
  RegisterCVar("showTimestamps", "none", F::Account, "Show timestamps in chat");
  RegisterCVar("chatMouseScroll", "1", F::Account, "Mouse scroll in chat");
  RegisterCVar("removeChatDelay", "0", F::Account, "Remove chat delay");
  RegisterCVar("guildMemberNotify", "0", F::Account,
               "Receive notification when guild members log on/off");

  RegisterCVar("useUiScale", "0", F::Archive, "Enable UI scaling");
  RegisterCVar("uiScale", "1.0", F::Archive, "UI scale factor", 0.64f, 1.0f);

  RegisterCVar("Gamma", "1.0", F::Archive, "Gamma correction");
  RegisterCVar("scriptErrors", "0", F::Account, "Show Lua script errors");
  RegisterCVar("taintLog", "0", F::Archive, "Taint logging");

  SetValidationCallback("taintLog", GameUI_TaintLogCVarValidationCallback);
  (void)GameUI_TaintLogCVarValidationCallback("taintLog", {}, GetCVar("taintLog"));
  RegisterCVar("showToolsUI", "-1", F::Archive, "Show tools UI");
  ApplyClientRegisterCVarsValueFixups();
  RegisterCVar("showClock", "1", F::Account, "Whether to display the time manager's clock button");
  RegisterCVar("colorblindMode", "0", F::Account, "Colorblind mode");
  openwow::game::Minimap_RegisterViolenceLevelCVar();
  RegisterCVar("minimapZoom", "3", F::Character, "The current outdoor minimap zoom level");
  RegisterCVar("rotateMinimap", "0", F::Account, "Rotate minimap with player");
  RegisterCVar("minimapInsideZoom", "3", F::Character, "Minimap inside zoom");
  RegisterCVar("autoQuestWatch", "1", F::Account, "Auto-track quests on accept");

  RegisterCVar("screenshotFormat", "jpeg", F::Archive, "Screenshot format");
  RegisterCVar("screenshotQuality", "3", F::Archive, "Screenshot quality", 1.0f, 10.0f);

  SetValidationCallback("screenshotFormat", ScreenshotFormatValidationCallback);
  SetValidationCallback("screenshotQuality", ScreenshotQualityValidationCallback);
  openwow::core::ScreenshotSystem::Instance().SetFormatCVarValue(GetCVar("screenshotFormat"));
  openwow::core::ScreenshotSystem::Instance().SetQualityCVarValue(GetCVar("screenshotQuality"));

  RegisterCVar("realmList", "us.logon.worldofwarcraft.com:3724", F::None,
               "Address of realm list server");
  RegisterCVar("realmName", "", F::Archive, "Current realm name");

  RegisterCVar("accountName", "", F::Archive | F::Hidden, "Saved account name");
  RegisterCVar("accounttype", "", F::None, "Account type");
  RegisterCVar("accountList", "", F::Archive, "List of wow accounts for saved Blizzard account");
  RegisterCVar("g_accountUsesToken", "0", F::Archive, "Saved whether uses authenticator");
  RegisterCVar("checkAddonVersion", "1", F::Archive, "Check interface addon version number");
  RegisterCVar("lastCharacterIndex", "0", F::Archive, "Last character selected");

  if (openwow::data::IsOnlineModeActive()) {
    RegisterCVar("converted", "0", F::Archive, "Trial to Retail");
  }

  RegisterCVar("locale", "****", F::Archive, "Client locale");
  RegisterCVar("portal", "", F::None, "Name of Battle.net portal to use");
  RegisterCVar("realmListbn", "", F::None, "Address of Battle.net server");
  RegisterCVar("dbCompress", "-1", F::Archive, "Database compression");
  RegisterCVar("processAffinityMask", "0", F::Archive | F::ReadOnly, "CPU affinity mask");
  SetValidationCallback("processAffinityMask", ProcessAffinityMaskValidationCallback);
  RegisterCVar(
      "timingMethod", "0", F::Archive,
      "Desired method for game timing (0=auto, 1=GetTickCount, 2=QueryPerformanceCounter)");

  RegisterCVar("Sound_EnableAllSound", "1", F::Archive, "");

  RegisterCVar("ChatSoundVolume", "0.4", F::Archive, "sound volume (0.0 to 1.0)");
  RegisterCVar("ChatMusicVolume", "0.3", F::Archive, "music volume (0.0 to 1.0)");
  RegisterCVar("ChatAmbienceVolume", "0.3", F::Archive, "Ambience Volume (0.0 to 1.0)");

  RegisterCVar("forceEnglishNames", "0", F::None, "", 0.0f, 0.0f, 5);

  const std::string mouse_speed_default =
      openwow::platform::SystemMouseSpeedController::Instance().CaptureSystemDefaultAsCVarValue();

  RegisterCVar("mouseSpeed", mouse_speed_default, F::Archive, "Mouse sensitivity");

  SetValidationCallback("mouseSpeed", MouseSpeedValidationCallback);

  RegisterCVar("timeMgrAlarmEnabled", "0", F::Account, "Alarm enabled");
  RegisterCVar("timeMgrAlarmTime", "0", F::Account, "Alarm time");
  RegisterCVar("timeMgrUseLocalTime", "0", F::Account, "Use local time");
  RegisterCVar("showTutorials", "1", F::Account, "Show tutorials");
  RegisterCVar("gameTip", "0", F::Archive, "Next loading-screen tip index");
  RegisterCVar("showGameTips", "1", F::Archive, "Show game tips");
  RegisterCVar("showItemLevel", "0", F::Account, "Show item level");
  RegisterCVar("assistAttack", "0", F::Character, "Whether to start attacking after an assist");

  RegisterCVar("movie", "1", F::Archive, "Play intro movie on startup");
  RegisterCVar("expansionMovie", "1", F::Archive, "Play expansion movie on startup");
  RegisterCVar("movieSubtitle", "0", F::Archive, "Show movie subtitles");
  RegisterCVar("MovieRecordingPath", "", F::Archive | F::Immutable, "Path for saving movies");
  RegisterCVar("MovieRecordingWidth", "640", F::Archive, "Movie width");
  RegisterCVar("MovieRecordingFramerate", "29.97", F::Archive, "Movie framerate");
  RegisterCVar("MovieRecordingSound", "1", F::Archive, "Enable sound in recorded movie");
  RegisterCVar("MovieRecordingGUI", "1", F::Archive,
               "Enable the user interface in recorded movie");
  RegisterCVar("MovieRecordingCursor", "0", F::Archive,
               "Enable the cursor in recorded movie");
  RegisterCVar("MovieRecordingQuality", "2", F::Archive,
               "Controls the quality of the movie.");
  RegisterCVar("MovieRecordingCompression", "1635148593", F::Archive,
               "Controls the compression of the movie.");
  RegisterCVar("MovieRecordingIcon", "1", F::Archive,
               "Show an icon on the minimap while recording.");
  RegisterCVar("MovieRecordingRecover", "1", F::Archive,
               "Check at login to compress any incomplete movies.");
  RegisterCVar("MovieRecordingAutoCompress", "1", F::Archive,
               "Compress the movie when the recording stops.");
  RegisterCVar("MovieRecordingForceEnable", "0", F::Archive,
               "Force movie recording to be enabled.");
  RegisterCVar("MovieRecordingGetTexImage", "0", F::Archive,
               "Use GetTexImage instead of ReadPixels.");
  RegisterCVar("readTOS", "0", F::Archive, "TOS accepted");
  RegisterCVar("readEULA", "0", F::Archive, "EULA accepted");
  RegisterCVar("readScanning", "0", F::Archive, "Scanning notice read");
  RegisterCVar("readContest", "0", F::Archive, "Contest notice read");
  RegisterCVar("readTerminationWithoutNotice", "0", F::Archive, "Termination notice read");

  RegisterCVar("textLocale", "enUS", F::ReadOnly | F::NoSave, "Text locale");
  RegisterCVar("audioLocale", "enUS", F::ReadOnly | F::NoSave, "Audio locale");

  openwow::render::RegisterSkyCVarDefaults(*this);
  RegisterCVar("shadowLOD", "1", F::Archive, "Shadow LOD quality",
               0.0f, 0.0f, 1);

  RegisterCVar("lod", "0", F::Archive, "Level of detail enable");

  RegisterCVar("Errors", "0", F::None, "Debug error output enable");
  RegisterCVar("ShowErrors", "1", F::None, "Show debug error dialog");
  RegisterCVar("ErrorLevelMin", "2", F::None, "Minimum error level to display");
  RegisterCVar("ErrorLevelMax", "3", F::None, "Maximum error level to display");
  RegisterCVar("ErrorFilter", "all", F::None, "Error category filter");
  SetValidationCallback("Errors", openwow::debug::CVar_Errors_Callback);
  SetValidationCallback("ShowErrors", openwow::debug::CVar_ShowErrors_Callback);
  SetValidationCallback("ErrorLevelMin", openwow::debug::CVar_ErrorLevelMin_Callback);
  SetValidationCallback("ErrorLevelMax", openwow::debug::CVar_ErrorLevelMax_Callback);
  SetValidationCallback("ErrorFilter", openwow::debug::CVar_ErrorFilter_Callback);

  RegisterCVar("DesktopGamma", "0", F::Archive, "Desktop gamma correction (WoW CVar name)");

  RegisterCVar("alwaysShowActionBars", "0", F::Account,
               "Whether to always show the action bar grid");
  RegisterCVar("lockActionBars", "1", F::Account,
               "Whether the action bars should be locked, preventing changes");

  RegisterCVar("timingTestError", "0", F::NoSave, "Timing test error threshold");

  RegisterCVar("autoDismount", "1", F::Account, "Auto-dismount on action");
  RegisterCVar("autoDismountFlying", "0", F::Account, "Auto-dismount while flying");
  RegisterCVar(
      "autoRangedCombat", "1", F::Account,
      "Whether your character will automatically switch between auto attack and auto shot");
  RegisterCVar(
      "threatWarning", "3", F::Character,
      "Whether or not to show threat warning UI (0=off, 1=dungeons, 2=party/raid, 3=always)", 0.0f,
      3.0f);
  RegisterCVar("threatPlaySounds", "1", F::Account,
               "Whether or not to play sounds when certain threat transitions occur");

  RegisterCVar("asyncThreadSleep", "0", F::Archive,
               "Engine option: Async read thread sleep");
  RegisterCVar("asyncHandlerTimeout", "100", F::Archive,
               "Engine option: Async read main thread timeout");

  RegisterCVar("ObjectSelectionCircle", "1", F::None, "");

  RegisterCVar("farClipOverride", "0", F::Archive, "Override old world graphic settings");
  RegisterCVar("showfootprints", "1", F::Archive, "toggles rendering of footprints");
  RegisterCVar("bspcache", "1", F::Archive, "BSP node caching");
  RegisterCVar("footstepBias", "0.125", F::Archive, "Unit footstep depth bias");
  RegisterCVar("occlusion", "1", F::Archive, "Use hardware occlusion test");
  RegisterCVar("worldPoolUsage", "Dynamic", F::Archive, "CGxPool usage static/dynamic");
  RegisterCVar("terrainAlphaBitDepth", "8", F::Archive, "Terrain alpha map bit depth");
  RegisterCVar("objectFade", "1", F::Archive, "Fade objects into view");
  RegisterCVar("objectFadeZFill", "0", F::Archive, "Fade objects using ZFill pass ");
  RegisterCVar("hwPCF", "1", F::Archive, "Hardware PCF Filtering");
  RegisterCVar("mapObjLightLOD", "0", F::Archive, "Map object light LOD");
  RegisterCVar("preferredFullscreenMode", "0", F::Archive, "preferred fullscreen mode");

  RegisterCVar("GLFaster", "1", F::None, "OpenGL multi threading on OS X (0/1/2)");
  RegisterCVar("UsePboSubImage", "0", F::None,
               "Allow TexSubImage2D calls to be routed through a streaming PBO (0/1)");
  RegisterCVar("UsePboSubImageZeroOffset", "1", F::None,
               "Force TexSubImage2D PBO-path calls to use zero offset in PBO (0/1)");
  RegisterCVar("NvidiaViewportFix", "-1", F::None,
               "Enable/Disable viewport fix for nvidia cards when rendering into a pbuffer or FBO (0/1)");
  RegisterCVar("FixVBOBug", "-1", F::None,
               "Work around a certain OpenGL bug on OS X (set to zero if 10.4.9 or higher)");

  RegisterCVar("shadowCull", "1", F::None, "enable shadow frustum culling");
  RegisterCVar("shadowScissor", "1", F::None, "enable scissoring when rendering shadowmaps");
  RegisterCVar("shadowInstancing", "1", F::None, "enable instancing when rendering shadowmaps");

  RegisterCVar("useWeatherShaders", "1", F::None, "");
  RegisterCVar("FootstepSounds", "1", F::None, "");

  RegisterCVar("useDesktopMouseSpeed", "0", F::None, "");

  RegisterCVar("EnableMultiTouch", "0", F::Archive,
               "Allow use of multi-touch tracking on newer mac touch pads.");
  RegisterCVar("MultiTouchAutoRunSensitivityX", "8", F::Archive,
               "Sensitivity along the x axis for the 3-touch auto-run feature.  A higher value will "
               "cause the player to turn faster.  EnableMultiTouch must be non-zero for this to "
               "take effect.  Minimum value is 1");
  RegisterCVar("MultiTouchAutoRunSensitivityY", "9", F::Archive,
               "Sensitivity along the y axis for the 3-touch auto-run feature.  A higher value will "
               "cause the camera to look up and down faster.  EnableMultiTouch must be non-zero for "
               "this to take effect.  Minimum value is 1");
  RegisterCVar("MultiTouchAutoRunInvertY", "0", F::Archive,
               "Invert Y axis for the 3-touch auto run feature.  EnableMultiTouch must be non-zero "
               "for this to take effect.");

  RegisterCVar("iTunesTrackDisplay", "0", F::None,
               "should iTunes Remote appear whenever a new song starts");
  RegisterCVar("iTunesRemoteFeedback", "1", F::None, "display iTunes Remote feedback");
}

}

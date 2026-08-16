#pragma once

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/ui/glue/glue_game_state.h"
#include "openwow/net/wotlk/protocol/world_protocol.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/vfs/virtual_file_system.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace openwow::render::m2 {
class M2System;
}

namespace openwow::ui::glue {

[[nodiscard]] inline bool IsGlueScreenName(std::string_view screen,
                                           std::string_view expected) {
  return openwow::text::EqualsIgnoreCaseAscii(screen, expected);
}

[[nodiscard]] inline bool UsesCharacterScreenHandler(std::string_view screen) {
  return IsGlueScreenName(screen, "charselect")
      || IsGlueScreenName(screen, "charcreate");
}

enum class GlueState : int {
  kIdle                  = 0,
  kAuthenticating        = 1,
  kConnecting            = 2,
  kCharListRetrieving    = 3,
  kRealmListPending      = 4,
  kCharCreateInProgress  = 5,
  kCharDeleteInProgress  = 6,
  kCharRenameInProgress  = 7,
  kCharDeclineInProgress = 8,
  kCharCustomizeInProgress = 9,
  kEnteringWorld         = 10,
  kCharEnumPending       = 11,
  kPatchDownload         = 12,
  kSurveyDownload        = 13,
  kScanDll               = 14,
};

[[nodiscard]] constexpr std::uint8_t GetRaceRequiredExpansionLevel(
    const int race_id) {
  switch (race_id) {
    case 10:
    case 11:
      return 1;
    default:
      return 0;
  }
}

[[nodiscard]] constexpr std::uint8_t GetClassRequiredExpansionLevel(
    const int class_id) {
  return class_id == 6 ? 2 : 0;
}

[[nodiscard]] constexpr bool IsRaceEnabledForExpansion(
    const int race_id, const std::uint8_t expansion_level) {
  return expansion_level >= GetRaceRequiredExpansionLevel(race_id);
}

[[nodiscard]] constexpr bool IsClassEnabledForExpansion(
    const int class_id, const std::uint8_t expansion_level) {
  return expansion_level >= GetClassRequiredExpansionLevel(class_id);
}

bool CGlueMgr_SyncDataFile(std::string_view filename,
                           std::string_view source_bytes,
                           const char* cvar_name,
                           bool locale_prefix);

void CGlueMgr_InitFFXEffects();

void Login_SetScreen(const GlueEventCallback& fire_event, std::string_view screen_name);

void CGlueMgr_HandlePatchFailure(const GlueEventCallback& fire_event, int error_code,
                                 unsigned int auxiliary_value);

void CGlueMgr_StartPatchDownload(GlueGameState& state,
                                 const GlueEventCallback& fire_event);

void CGlueMgr_UpdatePatchDownload(GlueGameState& state,
                                  const GlueEventCallback& fire_event,
                                  float dt_seconds);

void CGlueMgr_StartScanDll(GlueGameState& state,
                           const GlueEventCallback& fire_event,
                           std::string_view version_url,
                           std::string_view dll_url);
void CGlueMgr_UpdateScanDll(GlueGameState& state,
                            const GlueEventCallback& fire_event);

struct ScanDllExecutionResult {
  ScanDllStatus status{ScanDllStatus::kError};
  bool finished{false};
  bool continue_anyway_blocked{false};
  std::string result_primary_text;
  std::string result_secondary_text;
};

using ScanDllExecutorForTests =
    std::function<ScanDllExecutionResult(const std::filesystem::path&)>;

[[nodiscard]] float CGlueMgr_GetPatchDownloadProgress();

void CGlueMgr_ResetPatchDownloadRuntimeForTests();
void CGlueMgr_SetScanDllExecutorForTests(ScanDllExecutorForTests executor);
void CGlueMgr_ResetScanDllRuntimeForTests();

void CGlueMgr_SetGlueScreen(GlueGameState& state, const std::string& new_screen);

void CGlueMgr_ConnectToRealm(GlueGameState& state);

bool SetRealmName(GlueGameState& state, const std::string& name);

bool FindAndSelectSavedRealm(GlueGameState& state, bool force_locked);

void CGlueMgr_EnterWorld(GlueGameState& state);

void CGlueMgr_CleanupEnterWorldCharacterScenes(GlueGameState& state);

void CGlueMgr_CleanupCharCreateForEnterWorld(GlueGameState& state);

void CGlueMgr_SendCharCreate(GlueGameState& state);

void CGlueMgr_SendCharDelete(GlueGameState& state, std::uint64_t guid);

bool CGlueMgr_SendCharRename(GlueGameState& state, std::uint64_t guid,
                              const std::string& new_name);

bool CGlueMgr_SendDeclinedCharacterNames(
    GlueGameState& state, std::uint64_t guid,
    const std::array<std::string, 5>& declined_forms);

bool CGlueMgr_SendCharCustomize(GlueGameState& state, std::uint64_t guid,
                                 const std::string& name,
                                 std::uint8_t gender, std::uint8_t skin,
                                 std::uint8_t hair_style, std::uint8_t hair_color,
                                 std::uint8_t facial_hair, std::uint8_t face);

bool CGlueMgr_SendFactionChange(GlueGameState& state, std::uint64_t guid,
                                 const std::string& name,
                                 std::uint8_t gender, std::uint8_t skin,
                                 std::uint8_t hair_style, std::uint8_t hair_color,
                                 std::uint8_t facial_hair, std::uint8_t face,
                                 std::uint8_t race);

bool CGlueMgr_SendRaceChange(GlueGameState& state, std::uint64_t guid,
                              const std::string& name,
                              std::uint8_t gender, std::uint8_t skin,
                              std::uint8_t hair_style, std::uint8_t hair_color,
                              std::uint8_t facial_hair, std::uint8_t face,
                              std::uint8_t race);

void CGlueMgr_RequestCharacterList(GlueGameState& state);

void CGlueMgr_ResetCharacterListDisplay(GlueGameState& state);

void CGlueMgr_RequestRealmList(GlueGameState& state, bool show_dialog);

void CGlueMgr_ResetStateToIdle();

[[nodiscard]] int CGlueMgr_GetStateValue();
void CGlueMgr_SetStateValue(int state_value);

bool CGlueMgr_SetupCharLoginCamera(
    openwow::render::m2::M2System& m2_system,
    const openwow::data::dbc::DbcLoader& dbc,
    const openwow::vfs::VirtualFileSystem& vfs,
    std::uint8_t class_id,
    std::uint8_t race_id,
    float* out_camera_position_xyz);

const openwow::net::wotlk::CharacterSummary*
CGlueMgr_GetCharacterEntry(const GlueGameState& state, int index);

void CGlueMgr_SetDisconnectReason(std::uint32_t reason);

void CGlueMgr_RequestSilentDisconnect(GlueGameState* state);

bool CGlueMgr_NetDisconnectHandler(GlueGameState& state,
                                   const void* disconnected_connection);

bool CGlueMgr_NetDisconnectHandler(GlueGameState& state);

}

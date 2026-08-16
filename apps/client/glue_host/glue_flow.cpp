#include "glue_flow.h"
#include "realm_addon_handshake_composition.h"

#include "openwow/core/client_init.h"
#include "openwow/core/client_misc.h"
#include "openwow/core/cvar.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/core/init_subsystems.h"
#include "openwow/data/streaming_init.h"
#include "openwow/net/client_services.h"
#include "openwow/net/auth/login_matrix_challenge.h"
#include "openwow/net/auth/login_pin_challenge.h"
#include "openwow/net/auth/login_token_challenge.h"
#include "openwow/net/login_patch_download.h"
#include "openwow/net/login_survey_download.h"
#include "openwow/net/wotlk/glue_packet_handlers.h"
#include "openwow/net/wotlk/protocol/auth_protocol.h"
#include "openwow/net/adapters/presentation/char_enum_display.h"
#include "openwow/net/wotlk/realm_list.h"
#include "openwow/screens/loading_screen_manager.h"
#include "openwow/ui/addons_data.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/glue/cgluemgr.h"
#include "openwow/ui/glue/glue_charselect_scene.h"
#include "openwow/ui/glue/glue_lua_value.h"
#include "openwow/platform/filesystem/filesystem.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/vfs/sfile_core.h"
#include "openwow/vfs/virtual_file_system.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <utility>

namespace openwow::client {

using openwow::ui::glue::MakeLuaString;
using openwow::ui::glue::MakeLuaNumber;
using openwow::ui::glue::MakeLuaBool;
using openwow::ui::glue::GlueLuaValue;

namespace {

constexpr std::string_view kSavedAccountNameCVar = "accountName";

constexpr std::uint32_t kCharacterDeleteTransportTimeoutMs = 15'000;

int RawGlueStateForPhase(const GlueFlowState::Phase phase) {
  switch (phase) {
    case GlueFlowState::Phase::kIdle:
    case GlueFlowState::Phase::kWorldEnter:
    case GlueFlowState::Phase::kError:
    case GlueFlowState::Phase::kDisconnecting:
      return static_cast<int>(openwow::ui::glue::GlueState::kIdle);
    case GlueFlowState::Phase::kAuthInProgress:
      return static_cast<int>(openwow::ui::glue::GlueState::kAuthenticating);
    case GlueFlowState::Phase::kRealmListPending:
      return static_cast<int>(openwow::ui::glue::GlueState::kRealmListPending);
    case GlueFlowState::Phase::kWorldConnecting:
      return static_cast<int>(openwow::ui::glue::GlueState::kConnecting);
    case GlueFlowState::Phase::kCharListPending:
      return static_cast<int>(openwow::ui::glue::GlueState::kCharListRetrieving);
    case GlueFlowState::Phase::kCharCreating:
      return static_cast<int>(openwow::ui::glue::GlueState::kCharCreateInProgress);
    case GlueFlowState::Phase::kCharDeleting:
      return static_cast<int>(openwow::ui::glue::GlueState::kCharDeleteInProgress);
    case GlueFlowState::Phase::kCharRenaming:
      return static_cast<int>(openwow::ui::glue::GlueState::kCharRenameInProgress);
    case GlueFlowState::Phase::kCharCustomizing:
      return static_cast<int>(openwow::ui::glue::GlueState::kCharCustomizeInProgress);
    case GlueFlowState::Phase::kEnteringWorld:
      return static_cast<int>(openwow::ui::glue::GlueState::kEnteringWorld);
    case GlueFlowState::Phase::kCharDeclining:
      return static_cast<int>(openwow::ui::glue::GlueState::kCharDeclineInProgress);
  }

  return static_cast<int>(openwow::ui::glue::GlueState::kIdle);
}

void SetFlowPhase(GlueFlowState& state, const GlueFlowState::Phase phase) {
  state.phase = phase;
  const int raw_glue_state = RawGlueStateForPhase(phase);
  if (raw_glue_state == static_cast<int>(openwow::ui::glue::GlueState::kIdle)) {
    openwow::ui::glue::CGlueMgr_ResetStateToIdle();
    return;
  }

  openwow::ui::glue::CGlueMgr_SetStateValue(raw_glue_state);
}

std::string ResolveLoggedInAccountName(const GlueFlowContext& ctx);
void CloseStatusDialog(GlueFlowContext& ctx, GlueFlowState& state);
void OpenStatusDialogCancel(GlueFlowContext& ctx, GlueFlowState& state);
void UpdateStatusDialog(GlueFlowContext& ctx,
                        const std::string& text,
                        const std::string& secondary_text = {});

std::string ReadFirstNonEmptyText(openwow::ui::glue::GlueWidgetRuntime* widgets,
                                 const std::vector<std::string>& candidates) {
  if (widgets == nullptr) {
    return {};
  }
  for (const auto& name : candidates) {
    if (name.empty()) continue;
    const auto value = widgets->GetText(name);
    if (!value.empty()) {
      return value;
    }
  }
  return {};
}

std::string ResolveGlueStringOrFallback(const GlueFlowContext& ctx,
                                       const std::string& key,
                                       const std::string& fallback) {
  if (ctx.resolve_glue_string) {
    const auto resolved = ctx.resolve_glue_string(key);
    if (!resolved.empty()) {
      return resolved;
    }
  }
  return fallback;
}

const char* ResultKeyOrFallback(const std::int32_t result_code,
                                const char* const fallback = "") {
  const char* const key = openwow::net::ClientServices::GetResultString(result_code);
  return key != nullptr && *key != '\0' ? key : fallback;
}

std::string ResolveResultStringOrFallback(const GlueFlowContext& ctx,
                                          const std::int32_t result_code,
                                          const char* const fallback = "") {
  const char* const key = ResultKeyOrFallback(result_code, fallback);
  return ResolveGlueStringOrFallback(ctx, key, key);
}

std::string CharacterRenameFailureText(const GlueFlowContext& ctx,
                                       const std::int32_t result_code) {

  if (result_code == openwow::net::wotlk::CHAR_CREATE_NAME_IN_USE) {
    return ResultKeyOrFallback(result_code, "CHAR_CREATE_NAME_IN_USE");
  }
  return ResolveGlueStringOrFallback(ctx, "CHAR_RENAME_FAILED",
                                    "CHAR_RENAME_FAILED");
}

void ClearWorldConnectQueueProgress(GlueFlowState& state) {
  state.world_connect_queue_progress.reset();
  state.world_connect_used_fcm_dialog = false;
}

void DispatchPendingAccountMessagesEvent(GlueFlowContext& ctx,
                                         GlueFlowState& state) {
  if (!state.pending_account_messages_available || !ctx.fire_glue_event) {
    return;
  }

  const char* const event_name = openwow::ui::glue::GlueEventName(
      openwow::ui::glue::GlueScriptEvent::AccountMessagesAvailable);
  if (event_name == nullptr) {
    return;
  }

  ctx.fire_glue_event(event_name, {});
  state.pending_account_messages_available = false;
}

void ResetRealmAddonListUpdateState(GlueFlowState& state) {
  state.realm_addon_callback_epoch.store(0, std::memory_order_release);
  state.pending_realm_addon_list_update_epoch.store(0,
                                                    std::memory_order_release);
}

void ArmRealmAddonListUpdateCallback(GlueFlowContext& ctx,
                                     GlueFlowState& state) {
  if (ctx.realm_session == nullptr) {
    ResetRealmAddonListUpdateState(state);
    return;
  }

  const std::uint32_t epoch = ++state.next_realm_addon_callback_epoch;
  state.pending_realm_addon_list_update_epoch.store(0,
                                                    std::memory_order_release);
  state.realm_addon_callback_epoch.store(epoch, std::memory_order_release);

  auto* const active_epoch = &state.realm_addon_callback_epoch;
  auto* const pending_epoch = &state.pending_realm_addon_list_update_epoch;
  ctx.realm_session->SetAddonInfoProcessedCallback(
      [active_epoch, pending_epoch, epoch]() {
        if (active_epoch->load(std::memory_order_acquire) == epoch) {
          pending_epoch->store(epoch, std::memory_order_release);
        }
      });
}

void DrainPendingRealmAddonListUpdate(GlueFlowContext& ctx,
                                      GlueFlowState& state) {
  if (!ctx.fire_glue_event) {
    return;
  }

  const std::uint32_t active_epoch =
      state.realm_addon_callback_epoch.load(std::memory_order_acquire);
  if (active_epoch == 0) {
    return;
  }

  const std::uint32_t pending_epoch =
      state.pending_realm_addon_list_update_epoch.exchange(
          0, std::memory_order_acq_rel);
  if (pending_epoch != active_epoch) {
    return;
  }

  const char* const event_name = openwow::ui::glue::GlueEventName(
      openwow::ui::glue::GlueScriptEvent::AddonListUpdate);
  if (event_name != nullptr) {
    ctx.fire_glue_event(event_name, {});
  }
}

std::string FormatWorldQueueDialogText(
    const GlueFlowContext& ctx,
    const openwow::game::QueuePositionSnapshot& progress) {
  const bool has_realm_name = !progress.realm_name.empty();

  std::string key;
  if (!progress.has_estimated_wait) {
    key = has_realm_name ? "QUEUE_NAME_TIME_LEFT_UNKNOWN"
                         : "QUEUE_TIME_LEFT_UNKNOWN";
  } else if (progress.remaining_wait_ms < 60000) {
    key = has_realm_name ? "QUEUE_NAME_TIME_LEFT_SECONDS"
                         : "QUEUE_TIME_LEFT_SECONDS";
  } else {
    key = has_realm_name ? "QUEUE_NAME_TIME_LEFT"
                         : "QUEUE_TIME_LEFT";
  }

  const std::string format =
      ResolveGlueStringOrFallback(ctx, key, key);

  std::array<char, 512> message{};
  if (!progress.has_estimated_wait) {
    if (has_realm_name) {
      std::snprintf(message.data(), message.size(), format.c_str(),
                    progress.realm_name.c_str(), progress.position);
    } else {
      std::snprintf(message.data(), message.size(), format.c_str(),
                    progress.position);
    }
  } else if (progress.remaining_wait_ms < 60000) {
    if (has_realm_name) {
      std::snprintf(message.data(), message.size(), format.c_str(),
                    progress.realm_name.c_str(), progress.position);
    } else {
      std::snprintf(message.data(), message.size(), format.c_str(),
                    progress.position);
    }
  } else {
    const int remaining_minutes = progress.remaining_wait_ms / 60000;
    if (has_realm_name) {
      std::snprintf(message.data(), message.size(), format.c_str(),
                    progress.realm_name.c_str(), progress.position,
                    remaining_minutes);
    } else {
      std::snprintf(message.data(), message.size(), format.c_str(),
                    progress.position, remaining_minutes);
    }
  }

  std::string text = message.data();
  if (progress.free_character_migration) {
    text += "\n\n";
    text += ResolveGlueStringOrFallback(ctx, "QUEUE_FCM", "QUEUE_FCM");
  }
  return text;
}

void UpdateAccountTypeCVar(const std::uint8_t realm_expansion_level,
                           const bool is_trial_account) {
  std::string account_type;
  if (is_trial_account) {
    account_type = openwow::data::IsOnlineModeActive() ? "ST" : "RT";
  } else {
    switch (realm_expansion_level) {
      case 0:
        account_type = "CL";
        break;
      case 1:
        account_type = "BC";
        break;
      default:
        account_type = "LK";
        break;
    }
  }

  (void)openwow::ui::game::CVarSystem::Instance().SetCVar(
      "accounttype", account_type, true);
}

std::filesystem::path BuildConvertedTrialAccountPath(
    const GlueFlowContext& ctx) {
  const std::string account_name = ResolveLoggedInAccountName(ctx);
  if (account_name.empty()) {
    return {};
  }

  return std::filesystem::path("WTF") / "Account" / account_name;
}

bool HandleConvertedTrialTransition(GlueFlowContext& ctx,
                                    GlueFlowState& state) {
  if (!openwow::data::IsOnlineModeActive()) {
    return false;
  }

  auto& client_services = openwow::net::ClientServices::Instance();
  if (client_services.IsLoginConnectionTrialAccount()) {
    return false;
  }

  auto& cvars = openwow::ui::game::CVarSystem::Instance();
  if (cvars.GetCVarBool("converted")) {
    return false;
  }

  const auto account_path = BuildConvertedTrialAccountPath(ctx);
  const bool has_account_data =
      !account_path.empty()
      && openwow::vfs::FileSystem_IsDirectory(account_path.generic_string().c_str());
  if (has_account_data) {
    (void)cvars.SetCVar("converted", "1", true);
    openwow::core::SetConvertedTrialFlag(true);
    (void)openwow::core::ida::CVar_FlushToFile();
    CloseStatusDialog(ctx, state);
    state.status_dialog_open = false;
    state.last_status_text.clear();
    openwow::ui::glue::Login_SetScreen(ctx.fire_glue_event, "trialconvert");
    SetFlowPhase(state, GlueFlowState::Phase::kIdle);
    return true;
  }

  if (ctx.fire_glue_event) {
    ctx.fire_glue_event("CLIENT_TRIAL", {});
  }
  openwow::ui::glue::CGlueMgr_RequestSilentDisconnect(ctx.game_state);
  if (ctx.realm_session != nullptr) {
    ctx.realm_session->Disconnect();
  }
  client_services.DisconnectAndCleanup();
  client_services.Disconnect();
  SetFlowPhase(state, GlueFlowState::Phase::kIdle);
  return true;
}

bool HandleBattleNetUnknownAccountDisconnect(
    GlueFlowContext& ctx,
    GlueFlowState& state,
    const openwow::net::wotlk::AuthResult& result) {
  if (result.status != openwow::net::wotlk::AuthStatus::kAuthRejected) {
    return false;
  }

  if (result.login_status.state_code != 5
      || result.login_status.result_code != 21) {
    return false;
  }

  auto& client_services = openwow::net::ClientServices::Instance();
  if (client_services.GetLoginConnectionType()
      != openwow::net::LoginConnectionType::kBattleNet) {
    return false;
  }

  openwow::ui::glue::CGlueMgr_RequestSilentDisconnect(ctx.game_state);
  if (ctx.realm_session != nullptr) {
    ctx.realm_session->Disconnect();
  }

  client_services.DisconnectAndCleanup();
  client_services.Disconnect();

  if (ctx.fire_glue_event) {
    ctx.fire_glue_event("DISCONNECTED_FROM_SERVER", {MakeLuaNumber(0.0)});
  }

  SetFlowPhase(state, GlueFlowState::Phase::kIdle);
  return true;
}

std::uint32_t DecodeLeadingUtf8CodePoint(std::string_view text) {
  if (text.empty()) {
    return 0;
  }

  const auto first = static_cast<std::uint8_t>(text.front());
  if (first < 0x80u) {
    return first;
  }
  if ((first & 0xE0u) == 0xC0u && text.size() >= 2) {
    return ((first & 0x1Fu) << 6)
           | (static_cast<std::uint8_t>(text[1]) & 0x3Fu);
  }
  if ((first & 0xF0u) == 0xE0u && text.size() >= 3) {
    return ((first & 0x0Fu) << 12)
           | ((static_cast<std::uint8_t>(text[1]) & 0x3Fu) << 6)
           | (static_cast<std::uint8_t>(text[2]) & 0x3Fu);
  }
  if ((first & 0xF8u) == 0xF0u && text.size() >= 4) {
    return ((first & 0x07u) << 18)
           | ((static_cast<std::uint8_t>(text[1]) & 0x3Fu) << 12)
           | ((static_cast<std::uint8_t>(text[2]) & 0x3Fu) << 6)
           | (static_cast<std::uint8_t>(text[3]) & 0x3Fu);
  }
  return 0;
}

bool BeginsWithCyrillicCodePoint(std::string_view text) {
  const std::uint32_t code_point = DecodeLeadingUtf8CodePoint(text);
  return code_point >= 0x0400u && code_point <= 0x04FFu;
}

bool RequiresDeclinedNamesPrompt(
    const openwow::net::wotlk::CharacterSummary& character) {
  if ((character.char_flags & openwow::net::CharFlag::Declined) != 0) {
    return false;
  }
  if (openwow::net::ClientServices::Instance().GetCurrentLocale()
      != openwow::net::WowLocale::kRuRU) {
    return false;
  }
  return BeginsWithCyrillicCodePoint(character.name);
}

void PrepareEnterWorldLoadingTip(
    const openwow::net::wotlk::CharacterSummary& character) {
  if (character.is_first_login) {
    return;
  }

  auto& cvars = openwow::ui::game::CVarSystem::Instance();
  if (!cvars.Exists("gameTip")) {
    cvars.RegisterCVar("gameTip", "0", openwow::ui::game::CVarFlags::Archive,
                       "Next loading-screen tip index");
  }
  if (!cvars.Exists("showGameTips")) {
    cvars.RegisterCVar("showGameTips", "1",
                       openwow::ui::game::CVarFlags::Archive,
                       "Show game tips");
  }
  if (!cvars.GetCVarBool("showGameTips")) {
    return;
  }

  auto& loading_screen = openwow::screens::LoadingScreenManager::Get();
  const std::size_t tip_count = loading_screen.GetTipCount();
  if (tip_count == 0) {
    return;
  }

  int tip_index = cvars.GetCVarInt("gameTip");
  if (tip_index < 0 || tip_index >= static_cast<int>(tip_count)) {
    tip_index = 0;
  }

  if (const char* const tip_text =
      loading_screen.GetTipTextSourceByIndex(static_cast<std::size_t>(tip_index));
      tip_text != nullptr) {
    (void)openwow::core::LoadingScreen_SetTextSource(tip_text);
  }

  (void)cvars.SetCVar("gameTip", std::to_string(tip_index + 1), true);
}

void PrepareEnterWorldStreamingState(
    const openwow::net::wotlk::CharacterSummary& character) {
  if (!openwow::data::IsOnlineModeActive()) {
    return;
  }

  openwow::vfs::SetDataPreloadSelectedRace(static_cast<int>(character.race_id));
  const bool trial_gate_open = openwow::vfs::IsStartRaceDataPreloadGateOpen();
  openwow::vfs::SetDataPreloadRequestedState(2);
  (void)openwow::core::fn_TRIAL_LOADING_MESSAGE(trial_gate_open ? 0 : 1);
}

void PrepareEnterWorldLoadingState(
    const openwow::net::wotlk::CharacterSummary& character) {
  PrepareEnterWorldLoadingTip(character);
  PrepareEnterWorldStreamingState(character);
  openwow::core::LoadingScreen_InitFont(
      static_cast<int>(character.map_id), true);
  (void)openwow::core::ida::CVar_FlushToFile();
}

std::string LoginErrorText(const GlueFlowContext &ctx,
                           const openwow::net::wotlk::AuthResult &result) {
  using openwow::net::wotlk::AuthStatus;
  if (!result.login_status.result_key.empty()) {
    return ResolveGlueStringOrFallback(ctx, result.login_status.result_key,
                                       result.login_status.result_key);
  }
  if (!result.message.empty()) {
    return result.message;
  }
  switch (result.status) {
  case AuthStatus::kInvalidCredentials:
    return ResolveGlueStringOrFallback(ctx, "AUTH_FAILED", "Login failed");
  case AuthStatus::kNetworkError:
    return ResolveGlueStringOrFallback(ctx, "RESPONSE_FAILED_TO_CONNECT", "Failed to connect");
  case AuthStatus::kAuthRejected:
    return ResolveGlueStringOrFallback(ctx, "AUTH_REJECT", "Login rejected");
  case AuthStatus::kProtocolError:
    return ResolveGlueStringOrFallback(ctx, "AUTH_FAILED", "Login failed");
  case AuthStatus::kCancelled:
    return ResolveGlueStringOrFallback(ctx, "CANCEL", "Cancelled");
  case AuthStatus::kSuccess:
    return ResolveGlueStringOrFallback(ctx, "AUTH_OK", "Login OK");
  case AuthStatus::kPatchRequired:
  case AuthStatus::kPatchTransferComplete:
    return ResolveGlueStringOrFallback(ctx, "LOGIN_OK", "Login OK");
  }
  return ResolveGlueStringOrFallback(ctx, "AUTH_FAILED", "Login failed");
}

template <typename T> bool FutureReady(const std::future<T> &f) {
  return f.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
}

template <typename T>
bool FutureReady(const std::shared_future<T>& f) {
  return f.valid() &&
         f.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
}

std::vector<openwow::ui::AddonInfo> DiscoverRealmAddons(
    openwow::vfs::VirtualFileSystem discovery_vfs) {
  const auto started_at = std::chrono::steady_clock::now();
  std::vector<openwow::ui::AddonInfo> addons;
  try {
    addons = openwow::ui::AddonManager::DiscoverAddons(discovery_vfs);
  } catch (...) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "GlueFlow: addon discovery failed");
  }

  const auto enabled_count = std::count_if(
      addons.begin(), addons.end(),
      [](const openwow::ui::AddonInfo& addon) { return addon.enabled; });
  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started_at).count();
  openwow::diagnostics::Log(
      openwow::diagnostics::LogLevel::kInfo,
      "GlueFlow: addon discovery complete addons=" +
          std::to_string(addons.size()) +
          " enabled=" + std::to_string(enabled_count) +
          " mounts=" + std::to_string(discovery_vfs.mounts().size()) +
          " duration_ms=" + std::to_string(elapsed_ms));
  return addons;
}

void PublishRealmAddonsIfReady(GlueFlowState& state) {
  if (!state.addon_discovery_publish_pending ||
      state.addon_discovery_published ||
      !FutureReady(state.addon_discovery_future)) {
    return;
  }

  try {
    const auto& addons = state.addon_discovery_future.get();
    RealmAddonHandshakeComposition::PublishClientAddons(addons);
    openwow::ui::AddonManager::Get().PublishDiscoveredAddons(addons);
    state.addon_discovery_published = true;
  } catch (...) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "GlueFlow: addon discovery result unavailable");
  }
  state.addon_discovery_publish_pending = false;
}

void PrepareRealmAddonHandshake(
    const std::shared_future<std::vector<openwow::ui::AddonInfo>>& discovery) {
  if (!discovery.valid()) {
    return;
  }
  try {
    RealmAddonHandshakeComposition::PublishClientAddons(discovery.get());
  } catch (...) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "GlueFlow: addon discovery was unavailable for world auth");
  }
}

constexpr std::uint32_t kCompletedCharacterServiceAtLoginFlags = 0x00110001u;

void SetDialogType(GlueFlowContext& ctx, GlueFlowState& state,
                   openwow::ui::glue::StatusDialogType type);

std::string CharacterServiceGenericErrorString(const GlueFlowContext& ctx) {
  return ResolveResultStringOrFallback(
      ctx,
      static_cast<std::int32_t>(
          openwow::net::ConnectionResponse::kCharCreateNameInUse),
      "CHAR_CREATE_ERROR");
}

std::string CharCustomizeFailureString(const GlueFlowContext& ctx,
                                      const std::uint8_t code) {
  if (code == 0x32) {
    return CharacterServiceGenericErrorString(ctx);
  }
  return ResolveGlueStringOrFallback(ctx, "CHAR_CUSTOMIZE_FAILED", "CHAR_CUSTOMIZE_FAILED");
}

std::string CharFactionChangeFailureString(const GlueFlowContext& ctx,
                                          const std::uint8_t code) {
  const char* key = nullptr;
  switch (code) {
    case 0x32: return CharacterServiceGenericErrorString(ctx);
    case 0x3D: key = "CHAR_FACTION_CHANGE_STILL_IN_GUILD"; break;
    case 0x3E: key = "CHAR_FACTION_CHANGE_RACECLASS_RESTRICTED"; break;
    case 0x3F: key = "CHAR_FACTION_CHANGE_CHOOSE_RACE"; break;
    case 0x40: key = "CHAR_FACTION_CHANGE_ARENA_LEADER"; break;
    case 0x41: key = "CHAR_FACTION_CHANGE_DELETE_MAIL"; break;
    case 0x42: key = "CHAR_FACTION_CHANGE_SWAP_FACTION"; break;
    case 0x43: key = "CHAR_FACTION_CHANGE_RACE_ONLY"; break;
    case 0x44: key = "CHAR_FACTION_CHANGE_GOLD_LIMIT"; break;
    case 0x45: key = "CHAR_FACTION_CHANGE_FORCE_LOGIN"; break;
    default:   key = "CHAR_FACTION_CHANGE_FAILED"; break;
  }
  return ResolveGlueStringOrFallback(ctx, key, key);
}

bool MovePathIfPresentNoReplace(const std::filesystem::path& from,
                                const std::filesystem::path& to) {
  if (from.empty() || to.empty()) {
    return false;
  }

  std::error_code ec;
  if (!std::filesystem::exists(from, ec) || ec) {
    return false;
  }

  std::filesystem::create_directories(to.parent_path(), ec);
  if (ec) {
    return false;
  }

  return openwow::platform::filesystem::MovePathNoReplace(from, to);
}

std::string ResolveConnectedRealmName(const GlueFlowContext& ctx) {
  if (ctx.realm_session != nullptr && !ctx.realm_session->realm().name.empty()) {
    return ctx.realm_session->realm().name;
  }

  const auto* const gs = ctx.game_state;
  if (gs == nullptr) {
    return {};
  }

  const int realm_index = gs->selected_realm_index;
  if (realm_index < 0 || realm_index >= static_cast<int>(gs->realms.size())) {
    return {};
  }
  return gs->realms[static_cast<std::size_t>(realm_index)].name;
}

std::optional<std::size_t> ResolveWorldConnectRealmIndex(
    openwow::ui::glue::GlueGameState& gs) {
  if (gs.selected_realm_index >= 0
      && gs.selected_realm_index < static_cast<int>(gs.realms.size())) {
    return static_cast<std::size_t>(gs.selected_realm_index);
  }

  auto& cvars = openwow::ui::game::CVarSystem::Instance();
  const std::string realm_name = cvars.GetCVar("realmName");
  if (realm_name.empty()) {
    return std::nullopt;
  }

  for (std::size_t i = 0; i < gs.realms.size(); ++i) {
    if (openwow::text::EqualsIgnoreCaseAscii(gs.realms[i].name, realm_name)) {
      gs.selected_realm_index = static_cast<int>(i);
      return i;
    }
  }

  return std::nullopt;
}

std::string ResolveLoggedInAccountName(const GlueFlowContext&) {
  std::string account_name = openwow::net::ClientServices::Instance().GetAccountName();
  if (!account_name.empty()) {
    return account_name;
  }

  return openwow::ui::game::CVarSystem::Instance().GetCVar(
      std::string(kSavedAccountNameCVar));
}

void MoveCharacterScopedWtfDataIfRenamed(const GlueFlowContext& ctx,
                                         const std::string& old_name,
                                         const std::string& new_name) {
  if (old_name.empty() || new_name.empty() || old_name == new_name) {
    return;
  }

  const std::string account_name = ResolveLoggedInAccountName(ctx);
  const std::string realm_name = ResolveConnectedRealmName(ctx);
  if (account_name.empty() || realm_name.empty()) {
    return;
  }

  const auto account_root = std::filesystem::path("WTF") / "Account" / account_name;
  (void)MovePathIfPresentNoReplace(account_root / realm_name / old_name,
                                   account_root / realm_name / new_name);
  (void)MovePathIfPresentNoReplace(account_root / old_name,
                                   account_root / new_name);
  auto& addons_data = openwow::ui::AddOnsData::Get();
  addons_data.RemoveState(old_name.c_str());
  addons_data.LoadSavedStateForCharacter(account_name, realm_name, new_name);
}

void RefreshCharacterSelection(GlueFlowContext& ctx,
                               const openwow::ui::glue::GlueGameState& gs,
                               const std::uint64_t fallback_selected_id) {
  if (ctx.character_screen == nullptr) {
    return;
  }

  const auto selected_id = ctx.character_screen->selected_character_id();
  ctx.character_screen->SetCharacters(gs.characters);
  if (selected_id.has_value()) {
    ctx.character_screen->SelectCharacterById(*selected_id);
    return;
  }

  ctx.character_screen->SelectCharacterById(fallback_selected_id);
}

bool IsSelectedCharacterGuid(const openwow::ui::glue::GlueGameState& gs,
                             const std::uint64_t guid) {
  const int selected_index = gs.selected_character_index;
  return selected_index >= 0
      && selected_index < static_cast<int>(gs.characters.size())
      && gs.characters[static_cast<std::size_t>(selected_index)].id == guid;
}

void ReloadCharacterListAddonStates(
    const GlueFlowContext& ctx,
    const std::vector<openwow::net::wotlk::CharacterSummary>& characters) {
  auto& addons_data = openwow::ui::AddOnsData::Get();
  addons_data.ClearSavedStates();

  const std::string account_name = ResolveLoggedInAccountName(ctx);
  const std::string realm_name = ResolveConnectedRealmName(ctx);
  if (account_name.empty() || realm_name.empty()) {
    return;
  }

  for (const auto& character : characters) {
    if (character.name.empty()) {
      continue;
    }
    addons_data.LoadSavedStateForCharacter(account_name, realm_name,
                                           character.name);
  }
}

template <typename ResultT>
bool ApplyCharacterServiceUpdate(GlueFlowContext& ctx,
                                 const ResultT& result) {
  auto* const gs = ctx.game_state;
  if (gs == nullptr) {
    return false;
  }

  auto it = std::find_if(gs->characters.begin(), gs->characters.end(),
                         [&](const openwow::net::wotlk::CharacterSummary& character) {
                           return character.id == result.guid;
                         });
  if (it == gs->characters.end()) {
    return false;
  }

  const std::string old_name = it->name;

  it->name = result.name;
  if constexpr (requires { result.race; }) {
    if (result.race != 0) {
      it->race_id = result.race;
    }
  }
  it->gender = result.gender;
  it->skin = result.skin;
  it->face = result.face;
  it->hair_style = result.hair_style;
  it->hair_color = result.hair_color;
  it->facial_hair = result.facial_hair;
  it->at_login_flags &= ~kCompletedCharacterServiceAtLoginFlags;

  MoveCharacterScopedWtfDataIfRenamed(ctx, old_name, it->name);
  RefreshCharacterSelection(ctx, *gs, it->id);

  return true;
}

bool ApplyCharacterRenameUpdate(GlueFlowContext& ctx,
                                const std::uint64_t guid,
                                const std::string& new_name) {
  auto* const gs = ctx.game_state;
  if (gs == nullptr) {
    return false;
  }

  auto it = std::find_if(gs->characters.begin(), gs->characters.end(),
                         [&](const openwow::net::wotlk::CharacterSummary& character) {
                           return character.id == guid;
                         });
  if (it == gs->characters.end()) {
    return false;
  }

  const std::string old_name = it->name;
  it->name = new_name;
  it->char_flags &= ~(openwow::net::CharFlag::Rename | openwow::net::CharFlag::Declined);

  MoveCharacterScopedWtfDataIfRenamed(ctx, old_name, it->name);
  RefreshCharacterSelection(ctx, *gs, it->id);

  return true;
}

void OpenCharacterServiceErrorDialog(GlueFlowContext& ctx,
                                     GlueFlowState& state,
                                     const std::string& text) {
  SetDialogType(ctx, state, openwow::ui::glue::StatusDialogType::kOkay);
  state.status_dialog_open = true;
  state.last_status_text.clear();
  if (ctx.fire_glue_event) {
    ctx.fire_glue_event("OPEN_STATUS_DIALOG", {MakeLuaString("OKAY"), MakeLuaString(text)});
  }
}

void SetDialogType(GlueFlowContext& ctx, GlueFlowState& state,
                   openwow::ui::glue::StatusDialogType type) {
  state.status_dialog_type = type;
  if (ctx.game_state != nullptr) {
    ctx.game_state->status_dialog_type = type;
  }
}

void OpenStatusDialogCancel(GlueFlowContext& ctx, GlueFlowState& state) {
  SetDialogType(ctx, state, openwow::ui::glue::StatusDialogType::kCancel);
  if (!ctx.fire_glue_event) return;
  ctx.fire_glue_event("OPEN_STATUS_DIALOG", {MakeLuaString("CANCEL")});
}

void UpdateStatusDialog(GlueFlowContext& ctx,
                        const std::string& text,
                        const std::string& secondary_text) {
  if (!ctx.fire_glue_event || text.empty()) return;
  std::vector<GlueLuaValue> args;
  args.emplace_back(MakeLuaString(text));
  if (!secondary_text.empty()) {
    args.emplace_back(MakeLuaString(secondary_text));
  }
  ctx.fire_glue_event("UPDATE_STATUS_DIALOG", args);
}

void CloseStatusDialog(GlueFlowContext& ctx, GlueFlowState& state) {
  SetDialogType(ctx, state, openwow::ui::glue::StatusDialogType::kNone);
  if (!ctx.fire_glue_event) return;
  ctx.fire_glue_event("CLOSE_STATUS_DIALOG", {});
}

void OpenLoginStateDialogEvent(GlueFlowContext& ctx,
                               GlueFlowState& state,
                               const openwow::core::LoginDialogEvent& dialog_event) {
  if (dialog_event.args.empty()) {
    return;
  }

  const bool cancel_dialog = dialog_event.args.front() == "CANCEL";
  SetDialogType(ctx, state,
                cancel_dialog ? openwow::ui::glue::StatusDialogType::kCancel
                              : openwow::ui::glue::StatusDialogType::kOkay);
  state.status_dialog_open = true;
  state.last_status_text.clear();

  if (!ctx.fire_glue_event) {
    return;
  }

  std::vector<GlueLuaValue> args;
  args.reserve(dialog_event.args.size());
  for (const std::string& value : dialog_event.args) {
    args.emplace_back(MakeLuaString(value));
  }
  ctx.fire_glue_event("OPEN_STATUS_DIALOG", args);
}

bool OpenAuthRejectedDialog(GlueFlowContext& ctx,
                            GlueFlowState& state,
                            const openwow::net::wotlk::AuthResult& result) {
  if (result.status != openwow::net::wotlk::AuthStatus::kAuthRejected) {
    return false;
  }

  const auto dialog_event = state.login_state_dialog_handler.Poll(
      static_cast<std::int32_t>(result.login_status.state_code),
      static_cast<std::int32_t>(result.login_status.result_code),
      [&](const std::string_view key) {
        return ResolveGlueStringOrFallback(ctx, std::string(key), std::string(key));
      });
  if (!dialog_event.has_value()) {
    return false;
  }

  OpenLoginStateDialogEvent(ctx, state, *dialog_event);
  return true;
}

void ClearSelectedCharacterPresentation(openwow::ui::glue::GlueGameState& gs) {
  gs.selected_character_index = -1;
  gs.wants_enter_world = false;
  if (gs.char_select_scene != nullptr) {
    gs.char_select_scene->SyncFromGameState(gs);
  }
}

void ResetCharacterListForRefresh(GlueFlowContext& ctx,
                                  openwow::ui::glue::GlueGameState& gs) {
  openwow::ui::glue::CGlueMgr_ResetCharacterListDisplay(gs);
  if (ctx.character_screen != nullptr) {
    ctx.character_screen->SetCharacters({});
  }
}

std::array<std::uint32_t, 12> BuildRaceClassRestrictionMasks(
    const std::array<std::uint32_t, 10>& trailing_u32s) {
  std::array<std::uint32_t, 12> masks{};
  constexpr std::array<int, 10> kRaceIdsByTrailingIndex = {
      1, 3, 7, 4, 11, 2, 8, 6, 5, 10,
  };
  for (std::size_t index = 0; index < kRaceIdsByTrailingIndex.size(); ++index) {
    masks[static_cast<std::size_t>(kRaceIdsByTrailingIndex[index])] =
        trailing_u32s[index];
  }
  return masks;
}

int ResolveInitialCharacterSelection(
    const std::vector<openwow::net::wotlk::CharacterSummary>& characters) {
  if (characters.empty()) {
    return 0;
  }

  const int saved_index =
      openwow::ui::game::CVarSystem::Instance().GetCVarInt("lastCharacterIndex");
  if (saved_index < 0 || saved_index >= static_cast<int>(characters.size())) {
    return 0;
  }
  return saved_index;
}

void ApplyCharacterListResult(GlueFlowContext& ctx,
                              openwow::ui::glue::GlueGameState& gs,
                              const openwow::net::wotlk::CharacterListResult& result) {
  ReloadCharacterListAddonStates(ctx, result.characters);
  ClearSelectedCharacterPresentation(gs);
  gs.characters = result.characters;
  gs.race_class_restriction_masks =
      BuildRaceClassRestrictionMasks(result.trailing_u32s);
  gs.selected_character_index = ResolveInitialCharacterSelection(gs.characters);

  if (ctx.character_screen != nullptr) {
    ctx.character_screen->SetCharacters(gs.characters);
    if (!gs.characters.empty() &&
        gs.selected_character_index >= 0 &&
        gs.selected_character_index < static_cast<int>(gs.characters.size())) {
      ctx.character_screen->SelectCharacterById(
          gs.characters[static_cast<std::size_t>(gs.selected_character_index)].id);
    }
  }

  if (gs.char_select_scene != nullptr) {
    gs.char_select_scene->SyncFromGameState(gs);
  }
}

void BeginCharacterListRefresh(GlueFlowContext& ctx,
                               GlueFlowState& state) {
  auto* const session = ctx.realm_session;
  auto* const game_state = ctx.game_state;
  if (session == nullptr || game_state == nullptr) {
    return;
  }

  ResetCharacterListForRefresh(ctx, *game_state);
  state.cancel_requested.store(false, std::memory_order_release);
  SetFlowPhase(state, GlueFlowState::Phase::kCharListPending);
  const auto should_cancel = [&state] {
    return state.cancel_requested.load(std::memory_order_acquire);
  };
  state.charlist_future.emplace(std::async(std::launch::async, [session, should_cancel]() {
    return session->FetchCharacterList(5000, should_cancel);
  }));
}

void StartCharacterListRefresh(GlueFlowContext& ctx,
                               GlueFlowState& state) {
  if (state.phase != GlueFlowState::Phase::kIdle || ctx.realm_session == nullptr
      || ctx.game_state == nullptr) {
    return;
  }

  BeginCharacterListRefresh(ctx, state);
  const bool reuse_existing_dialog =
      ctx.game_state->status_dialog_type
      == openwow::ui::glue::StatusDialogType::kCancel;
  if (!reuse_existing_dialog) {
    SetDialogType(ctx, state, openwow::ui::glue::StatusDialogType::kCancel);
    if (ctx.fire_glue_event) {
      ctx.fire_glue_event(
          "OPEN_STATUS_DIALOG",
          {MakeLuaString("CANCEL"),
           MakeLuaString(ResolveGlueStringOrFallback(
               ctx, "CHAR_LIST_RETRIEVING", "CHAR_LIST_RETRIEVING"))});
    }
  } else {
    SetDialogType(ctx, state, openwow::ui::glue::StatusDialogType::kCancel);
  }
  state.status_dialog_open = true;
  state.last_status_text.clear();
}

bool BeginRealmListFetch(GlueFlowContext& ctx,
                         GlueFlowState& state,
                         const bool show_dialog,
                         const bool dialog_already_open,
                         const bool transitions_after_login) {
  const auto auth = state.auth_protocol;

  if (auth == nullptr || !auth->CanRequestRealmList()) {
    auto& client_services = openwow::net::ClientServices::Instance();
    if (client_services.GetCurrentOperation()
            == openwow::net::ClientOperation::kGetRealms
        && !client_services.IsOperationComplete()) {
      client_services.CompletePendingOperation(
          openwow::net::ConnectionResponse::kRealmListFailed);
    }
    if (show_dialog && dialog_already_open) {
      CloseStatusDialog(ctx, state);
      state.status_dialog_open = false;
      state.last_status_text.clear();
    }
    if (ctx.fire_glue_event) {
      ctx.fire_glue_event("OPEN_REALM_LIST", {});
    }
    return false;
  }

  if (!transitions_after_login && state.phase != GlueFlowState::Phase::kIdle) {
    if (show_dialog && dialog_already_open) {
      CloseStatusDialog(ctx, state);
      state.status_dialog_open = false;
      state.last_status_text.clear();
    }
    return false;
  }

  state.cancel_requested.store(false);
  SetFlowPhase(state, GlueFlowState::Phase::kRealmListPending);
  state.realm_fetch_transitions = transitions_after_login;
  if (show_dialog) {
    if (dialog_already_open) {
      SetDialogType(ctx, state, openwow::ui::glue::StatusDialogType::kCancel);
    } else {
      OpenStatusDialogCancel(ctx, state);
    }
    state.status_dialog_open = true;
    state.last_status_text.clear();
    if (!dialog_already_open) {
      UpdateStatusDialog(
          ctx,
          ResolveGlueStringOrFallback(ctx, "REALM_LIST_IN_PROGRESS",
                                      "Retrieving realm list..."));
    }
  }

  const auto should_cancel = [&]() { return state.cancel_requested.load(); };
  state.realm_future.emplace(
      std::async(std::launch::async, [auth, should_cancel]() {
        return auth->RequestRealmList(5000, should_cancel);
      }));
  return true;
}

void CompleteRealmListResult(
    GlueFlowContext& ctx,
    GlueFlowState& state,
    std::vector<openwow::net::wotlk::RealmInfo> realms,
    const bool transitions_after_login) {
  auto& gs = *ctx.game_state;

  PublishRealmAddonsIfReady(state);
  const bool has_realms = !realms.empty();
  auto& client_services = openwow::net::ClientServices::Instance();
  if (client_services.GetCurrentOperation()
          == openwow::net::ClientOperation::kGetRealms
      && !client_services.IsOperationComplete()) {
    client_services.CompletePendingOperation(
        has_realms
            ? openwow::net::ConnectionResponse::kRealmListSuccess
            : openwow::net::ConnectionResponse::kRealmListFailed);
  }

  if (has_realms) {
    if (ctx.realm_screen != nullptr) {
      ctx.realm_screen->SetRealms(realms);
    }
    gs.realms = std::move(realms);
    gs.ResetRealmListCategoryState();
    if (ctx.show_error != nullptr) {
      *ctx.show_error = false;
    }
    if (state.status_dialog_open) {
      UpdateStatusDialog(
          ctx,
          ResolveGlueStringOrFallback(ctx, "REALM_LIST_SUCCESS",
                                      "Realm list loaded."));
    }
  } else {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "GlueFlow: realm list was unavailable or empty");
    gs.realms.clear();
    gs.ResetRealmListCategoryState();
    if (ctx.realm_screen != nullptr) {
      ctx.realm_screen->SetRealms({});
    }
    if (ctx.show_error != nullptr) {
      *ctx.show_error = true;
    }
    if (state.status_dialog_open) {
      UpdateStatusDialog(
          ctx,
          ResolveGlueStringOrFallback(ctx, "REALM_LIST_FAILED",
                                      "No realms are currently available."));
    }
  }

  if (transitions_after_login) {
    gs.connected = false;
    gs.selected_realm_index = -1;
    gs.selected_character_index = -1;
    gs.characters.clear();
    gs.wants_enter_world = false;
    openwow::net::ClientServices::Instance().ClearSelectedRealmScriptMetadata();
  }

  if (state.status_dialog_open) {
    CloseStatusDialog(ctx, state);
    state.status_dialog_open = false;
  }
  state.last_status_text.clear();

  if (!transitions_after_login && ctx.fire_glue_event) {
    ctx.fire_glue_event("OPEN_REALM_LIST", {});
  }
  if (transitions_after_login && ctx.after_login_success) {
    ctx.after_login_success();
  }

  SetFlowPhase(state, GlueFlowState::Phase::kIdle);
  state.realm_fetch_transitions = false;
  DispatchPendingAccountMessagesEvent(ctx, state);
}

void ShowStatusAndReset(GlueFlowContext& ctx, GlueFlowState& state, const std::string& text) {
  if (state.status_dialog_open) {
    CloseStatusDialog(ctx, state);
    state.status_dialog_open = false;
  }
  state.last_status_text.clear();
  SetDialogType(ctx, state, openwow::ui::glue::StatusDialogType::kOkay);
  if (ctx.fire_glue_event) {
    ctx.fire_glue_event("OPEN_STATUS_DIALOG", {MakeLuaString("OKAY"), MakeLuaString(text)});
  }
}

void ResetRealmSessionFutures(GlueFlowState& state) {
  state.world_connect_future.reset();
  state.charlist_future.reset();
  state.char_create_future.reset();
  state.char_delete_future.reset();
  state.char_rename_future.reset();
  state.char_decline_future.reset();
  state.char_customize_future.reset();
  state.char_faction_change_future.reset();
  state.char_race_change_future.reset();
  state.world_enter_future.reset();
  ClearWorldConnectQueueProgress(state);
  ResetRealmAddonListUpdateState(state);
}

bool HasRealmSessionFuture(const GlueFlowState& state) {
  return state.world_connect_future.has_value()
      || state.charlist_future.has_value()
      || state.char_create_future.has_value()
      || state.char_delete_future.has_value()
      || state.char_rename_future.has_value()
      || state.char_decline_future.has_value()
      || state.char_customize_future.has_value()
      || state.char_faction_change_future.has_value()
      || state.char_race_change_future.has_value()
      || state.world_enter_future.has_value();
}

void DisconnectAuthProtocol(GlueFlowState& state) {
  openwow::net::ClientServices::Instance().SetLoginFileTransferResponseSender({});
  if (state.auth_protocol != nullptr) {
    state.auth_protocol->Disconnect();
    state.auth_protocol.reset();
  }
}

void CancelAllNetworkFutures(GlueFlowContext& ctx, GlueFlowState& state) {
  state.cancel_requested.store(true);

  if (state.auth_protocol != nullptr) {
    state.auth_protocol->CancelPendingIo();
  }
  const bool has_realm_future =
      HasRealmSessionFuture(state) && ctx.realm_session != nullptr;
  if (has_realm_future) {
    ctx.realm_session->CancelPendingIo();
  }

  state.auth_future.reset();
  state.realm_future.reset();
  ResetRealmSessionFutures(state);
  DisconnectAuthProtocol(state);
  if (has_realm_future) {
    ctx.realm_session->Disconnect();
  }
}

void ResetLoginNetFutures(GlueFlowState& state) {
  state.cancel_requested.store(true);

  if (state.auth_protocol != nullptr) {
    state.auth_protocol->CancelPendingIo();
  }
  state.auth_future.reset();
  state.realm_future.reset();
  DisconnectAuthProtocol(state);
  state.disconnect_requested = false;
  state.disconnect_timer = 0.0f;
  state.realm_fetch_transitions = false;
}

void ResetSelectedCharacterPresentationToFirstSlot(
    GlueFlowContext& ctx,
    openwow::ui::glue::GlueGameState& gs) {
  gs.wants_enter_world = false;
  gs.selected_character_index = 0;

  if (ctx.character_screen != nullptr && !gs.characters.empty()) {
    ctx.character_screen->SelectCharacterById(gs.characters.front().id);
  }
  if (gs.char_select_scene != nullptr) {
    gs.char_select_scene->SyncFromGameState(gs);
  }
  if (ctx.fire_glue_event) {
    ctx.fire_glue_event("UPDATE_SELECTED_CHARACTER", {MakeLuaNumber(1.0)});
    ctx.fire_glue_event("CHARACTER_LIST_UPDATE", {});
  }
}

void CancelRealmConnection(GlueFlowContext& ctx,
                           GlueFlowState& state,
                           openwow::ui::glue::GlueGameState& gs) {
  state.cancel_requested.store(true);

  if (ctx.realm_session != nullptr) {
    ctx.realm_session->CancelPendingIo();
  }
  ResetRealmSessionFutures(state);
  if (ctx.realm_session != nullptr) {
    ctx.realm_session->Disconnect();
  }
  if ((state.phase == GlueFlowState::Phase::kEnteringWorld
       || state.phase == GlueFlowState::Phase::kWorldEnter)
      && ctx.abort_enter_world_init) {
    ctx.abort_enter_world_init();
  }
  gs.connected = false;
  gs.wants_enter_world = false;

  SetDialogType(ctx, state, openwow::ui::glue::StatusDialogType::kNone);
  state.status_dialog_open = false;
  state.last_status_text.clear();
  SetFlowPhase(state, GlueFlowState::Phase::kIdle);
}

void AbortLoginNetOperation(GlueFlowContext& ctx,
                            GlueFlowState& state) {
  ResetLoginNetFutures(state);
  if (state.status_dialog_open) {
    CloseStatusDialog(ctx, state);
    state.status_dialog_open = false;
  }
  state.last_status_text.clear();
  openwow::net::ClientServices::Instance().Disconnect();
  SetFlowPhase(state, GlueFlowState::Phase::kIdle);
}

void CancelRealmListPendingOperation(GlueFlowContext& ctx,
                                     GlueFlowState& state) {
  openwow::net::ClientServices::Instance().CompletePendingOperation(
      openwow::net::ConnectionResponse::kCancelled);
  openwow::ui::glue::CGlueMgr_ResetStateToIdle();

  ResetLoginNetFutures(state);

  SetDialogType(ctx, state, openwow::ui::glue::StatusDialogType::kNone);
  state.status_dialog_open = false;
  state.last_status_text.clear();
  SetFlowPhase(state, GlueFlowState::Phase::kIdle);
}

void AbortSurveyAndDisconnect(GlueFlowContext& ctx,
                              GlueFlowState& state) {
  openwow::net::LoginSurveyDownloadBridge::Get().AbortAndDisconnect();
  AbortLoginNetOperation(ctx, state);
}

void AbortPatchAndDisconnect(GlueFlowContext& ctx,
                             GlueFlowState& state) {
  openwow::net::LoginPatchDownloadBridge::Get().AbortActiveDownload();
  openwow::ui::glue::CGlueMgr_ResetStateToIdle();
  openwow::net::ClientServices::Instance().Disconnect();
  AbortLoginNetOperation(ctx, state);
}

void RequestLoginNetDisconnect(GlueFlowState& state) {
  const bool login_net_operation_active =
      state.phase == GlueFlowState::Phase::kAuthInProgress ||
      state.phase == GlueFlowState::Phase::kRealmListPending;
  if (login_net_operation_active) {
    state.cancel_requested.store(true);
  }

  if (state.auth_protocol != nullptr) {
    state.auth_protocol->CancelPendingIo();
  }
}

void CancelByState(GlueFlowContext& ctx,
                   GlueFlowState& state,
                   openwow::ui::glue::GlueGameState& gs) {
  const int raw_glue_state = openwow::ui::glue::CGlueMgr_GetStateValue();

  if (openwow::text::EqualsIgnoreCaseAscii(gs.current_screen, "patchdownload")) {
    gs.current_screen = "login";
    openwow::ui::glue::Login_SetScreen(ctx.fire_glue_event, "login");
  }

  gs.wants_login = false;
  gs.wants_enter_world = false;
  gs.ResetLoginRequest();

  switch (raw_glue_state) {
    case static_cast<int>(openwow::ui::glue::GlueState::kIdle):
      if (state.status_dialog_open) {
        CloseStatusDialog(ctx, state);
        state.status_dialog_open = false;
      }
      state.last_status_text.clear();
      if (state.phase == GlueFlowState::Phase::kError) {
        state.error_message.clear();
        state.error_display_timer = 0.0f;
        if (ctx.show_error != nullptr) {
          *ctx.show_error = false;
        }
        SetFlowPhase(state, GlueFlowState::Phase::kIdle);
      }
      break;

    case static_cast<int>(openwow::ui::glue::GlueState::kAuthenticating):
      AbortLoginNetOperation(ctx, state);
      break;

    case static_cast<int>(openwow::ui::glue::GlueState::kConnecting):
    case static_cast<int>(openwow::ui::glue::GlueState::kCharListRetrieving): {
      openwow::net::ClientServices::Instance().CompletePendingOperation(
          openwow::net::ConnectionResponse::kCancelled);
      if (state.phase == GlueFlowState::Phase::kWorldConnecting
          && ctx.realm_session != nullptr) {
        state.cancel_requested.store(true);
        ctx.realm_session->CancelPendingIo();
        gs.connected = false;
      }
      const bool cancelling_character_list =
          state.phase == GlueFlowState::Phase::kCharListPending;
      if (cancelling_character_list) {

        state.cancel_requested.store(true, std::memory_order_release);
      }

      state.world_connect_future.reset();
      state.charlist_future.reset();
      if (state.phase == GlueFlowState::Phase::kWorldConnecting
          && ctx.realm_session != nullptr) {
        ctx.realm_session->Disconnect();
      }
      if (cancelling_character_list) {
        state.cancel_requested.store(false, std::memory_order_release);
      }
      ClearWorldConnectQueueProgress(state);
      ResetRealmAddonListUpdateState(state);

      SetDialogType(ctx, state, openwow::ui::glue::StatusDialogType::kNone);
      state.status_dialog_open = false;
      state.last_status_text.clear();
      SetFlowPhase(state, GlueFlowState::Phase::kIdle);
      ResetSelectedCharacterPresentationToFirstSlot(ctx, gs);
      break;
    }

    case static_cast<int>(openwow::ui::glue::GlueState::kRealmListPending):
      CancelRealmListPendingOperation(ctx, state);
      break;

    case static_cast<int>(openwow::ui::glue::GlueState::kCharCreateInProgress):
    case static_cast<int>(openwow::ui::glue::GlueState::kCharDeleteInProgress):
    case static_cast<int>(openwow::ui::glue::GlueState::kEnteringWorld):
      CancelRealmConnection(ctx, state, gs);
      break;

    case static_cast<int>(openwow::ui::glue::GlueState::kCharRenameInProgress):
    case static_cast<int>(openwow::ui::glue::GlueState::kCharDeclineInProgress):
    case static_cast<int>(openwow::ui::glue::GlueState::kCharCustomizeInProgress):
      if (state.status_dialog_open) {
        CloseStatusDialog(ctx, state);
        state.status_dialog_open = false;
      }
      state.last_status_text.clear();
      openwow::ui::glue::CGlueMgr_ResetStateToIdle();
      break;

    case static_cast<int>(openwow::ui::glue::GlueState::kCharEnumPending):
      openwow::ui::glue::CGlueMgr_ResetStateToIdle();
      SetFlowPhase(state, GlueFlowState::Phase::kIdle);
      openwow::ui::glue::CGlueMgr_RequestCharacterList(gs);
      gs.wants_character_list_refresh = false;
      StartCharacterListRefresh(ctx, state);
      break;

    case static_cast<int>(openwow::ui::glue::GlueState::kPatchDownload):
    case static_cast<int>(openwow::ui::glue::GlueState::kSurveyDownload):
      if (openwow::net::LoginPatchDownloadBridge::Get().HasActiveDownload()) {
        AbortPatchAndDisconnect(ctx, state);
      } else if (openwow::net::LoginSurveyDownloadBridge::Get().HasActiveDownload()) {
        AbortSurveyAndDisconnect(ctx, state);
      } else {
        openwow::ui::glue::CGlueMgr_ResetStateToIdle();
      }
      break;
  }
}

}

void CancelGlueFlowNetworkOperations(GlueFlowContext& ctx,
                                     GlueFlowState& state) {
  CancelAllNetworkFutures(ctx, state);
}

void PumpGlueFlow(GlueFlowContext& ctx, GlueFlowState& state) {
  if (ctx.game_state == nullptr) {
    return;
  }

  auto& gs = *ctx.game_state;

  if (ctx.realm_session != nullptr) {
    (void)ctx.realm_session->ServiceHeartbeat(
        openwow::core::GameClock::GetTickCount32());

    openwow::net::wotlk::WorldPacket realm_split_packet;
    while (ctx.realm_session->TryReceiveRealmSplit(realm_split_packet)) {
      openwow::net::wotlk::GlueHandlerCallbacks callbacks;
      callbacks.fire_event = ctx.fire_glue_event;
      openwow::net::wotlk::HandleRealmSplit(realm_split_packet, callbacks);
    }
  }
  PublishRealmAddonsIfReady(state);

  if (state.phase == GlueFlowState::Phase::kIdle) {
    DispatchPendingAccountMessagesEvent(ctx, state);
  }

  if ((gs.wants_cancel_login || gs.wants_dismiss_dialog || gs.wants_login)
      && !state.status_dialog_open
      && gs.status_dialog_type != openwow::ui::glue::StatusDialogType::kNone) {
    state.status_dialog_open = true;
    state.last_status_text.clear();
  }

  const auto emit_status_update = [&](const std::string& text) {
    if (!state.status_dialog_open || text.empty()) return;
    UpdateStatusDialog(ctx, text);
  };

  if (state.phase != GlueFlowState::Phase::kWorldConnecting
      && state.world_connect_future.has_value()
      && FutureReady(*state.world_connect_future)) {
    (void)state.world_connect_future->get();
    state.world_connect_future.reset();
    ClearWorldConnectQueueProgress(state);
  }

  DrainPendingRealmAddonListUpdate(ctx, state);

  if (gs.wants_cancel_realm_list_query) {
    gs.wants_cancel_realm_list_query = false;
    if (state.phase == GlueFlowState::Phase::kRealmListPending) {
      CancelRealmListPendingOperation(ctx, state);
    }
  }

  if (gs.wants_realm_list_dialog_cancelled) {
    gs.wants_realm_list_dialog_cancelled = false;
    RequestLoginNetDisconnect(state);
  }

  if (gs.wants_cancel_auth_login) {
    gs.wants_cancel_auth_login = false;
    if (state.phase == GlueFlowState::Phase::kAuthInProgress) {
      CancelByState(ctx, state, gs);
    }
  }

  if (gs.wants_cancel_login) {
    gs.wants_cancel_login = false;
    CancelByState(ctx, state, gs);
  }

  if (openwow::ui::glue::CGlueMgr_GetStateValue()
      == static_cast<int>(openwow::ui::glue::GlueState::kPatchDownload)) {
    openwow::ui::glue::CGlueMgr_UpdatePatchDownload(gs, ctx.fire_glue_event,
                                                    ctx.dt);
  }
  if (openwow::ui::glue::CGlueMgr_GetStateValue()
      == static_cast<int>(openwow::ui::glue::GlueState::kScanDll)) {
    openwow::ui::glue::CGlueMgr_UpdateScanDll(gs, ctx.fire_glue_event);
  }

  if (gs.wants_dismiss_dialog) {
    gs.wants_dismiss_dialog = false;
    CloseStatusDialog(ctx, state);
    state.status_dialog_open = false;
    state.last_status_text.clear();
    if (state.phase == GlueFlowState::Phase::kError) {
      RecoverFromError(ctx, state);
    }
  }

  const auto start_auth_attempt = [&](std::string username, std::string password) {
    if (ctx.login_screen == nullptr) return;

    if (username.empty()) {
      SetDialogType(ctx, state, openwow::ui::glue::StatusDialogType::kOkay);
      if (ctx.fire_glue_event) {
        ctx.fire_glue_event("OPEN_STATUS_DIALOG",
                            {MakeLuaString("OKAY"), MakeLuaString(ResolveGlueStringOrFallback(ctx, "LOGIN_ENTER_NAME", "Enter account name"))});
      }
      return;
    }
    if (password.empty()) {
      SetDialogType(ctx, state, openwow::ui::glue::StatusDialogType::kOkay);
      if (ctx.fire_glue_event) {
        ctx.fire_glue_event("OPEN_STATUS_DIALOG",
                            {MakeLuaString("OKAY"), MakeLuaString(ResolveGlueStringOrFallback(ctx, "LOGIN_ENTER_PASSWORD", "Enter password"))});
      }
      return;
    }

    state.cancel_requested.store(false);
    state.auth_future.reset();
    state.realm_future.reset();
    DisconnectAuthProtocol(state);
    state.auth_protocol =
        std::make_shared<openwow::net::wotlk::AuthProtocol>();
    openwow::net::ClientServices::Instance().SetLoginFileTransferResponseSender(
        [protocol = std::weak_ptr<openwow::net::wotlk::AuthProtocol>(
             state.auth_protocol)](const bool transfer_needed,
                                   const std::uint64_t resume_offset) {
          const auto active_protocol = protocol.lock();
          return active_protocol != nullptr &&
                 active_protocol->SendFileTransferResponse(transfer_needed,
                                                           resume_offset);
        });
    state.session_token.clear();
    state.login_state_dialog_handler.Reset();
    state.matrix_challenge_announced = false;
    state.matrix_submission_observed = false;
    state.pin_challenge_announced = false;
    state.pin_submission_observed = false;
    state.token_challenge_announced = false;
    state.token_submission_observed = false;
    state.addon_discovery_publish_pending = false;
    if (ctx.addon_discovery_vfs != nullptr &&
        (!state.addon_discovery_future.valid() ||
         FutureReady(state.addon_discovery_future))) {
      auto discovery_vfs = *ctx.addon_discovery_vfs;
      state.addon_discovery_future = std::async(
          std::launch::async,
          [discovery_vfs = std::move(discovery_vfs)]() mutable {
            return DiscoverRealmAddons(std::move(discovery_vfs));
          }).share();
      state.addon_discovery_published = false;
    }
    SetFlowPhase(state, GlueFlowState::Phase::kAuthInProgress);

    ctx.login_screen->SetUsername(username);

    ctx.login_screen->SecureClearPassword();

    if (gs.status_dialog_type == openwow::ui::glue::StatusDialogType::kCancel) {
      SetDialogType(ctx, state, openwow::ui::glue::StatusDialogType::kCancel);
    } else {
      OpenStatusDialogCancel(ctx, state);
    }
    state.status_dialog_open = true;
    state.last_status_text.clear();
    emit_status_update(ResolveGlueStringOrFallback(ctx, "CSTATUS_CONNECTING", "Connecting..."));

    const std::string host = ctx.auth_host;
    const std::uint16_t port = ctx.auth_port;
    const auto auth = state.auth_protocol;
    const auto should_cancel = [&]() { return state.cancel_requested.load(); };
    state.auth_future.emplace(std::async(std::launch::async, [auth, host, port, username, password, should_cancel]() mutable {
      auto result = auth->Login(host, port, username, password,
                                5000, should_cancel);
      openwow::ui::glue::GlueGameState::SecureClearString(password);
      return result;
    }));
    openwow::ui::glue::GlueGameState::SecureClearString(password);
  };

  if (gs.wants_login) {
    gs.wants_login = false;

    std::string username;
    std::string password;
    if (gs.login_request.pending) {
      auto request = gs.ConsumeLoginRequest();
      username = std::move(request.username);
      password = request.password;
      openwow::ui::glue::GlueGameState::SecureClearString(request.password);
    } else {
      username = ReadFirstNonEmptyText(ctx.glue_widgets,
                                       {"AccountLoginAccountEdit", "AccountNameEditBox", "AccountEditBox"});
      password = ReadFirstNonEmptyText(ctx.glue_widgets,
                                       {"AccountLoginPasswordEdit", "AccountPasswordEditBox", "PasswordEditBox"});
    }

    if (state.phase == GlueFlowState::Phase::kIdle) {
      start_auth_attempt(std::move(username), std::move(password));
    }
  }

  if (gs.wants_character_list_refresh) {
    gs.wants_character_list_refresh = false;
    StartCharacterListRefresh(ctx, state);
  }

  if (state.phase == GlueFlowState::Phase::kAuthInProgress && state.auth_future.has_value()) {
    auto& pin_bridge = openwow::net::LoginPinChallengeBridge::Get();
    auto& matrix_bridge = openwow::net::LoginMatrixChallengeBridge::Get();
    auto& token_bridge = openwow::net::LoginTokenChallengeBridge::Get();
    if (!state.pin_challenge_announced) {
      if (const auto challenge = pin_bridge.challenge_info(); challenge.has_value()) {

        if (state.status_dialog_open) {
          CloseStatusDialog(ctx, state);
          state.status_dialog_open = false;
        }
        if (ctx.fire_glue_event) {
          std::vector<GlueLuaValue> keypad_values;
          keypad_values.reserve(challenge->keypad_shuffle.size());
          for (const auto value : challenge->keypad_shuffle) {
            keypad_values.push_back(MakeLuaNumber(value));
          }
          ctx.fire_glue_event("PLAYER_ENTER_PIN", std::move(keypad_values));
        }
        state.pin_challenge_announced = true;
      }
    } else if (!state.pin_submission_observed && pin_bridge.has_submission()) {
      SetDialogType(ctx, state, openwow::ui::glue::StatusDialogType::kCancel);
      state.status_dialog_open = true;
      state.pin_submission_observed = true;
    }

    if ((!pin_bridge.is_active() || state.pin_submission_observed)
        && !state.matrix_challenge_announced) {
      if (const auto challenge = matrix_bridge.challenge_info(); challenge.has_value()) {

        if (state.status_dialog_open) {
          CloseStatusDialog(ctx, state);
          state.status_dialog_open = false;
        }
        if (ctx.fire_glue_event) {
          ctx.fire_glue_event(
              "PLAYER_ENTER_MATRIX",
              {MakeLuaNumber(challenge->columns),
               MakeLuaNumber(challenge->rows),
               MakeLuaNumber(challenge->minimum_digits),
               MakeLuaNumber(challenge->maximum_digits),
               MakeLuaBool(challenge->flip_coordinates),
               MakeLuaNumber(challenge->entry_count)});
        }
        state.matrix_challenge_announced = true;
      }
    } else if (state.matrix_challenge_announced
               && !state.matrix_submission_observed
               && matrix_bridge.has_submission()) {

      SetDialogType(ctx, state, openwow::ui::glue::StatusDialogType::kCancel);
      state.status_dialog_open = true;
      state.matrix_submission_observed = true;
    }

    if ((!matrix_bridge.is_active() || state.matrix_submission_observed) &&
        !state.token_challenge_announced && token_bridge.is_active()) {
      if (state.status_dialog_open) {
        CloseStatusDialog(ctx, state);
        state.status_dialog_open = false;
      }
      if (ctx.fire_glue_event) {
        ctx.fire_glue_event("PLAYER_ENTER_TOKEN", {});
      }
      state.token_challenge_announced = true;
    } else if (state.token_challenge_announced &&
               !state.token_submission_observed &&
               token_bridge.has_submission()) {
      SetDialogType(ctx, state, openwow::ui::glue::StatusDialogType::kCancel);
      state.status_dialog_open = true;
      state.token_submission_observed = true;
    }

    if (state.status_dialog_open) {
      emit_status_update(ResolveGlueStringOrFallback(ctx, "CSTATUS_AUTHENTICATING", "Authenticating..."));
    }

    if (FutureReady(*state.auth_future)) {
      auto result = state.auth_future->get();
      state.auth_future.reset();

      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                                "GlueFlow: auth result: " + result.message);
      if (ctx.set_login_status) {
        ctx.set_login_status(result.status != openwow::net::wotlk::AuthStatus::kSuccess &&
                                 result.status != openwow::net::wotlk::AuthStatus::kPatchRequired &&
                                 result.status !=
                                     openwow::net::wotlk::AuthStatus::kPatchTransferComplete,
                             LoginErrorText(ctx, result));
      }

      if (state.cancel_requested.load()) {
        if (state.status_dialog_open) {
          CloseStatusDialog(ctx, state);
          state.status_dialog_open = false;
        }
        state.last_status_text.clear();
        state.cancel_requested.store(false);
        DisconnectAuthProtocol(state);
        openwow::net::ClientServices::Instance().Disconnect();
        SetFlowPhase(state, GlueFlowState::Phase::kIdle);
        return;
      }

      if (result.status == openwow::net::wotlk::AuthStatus::kPatchRequired) {
        if (!result.file_transfer.has_value()) {
          DisconnectAuthProtocol(state);
          openwow::net::ClientServices::Instance().Disconnect();
          ShowStatusAndReset(ctx, state, "Invalid patch transfer metadata.");
          SetFlowPhase(state, GlueFlowState::Phase::kIdle);
          return;
        }

        openwow::net::ClientServices::Instance().SetPendingPatchDownloadInfo({
            .filename = result.file_transfer->filename,
            .digest = result.file_transfer->digest,
            .expected_size = result.file_transfer->expected_size,
        });
        openwow::ui::glue::CGlueMgr_StartPatchDownload(gs, ctx.fire_glue_event);
        state.status_dialog_open = false;
        state.last_status_text.clear();

        const auto snapshot =
            openwow::net::LoginPatchDownloadBridge::Get().SnapshotActiveDownload();
        if (snapshot.has_active_download &&
            snapshot.state == openwow::net::LoginPatchDownloadState::kInProgress) {
          const auto auth = state.auth_protocol;
          state.auth_future.emplace(std::async(std::launch::async, [auth, should_cancel = [&state] {
                                                 return state.cancel_requested.load();
                                               }] {
            return auth->ReceiveFileTransfer(
                [](const std::span<const std::uint8_t> bytes) {
                  return openwow::net::DispatchActiveLoginInlineDownloadChunk(bytes);
                },
                0, should_cancel);
          }));
        } else {
          DisconnectAuthProtocol(state);
          openwow::net::ClientServices::Instance().Disconnect();
          SetFlowPhase(state, GlueFlowState::Phase::kIdle);
        }
        return;
      }

      if (result.status == openwow::net::wotlk::AuthStatus::kPatchTransferComplete) {
        DisconnectAuthProtocol(state);
        openwow::net::ClientServices::Instance().Disconnect();
        SetFlowPhase(state, GlueFlowState::Phase::kIdle);
        return;
      }

      if (openwow::ui::glue::CGlueMgr_GetStateValue() ==
          static_cast<int>(openwow::ui::glue::GlueState::kPatchDownload)) {
        const auto snapshot =
            openwow::net::LoginPatchDownloadBridge::Get().SnapshotActiveDownload();
        if (snapshot.has_active_download &&
            snapshot.state == openwow::net::LoginPatchDownloadState::kInProgress) {
          openwow::net::LoginPatchDownloadBridge::Get().AbortActiveDownload();
        }
        DisconnectAuthProtocol(state);
        openwow::net::ClientServices::Instance().Disconnect();
        SetFlowPhase(state, GlueFlowState::Phase::kIdle);
        return;
      }

      if (result.status != openwow::net::wotlk::AuthStatus::kSuccess) {
        DisconnectAuthProtocol(state);
        if (HandleBattleNetUnknownAccountDisconnect(ctx, state, result)) {
          return;
        }

        if (ctx.show_error != nullptr)
          *ctx.show_error = true;
        if (!OpenAuthRejectedDialog(ctx, state, result)) {
          ShowStatusAndReset(ctx, state, LoginErrorText(ctx, result));
        }
        openwow::net::ClientServices::Instance().Disconnect();
        SetFlowPhase(state, GlueFlowState::Phase::kIdle);
        return;
      }

      state.session_token = result.session_token;

      openwow::net::ClientServices::Instance().SetLoginConnectionAccountFlags(result.account_flags);
      state.pending_account_messages_available =
          state.pending_account_messages_available || result.account_messages_available;

      state.addon_discovery_publish_pending =
          state.addon_discovery_future.valid();
      PublishRealmAddonsIfReady(state);
      gs.session_key_raw = result.session_key_raw;
      gs.session_key_valid = true;
      if (ctx.auth_session_token != nullptr) {
        *ctx.auth_session_token = result.session_token;
      }

      gs.wants_realm_list_refresh = false;
      gs.request_realm_list_show_dialog = false;
      gs.request_realm_list_dialog_opened = false;
      CompleteRealmListResult(ctx, state, std::move(result.realms), true);
      return;
    }
  }

  if (state.phase == GlueFlowState::Phase::kRealmListPending && state.realm_future.has_value()) {
    if (FutureReady(*state.realm_future)) {
      auto realms_result = state.realm_future->get();
      state.realm_future.reset();

      if (state.cancel_requested.load()) {
        if (state.status_dialog_open) { CloseStatusDialog(ctx, state); state.status_dialog_open = false; }
        state.last_status_text.clear();
        state.cancel_requested.store(false);
        DisconnectAuthProtocol(state);
        SetFlowPhase(state, GlueFlowState::Phase::kIdle);
        return;
      }

      if (!realms_result.ok) {
        realms_result.realms.clear();
        DisconnectAuthProtocol(state);
      }
      CompleteRealmListResult(ctx, state, std::move(realms_result.realms),
                              state.realm_fetch_transitions);
    }
  }

  if (gs.wants_realm_list_refresh) {
    gs.wants_realm_list_refresh = false;
    const bool show_dialog = gs.request_realm_list_show_dialog;
    const bool dialog_already_open = gs.request_realm_list_dialog_opened;
    gs.request_realm_list_show_dialog = false;
    gs.request_realm_list_dialog_opened = false;
    (void)BeginRealmListFetch(ctx, state, show_dialog, dialog_already_open,
                              false);
    return;
  }

  if (gs.wants_world_connect) {
    gs.wants_world_connect = false;
    if (state.phase != GlueFlowState::Phase::kIdle
        || ctx.realm_session == nullptr
        || state.world_connect_future.has_value()) {
      return;
    }

    const auto resolved_realm_index = ResolveWorldConnectRealmIndex(gs);
    if (!resolved_realm_index.has_value()) {
      openwow::net::ClientServices::Instance().ClearSelectedRealmScriptMetadata();
      ShowStatusAndReset(
          ctx,
          state,
          ResolveResultStringOrFallback(
              ctx,
              static_cast<std::int32_t>(
                  openwow::net::ConnectionResponse::kRealmListRealmNotFound)));
      if (ctx.show_error != nullptr) {
        *ctx.show_error = true;
      }
      SetFlowPhase(state, GlueFlowState::Phase::kIdle);
      return;
    }

    state.cancel_requested.store(false);
    SetFlowPhase(state, GlueFlowState::Phase::kWorldConnecting);

    const bool reuse_existing_connect_dialog =
        gs.status_dialog_type == openwow::ui::glue::StatusDialogType::kCancel;
    if (!reuse_existing_connect_dialog) {
      OpenStatusDialogCancel(ctx, state);
    } else {
      SetDialogType(ctx, state, openwow::ui::glue::StatusDialogType::kCancel);
    }
    state.status_dialog_open = true;
    state.last_status_text.clear();
    if (!reuse_existing_connect_dialog) {
      emit_status_update(
          ResolveGlueStringOrFallback(ctx, "CSTATUS_CONNECTING", "Connecting..."));
    }

    const auto realm = gs.realms[*resolved_realm_index];
    auto &client_services = openwow::net::ClientServices::Instance();
    auto &cvars = openwow::ui::game::CVarSystem::Instance();
    if (!cvars.Exists("realmName")) {
      cvars.RegisterCVar("realmName", "");
    }
    (void)cvars.SetCVar("realmName", realm.name, true);
    client_services.SetSelectedRealmAddress(realm.address);
    client_services.SetSelectedRealmScriptMetadata({
        .category = realm.timezone,
        .realm_type = static_cast<std::uint32_t>(realm.type),
        .is_pvp_flag = realm.is_pvp_flag,
    });

    client_services.SetSelectedRealmAuthSessionSeedWords(
        realm.tail_words[0], realm.tail_words[1], realm.tail_words[2]);
    const auto selected_realm_auth_session_seed_words =
        client_services.GetSelectedRealmAuthSessionSeedWords();
    auto world_realm = realm;
    world_realm.address = client_services.GetSelectedRealmAddress();
    const std::string token = state.session_token;
    const auto session_key = gs.session_key_raw;
    const bool has_key = gs.session_key_valid;
    auto* session = ctx.realm_session;
    session->SetAuthSessionSeedWords(
        selected_realm_auth_session_seed_words[0],
        selected_realm_auth_session_seed_words[1],
        selected_realm_auth_session_seed_words[2]);
    auto queue_progress =
        std::make_shared<openwow::game::QueuePositionTracker>();
    queue_progress->Reset(realm.name);
    state.world_connect_queue_progress = queue_progress;
    state.world_connect_used_fcm_dialog = false;
    ArmRealmAddonListUpdateCallback(ctx, state);
    std::string account_name = client_services.GetAccountName();
    if (account_name.empty()) {
      account_name = openwow::ui::game::CVarSystem::Instance().GetCVar(
          std::string(kSavedAccountNameCVar));
    }

    session->SetSessionToken(token);
    if (has_key) {
      session->SetSessionKey(session_key);
      session->SetAccountName(account_name);
    }
    const auto addon_discovery = state.addon_discovery_future;
    const auto should_cancel = [&state] {
      return state.cancel_requested.load(std::memory_order_acquire);
    };

    state.world_connect_future.emplace(std::async(std::launch::async,
        [session, world_realm = std::move(world_realm), queue_progress, addon_discovery,
         should_cancel]() -> openwow::net::wotlk::WorldAuthResult {

      PrepareRealmAddonHandshake(addon_discovery);
      if (!session->Connect(world_realm, 5000)) {
        return {.status = openwow::net::wotlk::WorldAuthStatus::kNetworkError,
                .message = "Failed to connect to world server."};
      }

      return session->Authenticate(
          5000,
          [queue_progress](const openwow::net::wotlk::WorldAuthResult& progress) {
            if (progress.has_account_info) {
              queue_progress->RecordAccountInfo(
                  progress.billing_time, progress.billing_flags,
                  progress.billing_rested, progress.expansion_level);
            }
            queue_progress->RecordQueuePosition(
                progress.queue_position,
                progress.free_character_migration != 0,
                openwow::core::GameClock::GetTickCount32());
          },
          should_cancel);
    }));
  }

  if (state.phase == GlueFlowState::Phase::kWorldConnecting) {
    bool queue_update_emitted = false;
    if (state.world_connect_queue_progress != nullptr) {
      const auto progress = state.world_connect_queue_progress->Snapshot(
          openwow::core::GameClock::GetTickCount32());
      if (progress.has_account_info) {
        auto& client_services = openwow::net::ClientServices::Instance();
        client_services.SetWorldAccountBilling(
            progress.billing_time, progress.billing_flags,
            progress.billing_rested);
        client_services.SetExpansionLevel(progress.expansion_level);
      }
      if (progress.active
          && progress.free_character_migration
          && !state.world_connect_used_fcm_dialog
          && ctx.fire_glue_event) {
        ctx.fire_glue_event("OPEN_STATUS_DIALOG",
                            {MakeLuaString("QUEUED_WITH_FCM")});
        state.world_connect_used_fcm_dialog = true;
      }

      const std::string queue_message =
          progress.active ? FormatWorldQueueDialogText(ctx, progress)
                          : std::string();
      if (progress.active && !queue_message.empty()) {
        UpdateStatusDialog(
            ctx,
            queue_message,
            ResolveGlueStringOrFallback(ctx, "CHANGE_REALM", "CHANGE_REALM"));
        queue_update_emitted = true;
      }
    }

    if (!queue_update_emitted) {
      emit_status_update(
          ResolveGlueStringOrFallback(ctx, "CSTATUS_NEGOTIATING_SECURITY",
                                      "Negotiating security..."));
    }

    if (state.world_connect_future.has_value()
        && FutureReady(*state.world_connect_future)) {
      const auto result = state.world_connect_future->get();
      state.world_connect_future.reset();
      ClearWorldConnectQueueProgress(state);

      auto& client_services = openwow::net::ClientServices::Instance();
      if (result.has_account_info) {
        client_services.SetWorldAccountBilling(
            result.billing_time, result.billing_flags, result.billing_rested);
        client_services.SetExpansionLevel(result.expansion_level);
      }

      if (state.cancel_requested.load()) {
        if (state.status_dialog_open) { CloseStatusDialog(ctx, state); state.status_dialog_open = false; }
        state.last_status_text.clear();
        state.cancel_requested.store(false);
        SetFlowPhase(state, GlueFlowState::Phase::kIdle);
        return;
      }

      if (result.status != openwow::net::wotlk::WorldAuthStatus::kSuccess) {
        gs.connected = false;
        const std::string error_text =
            result.status == openwow::net::wotlk::WorldAuthStatus::kAuthFailed
                ? ResolveResultStringOrFallback(
                      ctx, result.result_code, "AUTH_FAILED")
                : (result.message.empty()
                       ? ResolveGlueStringOrFallback(
                             ctx, "RESPONSE_FAILED_TO_CONNECT",
                             "Failed to connect to world server.")
                       : result.message);
        ShowStatusAndReset(
            ctx, state, error_text);
        if (ctx.show_error != nullptr) *ctx.show_error = true;
        SetFlowPhase(state, GlueFlowState::Phase::kIdle);
        return;
      }

      const std::uint8_t account_expansion_level =
          client_services.GetExpansionLevel();
      UpdateAccountTypeCVar(
          account_expansion_level,
          client_services.IsLoginConnectionTrialAccount());

      const std::uint8_t client_expansion_level =
          openwow::core::GetExpansionLevel();
      if (account_expansion_level > client_expansion_level) {
        if (ctx.fire_glue_event) {
          ctx.fire_glue_event("CLIENT_ACCOUNT_MISMATCH",
                              {MakeLuaNumber(account_expansion_level),
                               MakeLuaNumber(client_expansion_level)});
        }
        if (ctx.realm_session != nullptr) {
          ctx.realm_session->Disconnect();
        }
        gs.connected = false;
        if (ctx.show_error != nullptr) {
          *ctx.show_error = true;
        }

        SetDialogType(ctx, state, openwow::ui::glue::StatusDialogType::kNone);
        state.status_dialog_open = false;
        state.last_status_text.clear();
        SetFlowPhase(state, GlueFlowState::Phase::kIdle);
        return;
      }

      gs.connected = true;
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo, "GlueFlow: world auth succeeded");
      if (HandleConvertedTrialTransition(ctx, state)) {
        return;
      }

      ResetCharacterListForRefresh(ctx, gs);
      gs.selected_character_index = 0;
      SetFlowPhase(state, GlueFlowState::Phase::kIdle);
      if (ctx.fire_glue_event) {
        ctx.fire_glue_event("UPDATE_SELECTED_CHARACTER", {MakeLuaNumber(1.0)});
        ctx.fire_glue_event("CHARACTER_LIST_UPDATE", {});
        openwow::ui::glue::Login_SetScreen(ctx.fire_glue_event, "charselect");
      }
      DispatchPendingAccountMessagesEvent(ctx, state);
      return;
    }
  }

  if (state.phase == GlueFlowState::Phase::kCharListPending && state.charlist_future.has_value()) {
    if (!FutureReady(*state.charlist_future)) {
      emit_status_update(ResolveGlueStringOrFallback(
          ctx, "CHAR_LIST_RETRIEVING", "CHAR_LIST_RETRIEVING"));
    } else {
      auto result = state.charlist_future->get();
      state.charlist_future.reset();

      if (state.cancel_requested.load()) {
        if (state.status_dialog_open) { CloseStatusDialog(ctx, state); state.status_dialog_open = false; }
        state.last_status_text.clear();
        state.cancel_requested.store(false);
        SetFlowPhase(state, GlueFlowState::Phase::kIdle);
        return;
      }

      if (!result.ok) {
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                           "GlueFlow: character list fetch failed");
        const std::string error_text = ResolveGlueStringOrFallback(
            ctx, "CHAR_LIST_FAILED", "CHAR_LIST_FAILED");
        emit_status_update(error_text);
        SetFlowPhase(state, GlueFlowState::Phase::kIdle);
        OpenCharacterServiceErrorDialog(ctx, state, error_text);
        if (ctx.show_error != nullptr) {
          *ctx.show_error = true;
        }
        return;
      }

      emit_status_update(ResolveGlueStringOrFallback(
          ctx, "CHAR_LIST_RETRIEVED", "CHAR_LIST_RETRIEVED"));
      if (ctx.show_error != nullptr) {
        *ctx.show_error = false;
      }
      SetFlowPhase(state, GlueFlowState::Phase::kIdle);
      if (state.status_dialog_open) { CloseStatusDialog(ctx, state); state.status_dialog_open = false; }
      state.last_status_text.clear();

      ApplyCharacterListResult(ctx, gs, result);
      if (ctx.fire_glue_event) {
        ctx.fire_glue_event(
            "UPDATE_SELECTED_CHARACTER",
            {MakeLuaNumber(static_cast<double>(gs.selected_character_index + 1))});
        ctx.fire_glue_event("CHARACTER_LIST_UPDATE", {});
      }

      DispatchPendingAccountMessagesEvent(ctx, state);
    }
  }

  if (gs.wants_create_character) {
    gs.wants_create_character = false;
    if (state.phase != GlueFlowState::Phase::kIdle || ctx.realm_session == nullptr) return;
    if (!gs.char_create_request.pending) return;

    const auto req = gs.char_create_request;
    gs.char_create_request.pending = false;

    state.cancel_requested.store(false);
    SetFlowPhase(state, GlueFlowState::Phase::kCharCreating);

    auto* session = ctx.realm_session;
    state.char_create_future.emplace(std::async(std::launch::async,
        [session, req]() {
      return session->CreateCharacter(
          req.name, req.race, req.cls, req.gender,
          req.skin, req.face, req.hair_style, req.hair_color, req.facial_hair,
          5000);
    }));
  }

  if (state.phase == GlueFlowState::Phase::kCharCreating && state.char_create_future.has_value()) {
    if (FutureReady(*state.char_create_future)) {
      const auto result = state.char_create_future->get();
      state.char_create_future.reset();

      if (result.ok) {
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo, "GlueFlow: character created");

        SetFlowPhase(state, GlueFlowState::Phase::kIdle);
        CloseStatusDialog(ctx, state);
        state.status_dialog_open = false;
        state.last_status_text.clear();
        if (ctx.fire_glue_event) {
          ctx.fire_glue_event("SELECT_LAST_CHARACTER", {});
          openwow::ui::glue::Login_SetScreen(ctx.fire_glue_event, "charselect");
        }

      } else {
        const auto err_str = ResolveResultStringOrFallback(
            ctx, result.result_code, "CHAR_CREATE_FAILED");
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                           "GlueFlow: character create failed (code=" + std::to_string(result.result_code)
                               + "): " + err_str);
        if (ctx.fire_glue_event) {
          ctx.fire_glue_event("CHAR_CREATE_FAILED", {MakeLuaString(err_str)});
        }

        ShowStatusAndReset(ctx, state, err_str);
        if (ctx.show_error != nullptr) *ctx.show_error = true;
        SetFlowPhase(state, GlueFlowState::Phase::kIdle);
      }
    }
  }

  if (gs.wants_delete_character) {
    gs.wants_delete_character = false;
    const auto request = gs.char_delete_request;
    gs.char_delete_request.pending = false;
    if (state.phase != GlueFlowState::Phase::kIdle || ctx.realm_session == nullptr) return;
    if (!request.pending || request.guid == 0) return;

    state.cancel_requested.store(false);
    SetFlowPhase(state, GlueFlowState::Phase::kCharDeleting);

    auto* session = ctx.realm_session;
    state.char_delete_future.emplace(std::async(
        std::launch::async, [session, guid = request.guid]() {
          return session->DeleteCharacter(
              guid, kCharacterDeleteTransportTimeoutMs);
        }));
  }

  if (state.phase == GlueFlowState::Phase::kCharDeleting && state.char_delete_future.has_value()) {
    if (FutureReady(*state.char_delete_future)) {
      const auto result = state.char_delete_future->get();
      state.char_delete_future.reset();

      if (result.ok) {
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo, "GlueFlow: character deleted");

        if (ctx.fire_glue_event) {
          ctx.fire_glue_event("CHAR_DELETE_SUCCESS", {});
          ctx.fire_glue_event("SELECT_FIRST_CHARACTER", {});
        }
        SetFlowPhase(state, GlueFlowState::Phase::kIdle);
        openwow::ui::glue::CGlueMgr_RequestCharacterList(gs);
        gs.wants_character_list_refresh = false;
        StartCharacterListRefresh(ctx, state);
      } else if (result.server_outcome_unknown) {

        openwow::diagnostics::Log(
            openwow::diagnostics::LogLevel::kWarn,
            "GlueFlow: character delete response timed out; reconciling with CHAR_ENUM");
        SetFlowPhase(state, GlueFlowState::Phase::kIdle);
        openwow::ui::glue::CGlueMgr_RequestCharacterList(gs);
        gs.wants_character_list_refresh = false;
        StartCharacterListRefresh(ctx, state);
      } else {
        const auto err_str = ResolveResultStringOrFallback(
            ctx, result.result_code, "CHAR_DELETE_FAILED");
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                           "GlueFlow: character delete failed (code=" + std::to_string(result.result_code)
                               + "): " + err_str);
        if (ctx.fire_glue_event) {
          ctx.fire_glue_event("CHAR_DELETE_FAILED", {MakeLuaString(err_str)});
        }

        ShowStatusAndReset(ctx, state, err_str);
        if (ctx.show_error != nullptr) *ctx.show_error = true;
        SetFlowPhase(state, GlueFlowState::Phase::kIdle);
      }
    }
  }

  if (gs.wants_rename_character) {
    gs.wants_rename_character = false;
    if (state.phase != GlueFlowState::Phase::kIdle || ctx.realm_session == nullptr) return;
    if (!gs.char_rename_request.pending) return;

    const auto req = gs.char_rename_request;
    gs.char_rename_request.pending = false;

    state.cancel_requested.store(false);
    SetFlowPhase(state, GlueFlowState::Phase::kCharRenaming);

    auto* session = ctx.realm_session;
    state.char_rename_future.emplace(std::async(std::launch::async,
        [session, req]() {
      return session->RenameCharacter(req.guid, req.new_name, 5000);
    }));
  }

  if (state.phase == GlueFlowState::Phase::kCharRenaming && state.char_rename_future.has_value()) {
    if (FutureReady(*state.char_rename_future)) {
      const auto result = state.char_rename_future->get();
      state.char_rename_future.reset();

      CloseStatusDialog(ctx, state);
      state.status_dialog_open = false;
      state.last_status_text.clear();

      if (result.ok) {
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo, "GlueFlow: character renamed");
        const bool updated_character =
            ApplyCharacterRenameUpdate(ctx, result.guid, result.new_name);
        const bool selected_character_renamed =
            updated_character && IsSelectedCharacterGuid(gs, result.guid);
        if (ctx.fire_glue_event) {
          if (updated_character) {
            ctx.fire_glue_event("CHARACTER_LIST_UPDATE", {});
          }
        }
        if (ctx.show_error != nullptr) {
          *ctx.show_error = false;
        }
        SetFlowPhase(state, GlueFlowState::Phase::kIdle);
        if (selected_character_renamed) {
          gs.wants_enter_world = true;
        }
      } else {
        const std::string error_text =
            CharacterRenameFailureText(ctx, result.result_code);
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                           "GlueFlow: character rename failed (code=" +
                               std::to_string(result.result_code) + "): " + error_text);
        if (ctx.fire_glue_event) {
          ctx.fire_glue_event("FORCE_RENAME_CHARACTER", {MakeLuaString(error_text)});
        }
        if (ctx.show_error != nullptr) {
          *ctx.show_error = false;
        }
        SetFlowPhase(state, GlueFlowState::Phase::kIdle);
      }
    }
  }

  if (gs.wants_decline_character) {
    gs.wants_decline_character = false;
    if (state.phase != GlueFlowState::Phase::kIdle || ctx.realm_session == nullptr) return;
    if (!gs.char_decline_request.pending) return;

    const auto req = gs.char_decline_request;
    gs.char_decline_request.pending = false;

    state.cancel_requested.store(false);
    SetFlowPhase(state, GlueFlowState::Phase::kCharDeclining);
    state.status_dialog_open = true;
    state.last_status_text.clear();

    auto* session = ctx.realm_session;
    state.char_decline_future.emplace(std::async(std::launch::async,
        [session, req]() {
      return session->SetPlayerDeclinedNames(req.guid, req.base_name, req.forms, 5000);
    }));
  }

  if (state.phase == GlueFlowState::Phase::kCharDeclining
      && state.char_decline_future.has_value()) {
    if (FutureReady(*state.char_decline_future)) {
      const auto result = state.char_decline_future->get();
      state.char_decline_future.reset();

      if (state.status_dialog_open) {
        CloseStatusDialog(ctx, state);
        state.status_dialog_open = false;
      }
      state.last_status_text.clear();

      if (result.ok) {
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                           "GlueFlow: character declined names accepted");
        for (auto& character : gs.characters) {
          if (character.id != result.guid) {
            continue;
          }
          character.char_flags |= 0x02000000u;
          break;
        }
        const bool selected_character_declined =
            IsSelectedCharacterGuid(gs, result.guid);

        SetFlowPhase(state, GlueFlowState::Phase::kIdle);
        if (selected_character_declined) {
          gs.wants_enter_world = true;
        }
      } else {
        openwow::diagnostics::Log(
            openwow::diagnostics::LogLevel::kWarn,
            "GlueFlow: character decline failed (code="
                + std::to_string(result.result_code) + ")");
        if (ctx.fire_glue_event) {
          ctx.fire_glue_event("FORCE_DECLINE_CHARACTER",
                              {MakeLuaString("CHAR_DECLINE_FAILED")});
        }
        SetFlowPhase(state, GlueFlowState::Phase::kIdle);
      }
    }
  }

  if (gs.wants_customize_character) {
    gs.wants_customize_character = false;
    if (state.phase != GlueFlowState::Phase::kIdle || ctx.realm_session == nullptr) return;
    if (!gs.char_customize_request.pending) return;

    const auto req = gs.char_customize_request;
    gs.char_customize_request.pending = false;

    state.cancel_requested.store(false);
    SetFlowPhase(state, GlueFlowState::Phase::kCharCustomizing);

    auto* session = ctx.realm_session;
    state.char_customize_future.emplace(std::async(std::launch::async,
        [session, req]() {
      return session->CustomizeCharacter(
          req.guid, req.name, req.gender, req.skin,
          req.hair_style, req.hair_color, req.facial_hair, req.face, 5000);
    }));
  }

  if (state.phase == GlueFlowState::Phase::kCharCustomizing && state.char_customize_future.has_value()) {
    if (FutureReady(*state.char_customize_future)) {
      const auto result = state.char_customize_future->get();
      state.char_customize_future.reset();

      if (result.ok) {
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo, "GlueFlow: character customized");
        const bool updated_character = ApplyCharacterServiceUpdate(ctx, result);
        if (ctx.fire_glue_event && updated_character) {
          ctx.fire_glue_event("CHARACTER_LIST_UPDATE", {});
        }
        CloseStatusDialog(ctx, state);
        state.status_dialog_open = false;
        state.last_status_text.clear();
        if (ctx.fire_glue_event) {
          openwow::ui::glue::Login_SetScreen(ctx.fire_glue_event, "charselect");
        }
        if (ctx.show_error != nullptr) *ctx.show_error = false;
        SetFlowPhase(state, GlueFlowState::Phase::kIdle);
      } else {
        const auto err_str = CharCustomizeFailureString(ctx, result.result_code);
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                           "GlueFlow: customize failed (code=" + std::to_string(result.result_code)
                               + "): " + err_str);
        OpenCharacterServiceErrorDialog(ctx, state, err_str);
        if (ctx.show_error != nullptr) *ctx.show_error = true;
        SetFlowPhase(state, GlueFlowState::Phase::kIdle);
      }
    }
  }

  if (gs.wants_faction_change) {
    gs.wants_faction_change = false;
    if (state.phase != GlueFlowState::Phase::kIdle || ctx.realm_session == nullptr) return;
    if (!gs.char_faction_change_request.pending) return;

    const auto req = gs.char_faction_change_request;
    gs.char_faction_change_request.pending = false;

    state.cancel_requested.store(false);
    SetFlowPhase(state, GlueFlowState::Phase::kCharCustomizing);

    auto* session = ctx.realm_session;
    state.char_faction_change_future.emplace(std::async(std::launch::async,
        [session, req]() {
      return session->FactionChangeCharacter(
          req.guid, req.name, req.gender, req.skin,
          req.hair_style, req.hair_color, req.facial_hair, req.face, req.race, 5000);
    }));
  }

  if (state.phase == GlueFlowState::Phase::kCharCustomizing && state.char_faction_change_future.has_value()) {
    if (FutureReady(*state.char_faction_change_future)) {
      const auto result = state.char_faction_change_future->get();
      state.char_faction_change_future.reset();

      if (result.ok) {
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo, "GlueFlow: faction changed");
        const bool updated_character = ApplyCharacterServiceUpdate(ctx, result);
        if (ctx.fire_glue_event && updated_character) {
          ctx.fire_glue_event("CHARACTER_LIST_UPDATE", {});
        }
        CloseStatusDialog(ctx, state);
        state.status_dialog_open = false;
        state.last_status_text.clear();
        if (ctx.fire_glue_event) {
          openwow::ui::glue::Login_SetScreen(ctx.fire_glue_event, "charselect");
        }
        if (ctx.show_error != nullptr) *ctx.show_error = false;
        SetFlowPhase(state, GlueFlowState::Phase::kIdle);
      } else {
        const auto err_str = CharFactionChangeFailureString(ctx, result.result_code);
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                           "GlueFlow: faction change failed (code=" + std::to_string(result.result_code)
                               + "): " + err_str);
        OpenCharacterServiceErrorDialog(ctx, state, err_str);
        if (ctx.show_error != nullptr) *ctx.show_error = true;
        SetFlowPhase(state, GlueFlowState::Phase::kIdle);
      }
    }
  }

  if (gs.wants_race_change) {
    gs.wants_race_change = false;
    if (state.phase != GlueFlowState::Phase::kIdle || ctx.realm_session == nullptr) return;
    if (!gs.char_race_change_request.pending) return;

    const auto req = gs.char_race_change_request;
    gs.char_race_change_request.pending = false;

    state.cancel_requested.store(false);
    SetFlowPhase(state, GlueFlowState::Phase::kCharCustomizing);

    auto* session = ctx.realm_session;
    state.char_race_change_future.emplace(std::async(std::launch::async,
        [session, req]() {
      return session->RaceChangeCharacter(
          req.guid, req.name, req.gender, req.skin,
          req.hair_style, req.hair_color, req.facial_hair, req.face, req.race, 5000);
    }));
  }

  if (state.phase == GlueFlowState::Phase::kCharCustomizing && state.char_race_change_future.has_value()) {
    if (FutureReady(*state.char_race_change_future)) {
      const auto result = state.char_race_change_future->get();
      state.char_race_change_future.reset();

      if (result.ok) {
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo, "GlueFlow: race changed");
        const bool updated_character = ApplyCharacterServiceUpdate(ctx, result);
        if (ctx.fire_glue_event && updated_character) {
          ctx.fire_glue_event("CHARACTER_LIST_UPDATE", {});
        }
        CloseStatusDialog(ctx, state);
        state.status_dialog_open = false;
        state.last_status_text.clear();
        if (ctx.fire_glue_event) {
          openwow::ui::glue::Login_SetScreen(ctx.fire_glue_event, "charselect");
        }
        if (ctx.show_error != nullptr) *ctx.show_error = false;
        SetFlowPhase(state, GlueFlowState::Phase::kIdle);
      } else {
        const auto err_str = CharFactionChangeFailureString(ctx, result.result_code);
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                           "GlueFlow: race change failed (code=" + std::to_string(result.result_code)
                               + "): " + err_str);
        OpenCharacterServiceErrorDialog(ctx, state, err_str);
        if (ctx.show_error != nullptr) *ctx.show_error = true;
        SetFlowPhase(state, GlueFlowState::Phase::kIdle);
      }
    }
  }

  if (gs.wants_enter_world) {
    gs.wants_enter_world = false;
    if (state.phase != GlueFlowState::Phase::kIdle || ctx.realm_session == nullptr) return;

    const int idx = gs.selected_character_index;
    if (idx < 0 || idx >= static_cast<int>(gs.characters.size())) {
      if (ctx.show_error != nullptr) *ctx.show_error = true;
      return;
    }

    const auto& character = gs.characters[static_cast<std::size_t>(idx)];
    if ((character.char_flags & openwow::net::CharFlag::LockedForTransfer) != 0) {
      OpenCharacterServiceErrorDialog(
          ctx, state, ResolveResultStringOrFallback(ctx, 84));
      if (ctx.show_error != nullptr) *ctx.show_error = true;
      return;
    }
    if ((character.char_flags & openwow::net::CharFlag::LockedByBilling) != 0) {
      OpenCharacterServiceErrorDialog(
          ctx, state, ResolveResultStringOrFallback(ctx, 85));
      if (ctx.show_error != nullptr) *ctx.show_error = true;
      return;
    }
    if ((character.char_flags & openwow::net::CharFlag::Rename) != 0) {
      if (ctx.fire_glue_event) {
        ctx.fire_glue_event("FORCE_RENAME_CHARACTER",
                            {MakeLuaString("CHAR_RENAME_DESCRIPTION")});
      }
      return;
    }
    if (RequiresDeclinedNamesPrompt(character)) {
      if (ctx.fire_glue_event) {
        ctx.fire_glue_event("FORCE_DECLINE_CHARACTER", {});
      }
      return;
    }

    const std::uint8_t expansion_level =
        openwow::net::ClientServices::Instance().GetExpansionLevel();
    if (!openwow::ui::glue::IsRaceEnabledForExpansion(character.race_id,
                                                      expansion_level)
        || !openwow::ui::glue::IsClassEnabledForExpansion(character.class_id,
                                                          expansion_level)) {
      OpenCharacterServiceErrorDialog(
          ctx, state, ResolveResultStringOrFallback(ctx, 82));
      if (ctx.show_error != nullptr) *ctx.show_error = true;
      return;
    }

    const std::uint64_t guid = character.id;
    openwow::ui::game::CVarSystem::Instance().SetCVar("lastCharacterIndex",
                                                      std::to_string(idx), true);
    PrepareEnterWorldLoadingState(character);
    state.cancel_requested.store(false);
    SetFlowPhase(state, GlueFlowState::Phase::kEnteringWorld);

    OpenStatusDialogCancel(ctx, state);
    state.status_dialog_open = true;
    state.last_status_text.clear();
    emit_status_update(ResolveGlueStringOrFallback(ctx, "CSTATUS_ENTERING_WORLD", "Entering the World of Warcraft..."));

    auto* session = ctx.realm_session;
    state.world_enter_future.emplace(std::async(std::launch::async, [session, guid]() {
      return session->EnterWorld(guid, 10000);
    }));

    float enter_world_x = character.x;
    float enter_world_y = character.y;
    float enter_world_z = character.z;
    if (character.is_first_login && ctx.setup_char_login_camera) {
      float login_camera_position[3]{};
      if (ctx.setup_char_login_camera(character.class_id, character.race_id,
                                      login_camera_position)) {
        enter_world_x = login_camera_position[0];
        enter_world_y = login_camera_position[1];
        enter_world_z = login_camera_position[2];
      }
    }

    if (ctx.enter_world_init) {
      ctx.enter_world_init(character.map_id, enter_world_x, enter_world_y,
                           enter_world_z, character.race_id);
    }
  }

  if (state.phase == GlueFlowState::Phase::kEnteringWorld && state.world_enter_future.has_value()) {
    if (FutureReady(*state.world_enter_future)) {
      const auto result = state.world_enter_future->get();
      state.world_enter_future.reset();

      if (state.status_dialog_open) { CloseStatusDialog(ctx, state); state.status_dialog_open = false; }
      state.last_status_text.clear();

      if (result.status == openwow::net::wotlk::WorldEnterStatus::kSuccess) {
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                           "GlueFlow: entered world (map=" + std::to_string(result.map_id)
                               + " x=" + std::to_string(result.x)
                               + " y=" + std::to_string(result.y)
                               + " z=" + std::to_string(result.z) + ")");
        SetFlowPhase(state, GlueFlowState::Phase::kWorldEnter);
        if (ctx.after_enter_world) {
          ctx.after_enter_world(result.map_id, result.x, result.y, result.z,
                                result.orientation);
        }
      } else {
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                           "GlueFlow: world enter failed: " + result.message);
        if (ctx.abort_enter_world_init) {
          ctx.abort_enter_world_init();
        }
        if (ctx.show_error != nullptr) *ctx.show_error = true;
        const auto error_text = result.result_code != 0
            ? ResolveResultStringOrFallback(
                  ctx, result.result_code, "CHAR_LOGIN_FAILED")
            : (result.message.empty()
                   ? ResolveGlueStringOrFallback(
                         ctx, "CHARACTER_LOGIN_FAILED", "Character login failed.")
                   : result.message);
        ShowStatusAndReset(ctx, state, error_text);
        SetFlowPhase(state, GlueFlowState::Phase::kIdle);
      }
    }
  }

  if (state.phase == GlueFlowState::Phase::kDisconnecting) {
    state.disconnect_timer -= ctx.dt;
    if (state.disconnect_timer <= 0) {

      if (ctx.realm_session != nullptr) {
        ctx.realm_session->Disconnect();
      }
      gs.connected = false;
      gs.characters.clear();
      gs.selected_realm_index = -1;
      gs.selected_character_index = -1;
      gs.session_key_valid = false;
      state.disconnect_requested = false;
      SetFlowPhase(state, GlueFlowState::Phase::kIdle);
      if (ctx.fire_glue_event) ctx.fire_glue_event("DISCONNECTED_FROM_SERVER", {});
    }
  }

  if (state.phase == GlueFlowState::Phase::kError) {

  }

  if (state.disconnect_requested &&
      state.phase != GlueFlowState::Phase::kDisconnecting) {
    state.disconnect_requested = false;
    RequestDisconnect(ctx, state);
  }
}

void RequestDisconnect(GlueFlowContext& ctx, GlueFlowState& state) {

  CancelGlueFlowNetworkOperations(ctx, state);

  if (state.status_dialog_open) {
    CloseStatusDialog(ctx, state);
    state.status_dialog_open = false;
  }
  state.last_status_text.clear();
  state.disconnect_timer = 2.0f;
  SetFlowPhase(state, GlueFlowState::Phase::kDisconnecting);
}

void EnterErrorState(GlueFlowContext& ctx, GlueFlowState& state,
                     const std::string& message) {

  CancelGlueFlowNetworkOperations(ctx, state);

  state.error_message = message;
  SetFlowPhase(state, GlueFlowState::Phase::kError);

  if (state.status_dialog_open) {
    CloseStatusDialog(ctx, state);
    state.status_dialog_open = false;
  }
  state.last_status_text.clear();

  SetDialogType(ctx, state, openwow::ui::glue::StatusDialogType::kOkay);
  if (ctx.fire_glue_event) {
    ctx.fire_glue_event("OPEN_STATUS_DIALOG", {MakeLuaString("OKAY"), MakeLuaString(message)});
  }
  if (ctx.show_error != nullptr) *ctx.show_error = true;
}

void RecoverFromError(GlueFlowContext& ctx, GlueFlowState& state) {
  if (state.phase != GlueFlowState::Phase::kError) return;

  state.error_message.clear();
  state.error_display_timer = 0;
  state.cancel_requested.store(false);

  if (ctx.show_error != nullptr) *ctx.show_error = false;

  if (ctx.game_state != nullptr && ctx.game_state->connected) {
    RequestDisconnect(ctx, state);
  } else {
    SetFlowPhase(state, GlueFlowState::Phase::kIdle);
  }
}

}

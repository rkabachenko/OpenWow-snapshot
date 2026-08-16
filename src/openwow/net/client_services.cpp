
#include "openwow/net/client_services.h"

#include "openwow/core/client_init.h"
#include "openwow/core/client_misc.h"
#include "openwow/core/login_state_handler.h"
#include "openwow/core/storm_string.h"
#include "openwow/debug/diagnostics/debug_console.h"
#include "openwow/game/battlenet_login.h"
#include "openwow/game/localization.h"
#include "openwow/net/auth/auth_session.h"
#include "openwow/net/client_services_packet_sender.h"
#include "openwow/net/auth/logon_challenge_packet.h"
#include "openwow/network/protocol/wotlk/opcodes.h"
#include "openwow/net/wotlk/protocol/packet_sender.h"
#include "openwow/net/wotlk/realm_connection.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/foundation/text/ascii.h"

#include <array>
#include <cassert>
#include <cstring>
#include <utility>

namespace openwow::net {

static std::uint8_t s_expansion_byte = 2;

ClientServices &ClientServices::Instance() {
  static ClientServices instance;
  return instance;
}

namespace {

constexpr std::uint8_t kBillingPlanMask = 0x16;
constexpr std::uint8_t kBillingTimeExcludedFlag = 0x02;
constexpr std::uint8_t kBillingPaidTimeFlag = 0x08;
constexpr std::uint8_t kBillingPlanFourFlag = 0x10;
constexpr std::uint8_t kBillingGameRoomFlag = 0x20;
constexpr char kLogoutConsoleCommand[] = "logout";

void LogInitiatingClientConnection(const char *operation, const char *code) {
  openwow::core::LoginConsoleDiagnostics::Instance().EnqueueFormattedLine(
      "ClientConnection %s: %s code=%s %s", "Initiating", operation, code, "");
}

[[nodiscard]] wotlk::WorldPacket BuildLogoutPacket(const bool force_logout) {
  if (force_logout) {
    return wotlk::WorldPacket(wotlk::Opcode::CMSG_PLAYER_LOGOUT);
  }
  return wotlk::PacketSender::BuildLogoutRequest();
}

[[nodiscard]] constexpr bool IsSuccessfulConnectionResponse(
    const ConnectionResponse response) {
  switch (response) {
  case ConnectionResponse::kSuccess:
  case ConnectionResponse::kConnected:
  case ConnectionResponse::kNegotiationComplete:
  case ConnectionResponse::kAuthOk:
  case ConnectionResponse::kRealmListSuccess:
  case ConnectionResponse::kAccountCreateSuccess:
  case ConnectionResponse::kCharListRetrieved:
  case ConnectionResponse::kCharCreateSuccess:
  case ConnectionResponse::kCharDeleteSuccess:
  case ConnectionResponse::kCharLoginSuccess:
  case ConnectionResponse::kCharNameSuccess:
    return true;
  default:
    return false;
  }
}

void RegisterLogoutConsoleCommand() {
  auto &console = openwow::debug::DebugConsole::Get();
  console.RegisterCommand(
      kLogoutConsoleCommand, "Immediately send CMSG_PLAYER_LOGOUT when the world session is ready",
      [](const std::vector<std::string> &) -> std::string {
        openwow::net::ClientServices::Instance().ForceLogout();
        return {};
      });
}

void UnregisterLogoutConsoleCommand() {
  openwow::debug::DebugConsole::Get().UnregisterCommand(kLogoutConsoleCommand);
}

}

void ClientServices::Initialize() {
  auto &inst = Instance();
  if (inst.initialized_)
    return;
  RegisterLogoutConsoleCommand();
  inst.initialized_ = true;
}

LoginConnectionType ClientServices::GetLoginConnectionType() const {
  if (!has_connection_)
    return LoginConnectionType::kGrunt;
  return login_type_;
}

WowClientConnection *ClientServices::GetConnectionObject() {
  return connection_object_;
}

void ClientServices::SetConnectionObject(WowClientConnection *conn) {
  connection_object_ = conn;
}

bool ClientServices::IsActiveConnectionObject(const void *connection) {
  const auto *active = wotlk::RealmConnection::GetActiveInstance();
  if (connection == nullptr || active == nullptr) {
    return false;
  }
  return connection == static_cast<const void *>(active);
}

bool ClientServices::IsBNLogin() const {

  return has_connection_ && login_type_ == LoginConnectionType::kBattleNet &&
         battlenet_login_ != nullptr;
}

bool ClientServices::IsLoginConnectionTrialAccount() const {
  if (!has_connection_)
    return false;
  return (login_account_flags_ & (std::uint64_t{1} << 3U)) != 0;
}

void ClientServices::SetLoginConnectionAccountFlags(const std::uint64_t account_flags) {
  login_account_flags_ = account_flags;
}

void ClientServices::SetLoginConnectionTrialAccount(bool is_trial_account) {
  constexpr std::uint64_t kTrialAccountMask = std::uint64_t{1} << 3U;
  if (is_trial_account) {
    login_account_flags_ |= kTrialAccountMask;
  } else {
    login_account_flags_ &= ~kTrialAccountMask;
  }
}

bool ClientServices::BypassesRealmCategoryLocaleValidation() const {
  if (!has_connection_) {
    return false;
  }
  return (login_account_flags_ & 1U) != 0;
}

bool ClientServices::BypassesTournamentRealmCategoryValidation() const {
  if (!has_connection_) {
    return false;
  }
  return (login_account_flags_ & (std::uint64_t{1} << 23U)) != 0;
}

void ClientServices::SetRealmCategoryValidationBypassFlagsForTesting(
    const bool locale_validation_bypass, const bool tournament_validation_bypass) {
  constexpr std::uint64_t kLocaleBypassMask = 1U;
  constexpr std::uint64_t kTournamentBypassMask = std::uint64_t{1} << 23U;
  login_account_flags_ &= ~(kLocaleBypassMask | kTournamentBypassMask);
  if (locale_validation_bypass) {
    login_account_flags_ |= kLocaleBypassMask;
  }
  if (tournament_validation_bypass) {
    login_account_flags_ |= kTournamentBypassMask;
  }
}

void ClientServices::SetSelectedRealmScriptMetadata(SelectedRealmScriptMetadata metadata) {
  selected_realm_script_metadata_ = metadata;
}

void ClientServices::SetSelectedRealmAddress(std::string address) {
  selected_realm_address_ = std::move(address);
}

const std::string &ClientServices::GetSelectedRealmAddress() const {
  return selected_realm_address_;
}

void ClientServices::SetSelectedRealmAuthSessionSeedWords(const std::uint32_t seed0,
                                                          const std::uint32_t seed1,
                                                          const std::uint32_t seed2) {
  selected_realm_auth_session_seed_words_ = {seed0, seed1, seed2};
}

const std::array<std::uint32_t, 3> &ClientServices::GetSelectedRealmAuthSessionSeedWords() const {
  return selected_realm_auth_session_seed_words_;
}

void ClientServices::ClearSelectedRealmScriptMetadata() {
  selected_realm_address_.clear();
  selected_realm_auth_session_seed_words_.fill(0);
  selected_realm_script_metadata_.reset();
}

std::optional<SelectedRealmScriptMetadata> ClientServices::GetSelectedRealmScriptMetadata() const {
  return selected_realm_script_metadata_;
}

ClientServices::BillingPlanInfo ClientServices::GetBillingPlan() const {
  BillingPlanInfo info;
  const std::uint8_t masked_plan = billing_flags_ & kBillingPlanMask;
  if (masked_plan == kBillingTimeExcludedFlag) {
    info.plan = 1;
  } else if (masked_plan == 0x04) {
    info.plan = 2;
  } else if (masked_plan == 0) {
    info.plan = 3;
  } else if (masked_plan == kBillingPlanFourFlag) {
    info.plan = 4;
  }

  info.is_game_room = (billing_flags_ & kBillingGameRoomFlag) != 0;
  info.is_paid_time = (billing_flags_ & kBillingPaidTimeFlag) != 0;

  return info;
}

std::uint32_t ClientServices::GetBillingTimeRemaining() const {
  if ((billing_flags_ & kBillingTimeExcludedFlag) != 0) {
    return 0;
  }
  return billing_time_remaining_;
}

std::uint32_t ClientServices::GetBillingTimeRested() const {
  return billing_time_rested_;
}

std::uint8_t ClientServices::GetBillingFlags() const {
  return billing_flags_;
}

void ClientServices::SetWorldAccountBilling(const std::uint32_t time_remaining,
                                            const std::uint8_t flags,
                                            const std::uint32_t rested_time) {
  billing_time_remaining_ = time_remaining;
  billing_flags_ = flags;
  billing_time_rested_ = rested_time;
}

void ClientServices::SetPendingPatchDownloadInfo(PendingPatchDownloadInfo info) {
  pending_patch_download_info_ = std::move(info);
}

std::optional<PendingPatchDownloadInfo>
ClientServices::GetPendingPatchDownloadInfo() const {
  return pending_patch_download_info_;
}

void ClientServices::ClearPendingPatchDownloadInfo() {
  pending_patch_download_info_.reset();
}

void ClientServices::ResetConnectOperationState() {
  operation_complete_ = true;
  operation_success_ = false;
  is_character_login_pending_ = false;
  world_session_ready_ = false;
  cleanup_callback_ = nullptr;
  ResetLogoutRequestState();
}

void ClientServices::LogConnectionStatus(const ClientOperation op,
                                         const ConnectionResponse status,
                                         const bool initiating,
                                         const bool result_success) {
  std::array<char, 32> op_buffer{};
  const char *op_str = op_buffer.data();
  switch (op) {
  case ClientOperation::kNone:
    op_str = "COP_NONE";
    break;
  case ClientOperation::kInit:
    op_str = "COP_INIT";
    break;
  case ClientOperation::kConnect:
    op_str = "COP_CONNECT";
    break;
  case ClientOperation::kAuthenticate:
    op_str = "COP_AUTHENTICATE";
    break;
  case ClientOperation::kCreateAccount:
    op_str = "COP_CREATE_ACCOUNT";
    break;
  case ClientOperation::kCreateCharacter:
    op_str = "COP_CREATE_CHARACTER";
    break;
  case ClientOperation::kGetCharacters:
    op_str = "COP_GET_CHARACTERS";
    break;
  case ClientOperation::kDeleteCharacter:
    op_str = "COP_DELETE_CHARACTER";
    break;
  case ClientOperation::kLoginCharacter:
    op_str = "COP_LOGIN_CHARACTER";
    break;
  case ClientOperation::kGetRealms:
    op_str = "COP_GET_REALMS";
    break;
  case ClientOperation::kWaitQueue:
    op_str = "COP_WAIT_QUEUE";
    break;
  default:
    openwow::core::SStrPrintf(op_buffer.data(), op_buffer.size(), "%d",
                              static_cast<std::int32_t>(op));
    break;
  }

  std::array<char, 32> status_buffer{};
  const char *status_str = status_buffer.data();
  switch (status) {
  case ConnectionResponse::kSuccess:
    status_str = "RESPONSE_SUCCESS";
    break;
  case ConnectionResponse::kFailure:
    status_str = "RESPONSE_FAILURE";
    break;
  case ConnectionResponse::kCancelled:
    status_str = "RESPONSE_CANCELLED";
    break;
  case ConnectionResponse::kDisconnected:
    status_str = "RESPONSE_DISCONNECTED";
    break;
  case ConnectionResponse::kFailedToConnect:
    status_str = "RESPONSE_FAILED_TO_CONNECT";
    break;
  case ConnectionResponse::kConnected:
    status_str = "RESPONSE_CONNECTED";
    break;
  case ConnectionResponse::kVersionMismatch:
    status_str = "RESPONSE_VERSION_MISMATCH";
    break;
  case ConnectionResponse::kConnecting:
    status_str = "CSTATUS_CONNECTING";
    break;
  case ConnectionResponse::kNegotiatingSecurity:
    status_str = "CSTATUS_NEGOTIATING_SECURITY";
    break;
  case ConnectionResponse::kNegotiationComplete:
    status_str = "CSTATUS_NEGOTIATION_COMPLETE";
    break;
  case ConnectionResponse::kNegotiationFailed:
    status_str = "CSTATUS_NEGOTIATION_FAILED";
    break;
  case ConnectionResponse::kAuthenticating:
    status_str = "CSTATUS_AUTHENTICATING";
    break;
  case ConnectionResponse::kAuthOk:
    status_str = "AUTH_OK";
    break;
  case ConnectionResponse::kAuthFailed:
    status_str = "AUTH_FAILED";
    break;
  case ConnectionResponse::kAuthReject:
    status_str = "AUTH_REJECT";
    break;
  case ConnectionResponse::kAuthBadServerProof:
    status_str = "AUTH_BAD_SERVER_PROOF";
    break;
  case ConnectionResponse::kAuthUnavailable:
    status_str = "AUTH_UNAVAILABLE";
    break;
  case ConnectionResponse::kAuthSystemError:
    status_str = "AUTH_SYSTEM_ERROR";
    break;
  case ConnectionResponse::kAuthBillingError:
    status_str = "AUTH_BILLING_ERROR";
    break;
  case ConnectionResponse::kAuthBillingExpired:
    status_str = "AUTH_BILLING_EXPIRED";
    break;
  case ConnectionResponse::kAuthVersionMismatch:
    status_str = "AUTH_VERSION_MISMATCH";
    break;
  case ConnectionResponse::kAuthUnknownAccount:
    status_str = "AUTH_UNKNOWN_ACCOUNT";
    break;
  case ConnectionResponse::kAuthIncorrectPassword:
    status_str = "AUTH_INCORRECT_PASSWORD";
    break;
  case ConnectionResponse::kAuthSessionExpired:
    status_str = "AUTH_SESSION_EXPIRED";
    break;
  case ConnectionResponse::kAuthServerShuttingDown:
    status_str = "AUTH_SERVER_SHUTTING_DOWN";
    break;
  case ConnectionResponse::kAuthAlreadyLoggingIn:
    status_str = "AUTH_ALREADY_LOGGING_IN";
    break;
  case ConnectionResponse::kAuthLoginServerNotFound:
    status_str = "AUTH_LOGIN_SERVER_NOT_FOUND";
    break;
  case ConnectionResponse::kAuthWaitQueue:
    status_str = "AUTH_WAIT_QUEUE";
    break;
  case ConnectionResponse::kRealmListInProgress:
    status_str = "REALM_LIST_IN_PROGRESS";
    break;
  case ConnectionResponse::kRealmListSuccess:
    status_str = "REALM_LIST_SUCCESS";
    break;
  case ConnectionResponse::kRealmListFailed:
    status_str = "REALM_LIST_FAILED";
    break;
  case ConnectionResponse::kRealmListInvalid:
    status_str = "REALM_LIST_INVALID";
    break;
  case ConnectionResponse::kRealmListRealmNotFound:
    status_str = "REALM_LIST_REALM_NOT_FOUND";
    break;
  case ConnectionResponse::kCharListRetrieving:
    status_str = "CHAR_LIST_RETRIEVING";
    break;
  case ConnectionResponse::kCharListRetrieved:
    status_str = "CHAR_LIST_RETRIEVED";
    break;
  case ConnectionResponse::kCharListFailed:
    status_str = "CHAR_LIST_FAILED";
    break;
  case ConnectionResponse::kCharCreateInProgress:
    status_str = "CHAR_CREATE_IN_PROGRESS";
    break;
  case ConnectionResponse::kCharCreateSuccess:
    status_str = "CHAR_CREATE_SUCCESS";
    break;
  case ConnectionResponse::kCharDeleteInProgress:
    status_str = "CHAR_DELETE_IN_PROGRESS";
    break;
  case ConnectionResponse::kCharDeleteSuccess:
    status_str = "CHAR_DELETE_SUCCESS";
    break;
  case ConnectionResponse::kCharDeleteFailedGuildLeader:
    status_str = "CHAR_DELETE_FAILED_GUILD_LEADER";
    break;
  case ConnectionResponse::kCharDeleteFailedArenaCaptain:
    status_str = "CHAR_DELETE_FAILED_ARENA_CAPTAIN";
    break;
  default:
    openwow::core::SStrPrintf(status_buffer.data(), status_buffer.size(), "%d",
                              static_cast<std::int32_t>(status));
    break;
  }

  const char *phase = "Completed";
  const char *result_suffix = result_success ? "result=TRUE" : "result=FALSE";
  if (initiating) {
    phase = "Initiating";
    result_suffix = "";
  }

  openwow::core::LoginConsoleDiagnostics::Instance().EnqueueFormattedLine(
      "ClientConnection %s: %s code=%s %s", phase, op_str, status_str, result_suffix);
}

const char *ClientServices::GetResultString(std::int32_t result_code) {
  if (result_code < 1 || result_code > 103)
    return "";

  static const char *kResultStrings[104] = {
       "",
       "RESPONSE_FAILURE",
       "RESPONSE_CANCELLED",
       "RESPONSE_DISCONNECTED",
       "RESPONSE_FAILED_TO_CONNECT",
       "RESPONSE_CONNECTED",
       "RESPONSE_VERSION_MISMATCH",
       "CSTATUS_CONNECTING",
       "CSTATUS_NEGOTIATING_SECURITY",
       "CSTATUS_NEGOTIATION_COMPLETE",
       "CSTATUS_NEGOTIATION_FAILED",
       "CSTATUS_AUTHENTICATING",
       "AUTH_OK",
       "AUTH_FAILED",
       "AUTH_REJECT",
       "AUTH_BAD_SERVER_PROOF",
       "AUTH_UNAVAILABLE",
       "AUTH_SYSTEM_ERROR",
       "AUTH_BILLING_ERROR",
       "AUTH_BILLING_EXPIRED",
       "AUTH_VERSION_MISMATCH",
       "AUTH_UNKNOWN_ACCOUNT",
       "AUTH_INCORRECT_PASSWORD",
       "AUTH_SESSION_EXPIRED",
       "AUTH_SERVER_SHUTTING_DOWN",
       "AUTH_ALREADY_LOGGING_IN",
       "AUTH_LOGIN_SERVER_NOT_FOUND",
       "AUTH_WAIT_QUEUE",
       "AUTH_BANNED",
       "AUTH_ALREADY_ONLINE",
       "AUTH_NO_TIME",
       "AUTH_DB_BUSY",
       "AUTH_SUSPENDED",
       "AUTH_PARENTAL_CONTROL",
       "AUTH_LOCKED_ENFORCED",
       "REALM_LIST_IN_PROGRESS",
       "REALM_LIST_SUCCESS",
       "REALM_LIST_FAILED",
       "REALM_LIST_INVALID",
       "REALM_LIST_REALM_NOT_FOUND",
       "ACCOUNT_CREATE_IN_PROGRESS",
       "ACCOUNT_CREATE_SUCCESS",
       "ACCOUNT_CREATE_FAILED",
       "CHAR_LIST_RETRIEVING",
       "CHAR_LIST_RETRIEVED",
       "CHAR_LIST_FAILED",
       "CHAR_CREATE_IN_PROGRESS",
       "CHAR_CREATE_SUCCESS",
       "CHAR_CREATE_ERROR",
       "CHAR_CREATE_FAILED",
       "CHAR_CREATE_NAME_IN_USE",
       "CHAR_CREATE_DISABLED",
       "CHAR_CREATE_PVP_TEAMS_VIOLATION",
       "CHAR_CREATE_SERVER_LIMIT",
       "CHAR_CREATE_ACCOUNT_LIMIT",
       "CHAR_CREATE_SERVER_QUEUE",
       "CHAR_CREATE_ONLY_EXISTING",
       "CHAR_CREATE_EXPANSION",
       "CHAR_CREATE_EXPANSION_CLASS",
       "CHAR_CREATE_LEVEL_REQUIREMENT",
       "CHAR_CREATE_UNIQUE_CLASS_LIMIT",
       "CHAR_CREATE_CHARACTER_IN_GUILD",
       "CHAR_CREATE_RESTRICTED_RACECLASS",
       "CHAR_CREATE_CHARACTER_CHOOSE_RACE",
       "CHAR_CREATE_CHARACTER_ARENA_LEADER",
       "CHAR_CREATE_CHARACTER_DELETE_MAIL",
       "CHAR_CREATE_CHARACTER_SWAP_FACTION",
       "CHAR_CREATE_CHARACTER_RACE_ONLY",
       "CHAR_CREATE_CHARACTER_GOLD_LIMIT",
       "CHAR_CREATE_FORCE_LOGIN",
       "CHAR_DELETE_IN_PROGRESS",
       "CHAR_DELETE_SUCCESS",
       "CHAR_DELETE_FAILED",
       "CHAR_DELETE_FAILED_LOCKED_FOR_TRANSFER",
       "CHAR_DELETE_FAILED_GUILD_LEADER",
       "CHAR_DELETE_FAILED_ARENA_CAPTAIN",
       "CHAR_LOGIN_IN_PROGRESS",
       "CHAR_LOGIN_SUCCESS",
       "CHAR_LOGIN_NO_WORLD",
       "CHAR_LOGIN_DUPLICATE_CHARACTER",
       "CHAR_LOGIN_NO_INSTANCES",
       "CHAR_LOGIN_FAILED",
       "CHAR_LOGIN_DISABLED",
       "CHAR_LOGIN_NO_CHARACTER",
       "CHAR_LOGIN_LOCKED_FOR_TRANSFER",
       "CHAR_LOGIN_LOCKED_BY_BILLING",
       "CHAR_LOGIN_LOCKED_BY_MOBILE_AH",
       "CHAR_NAME_SUCCESS",
       "CHAR_NAME_FAILURE",
       "CHAR_NAME_NO_NAME",
       "CHAR_NAME_TOO_SHORT",
       "CHAR_NAME_TOO_LONG",
       "CHAR_NAME_INVALID_CHARACTER",
       "CHAR_NAME_MIXED_LANGUAGES",
       "CHAR_NAME_PROFANE",
       "CHAR_NAME_RESERVED",
       "CHAR_NAME_INVALID_APOSTROPHE",
       "CHAR_NAME_MULTIPLE_APOSTROPHES",
       "CHAR_NAME_THREE_CONSECUTIVE",
       "CHAR_NAME_INVALID_SPACE",
       "CHAR_NAME_CONSECUTIVE_SPACES",
       "CHAR_NAME_RUSSIAN_CONSECUTIVE_SILENT_CHARACTERS",
       "CHAR_NAME_RUSSIAN_SILENT_CHARACTER_AT_BEGINNING_OR_END",
       "CHAR_NAME_DECLENSION_DOESNT_MATCH_BASE_NAME",
  };
  return kResultStrings[result_code];
}

DisconnectDialogInfo ClientServices::GetDisconnectDialogInfo() const {
  DisconnectDialogInfo info;
  info.current_op = current_op_;
  info.expected_response = expected_response_;
  info.operation_success = operation_success_;
  info.has_pending_result = operation_complete_;

  if (!operation_complete_) {
    return info;
  }

  const char *response_key = GetResultString(expected_response_);

  auto &localization = openwow::game::Localization::Get();
  const bool has_localized =
      response_key != nullptr && *response_key != '\0' &&
      localization.HasString(response_key);

  if (!has_localized) {
    std::array<char, 256> buf{};
    openwow::core::SStrPrintf(buf.data(), buf.size(), "(%i)", expected_response_);
    info.message = buf.data();
    return info;
  }

  if (expected_response_ == static_cast<std::int32_t>(ConnectionResponse::kAuthWaitQueue)) {
    const auto *active_conn = wotlk::RealmConnection::GetActiveInstance();

    if (active_conn == nullptr) {
      return info;
    }
    const std::string format = localization.GetString(response_key);
    std::array<char, 256> buf{};
    openwow::core::SStrPrintf(buf.data(), buf.size(), format.c_str(),
                              active_conn->queue_position());
    info.message = buf.data();
    return info;
  }

  info.message = localization.GetString(response_key);
  return info;
}

void ClientServices::InvokeAndClearCleanup() {
  if (cleanup_callback_) {
    cleanup_callback_();
    cleanup_callback_ = nullptr;
  }
}

void ClientServices::PublishLogoutCancellationIfPending() {
  if (!logout_request_active_) {
    return;
  }

  ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::LOGOUT_CANCEL);
  logout_request_active_ = false;
}

bool ClientServices::SendLogoutPacket(const bool shutdown_after_logout,
                                      const bool force_logout) {
  if (logout_request_active_ && !force_logout) {
    return false;
  }
  if (!world_session_ready_) {
    return false;
  }

  auto packet = BuildLogoutPacket(force_logout);
  if (!ClientServices__SendPacket(packet)) {
    return false;
  }

  shutdown_after_logout_ = shutdown_after_logout;
  if (!force_logout) {
    logout_request_active_ = true;
  }
  return true;
}

void ClientServices::ResetLogoutRequestState() {
  shutdown_after_logout_ = false;
  logout_request_active_ = false;
}

void ClientServices::SetOperationState(ClientOperation op, std::int32_t expected_response) {

  cleanup_callback_ = nullptr;
  current_op_ = op;
  expected_response_ = expected_response;
  operation_complete_ = false;
}

void ClientServices::SyncLoginTransport() {
  if (has_connection_ && login_type_ == LoginConnectionType::kBattleNet) {
    if (!battlenet_login_) {
      battlenet_login_ = std::make_unique<openwow::game::BattlenetLogin>();
    }
    return;
  }

  battlenet_login_.reset();
}

void ClientServices::BeginAuthenticateOperation([[maybe_unused]] const char *account_name,
                                                [[maybe_unused]] const char *password) {
  assert(operation_complete_ && "BeginAuthenticateOperation requires operation_complete");
  assert(account_name && "BeginAuthenticateOperation requires non-null account_name");
  assert(password && "BeginAuthenticateOperation requires non-null password");

  cleanup_callback_ = nullptr;
  current_op_ = ClientOperation::kAuthenticate;
  expected_response_ =
      static_cast<std::int32_t>(ConnectionResponse::kAuthenticating);
  operation_complete_ = false;

  LogConnectionStatus(ClientOperation::kAuthenticate,
                      ConnectionResponse::kAuthenticating,
                      true, false);
}

void ClientServices::Login(const std::string &account_name,
                           [[maybe_unused]] const std::string &password) {
  const bool is_bnet = (account_name.find('@') != std::string::npos);
  login_type_ = is_bnet ? LoginConnectionType::kBattleNet : LoginConnectionType::kGrunt;

  SetAccountName(ApplyLocaleSuffix(account_name, locale_, region_id_, bnet_login_required_));
  login_account_flags_ = 0;
  has_connection_ = true;
  current_op_ = ClientOperation::kAuthenticate;
  SyncLoginTransport();
}

std::string ClientServices::ApplyLocaleSuffix(const std::string &account, WowLocale locale,
                                              std::int32_t region_id, bool bnet_required) const {
  if (!bnet_required)
    return account;

  switch (locale) {
  case WowLocale::kEnUS:
  case WowLocale::kEnGB:
    return account + (region_id == 3 ? "#EU" : "#US");
  case WowLocale::kKoKR:
    return account + "#KR";
  case WowLocale::kFrFR:
  case WowLocale::kDeDE:
  case WowLocale::kEsES:
  case WowLocale::kRuRU:
    return account + "#EU";
  case WowLocale::kZhCN:
  case WowLocale::kZhTW:
    return account + "#CN";
  default:
    return account;
  }
}

void ClientServices::LoginCharacter(std::uint64_t ) {
  SetOperationState(ClientOperation::kLoginCharacter, 76);
  LogInitiatingClientConnection("COP_LOGIN_CHARACTER", "76");

  if (!is_world_connected_) {
    CompletePendingOperation(ConnectionResponse::kFailedToConnect);
    return;
  }

  is_character_login_pending_ = true;
}

void ClientServices::CreateCharacter(const std::string & ) {
  SetOperationState(ClientOperation::kCreateCharacter, 46);
  LogInitiatingClientConnection("COP_CREATE_CHARACTER", "46");
  if (!is_world_connected_) {
    CompletePendingOperation(ConnectionResponse::kFailedToConnect);
    return;
  }

}

void ClientServices::DeleteCharacter(std::uint64_t ) {
  SetOperationState(ClientOperation::kDeleteCharacter, 70);
  LogInitiatingClientConnection("COP_DELETE_CHARACTER", "70");
  if (!is_world_connected_) {
    CompletePendingOperation(ConnectionResponse::kFailedToConnect);
    return;
  }

}

void ClientServices::GetCharacters() {
  SetOperationState(ClientOperation::kGetCharacters, 43);
  LogInitiatingClientConnection("COP_GET_CHARACTERS", "43");
  if (!is_world_connected_) {
    CompletePendingOperation(ConnectionResponse::kFailedToConnect);
    return;
  }

}

void ClientServices::GetRealmList() {
  cleanup_callback_ = nullptr;

  current_op_ = ClientOperation::kGetRealms;
  expected_response_ = 35;
  operation_complete_ = false;
  LogInitiatingClientConnection("COP_GET_REALMS", "REALM_LIST_IN_PROGRESS");

  auto& auth = auth::AuthSession::Get();
  if (auth.IsAuthServerConnected()) {
    auth.RequestRealmList();
  } else {
    auth.Reconnect();
  }
}

void ClientServices::ConnectToRealm() {
  assert(operation_complete_ && "ConnectToRealm requires operation_complete");

  ResetConnectOperationState();
  some_flag_ = false;
  cleanup_callback_ = nullptr;
  current_op_ = ClientOperation::kConnect;
  expected_response_ = 7;
  operation_complete_ = false;
  LogInitiatingClientConnection("COP_CONNECT", "CSTATUS_CONNECTING");

  if (is_world_connected_) {
    InvokeAndClearCleanup();
    operation_success_ = true;
    expected_response_ =
        static_cast<std::int32_t>(ConnectionResponse::kConnected);
    operation_complete_ = true;
    LogConnectionStatus(current_op_, ConnectionResponse::kConnected, false,
                        true);
  }

}

void ClientServices::HandleEnterWorldInit() {
  InvokeAndClearCleanup();
  operation_success_ = true;
  expected_response_ = 5;
  operation_complete_ = true;
  LogConnectionStatus(current_op_, ConnectionResponse::kConnected, false, true);
  is_world_connected_ = true;
}

void ClientServices::CompleteCharacterLoginTransition(const bool world_session_ready) {
  InvokeAndClearCleanup();
  operation_success_ = true;
  operation_complete_ = true;
  expected_response_ = static_cast<std::int32_t>(ConnectionResponse::kCharLoginSuccess);
  LogConnectionStatus(current_op_, ConnectionResponse::kCharLoginSuccess, false, true);
  world_session_ready_ = world_session_ready;
  PublishLogoutCancellationIfPending();
}

void ClientServices::HandleDisconnectWithCleanup() {
  InvokeAndClearCleanup();
  operation_complete_ = true;
  is_world_connected_ = false;
  is_character_login_pending_ = false;
  world_session_ready_ = false;
  ResetLogoutRequestState();
  openwow::core::Console_UnregisterDebugCommands();
}

void ClientServices::DisconnectAndCleanup() {
  is_world_connected_ = false;
  is_character_login_pending_ = false;
  world_session_ready_ = false;
  ResetLogoutRequestState();
  openwow::core::Console_UnregisterDebugCommands();
}

bool ClientServices::RequestLogout() {
  return SendLogoutPacket(false, false);
}

bool ClientServices::RequestQuit() {
  return SendLogoutPacket(true, false);
}

bool ClientServices::RequestLogoutCancel() {
  if (!world_session_ready_) {
    return false;
  }

  if (!ClientServices__SendPacket(wotlk::PacketSender::BuildLogoutCancel())) {
    return false;
  }

  logout_request_active_ = false;
  return true;
}

bool ClientServices::ForceLogout() {
  return SendLogoutPacket(false, true);
}

void ClientServices::HandleLogoutComplete() {
  PublishLogoutCancellationIfPending();
  if (world_session_ready_) {
    CompleteCharacterLoginTransition(false);
  }

  const bool shutdown_after_logout = shutdown_after_logout_;
  is_character_login_pending_ = false;
  world_session_ready_ = false;
  logout_request_active_ = false;
  shutdown_after_logout_ = false;
  if (shutdown_after_logout) {
    openwow::core::RequestClientShutdownWithErrorCode(0);
  }
}

void ClientServices::HandleLogoutCancelAck() {
  PublishLogoutCancellationIfPending();
}

void ClientServices::OnLogoutResponse(const std::uint32_t result,
                                      const std::uint8_t instant_flag) {
  if (result != 0) {
    ui::game::DisplaySystemMessage(410);
    logout_request_active_ = false;
  } else if (instant_flag == 0) {
    const char *event = shutdown_after_logout_
                            ? ui::game::events::PLAYER_QUITING
                            : ui::game::events::PLAYER_CAMPING;
    ui::game::ScriptEventDispatch::Get().FireEvent(event);
  }
}

void ClientServices::FullLogout() {

  openwow::core::Console_UnregisterDebugCommands();
  UnregisterLogoutConsoleCommand();
  auth::AuthSession::Get().Reset();
  Reset();
  locale_ = WowLocale::kEnUS;
  region_id_ = 0;
  bnet_login_required_ = false;

  some_flag_ = false;
  initialized_ = false;
}

void ClientServices::OnAuthResponse(ConnectionResponse result) {
  if (result == ConnectionResponse::kAuthWaitQueue) {
    current_op_ = ClientOperation::kWaitQueue;
    expected_response_ = static_cast<std::int32_t>(ConnectionResponse::kAuthWaitQueue);
    operation_complete_ = false;
    LogConnectionStatus(ClientOperation::kWaitQueue, ConnectionResponse::kAuthWaitQueue,
                        true, false);

  } else {
    HandleAuthResult(result);
  }
}

void ClientServices::HandleAuthResult(ConnectionResponse result) {
  InvokeAndClearCleanup();
  expected_response_ = static_cast<std::int32_t>(result);
  operation_complete_ = true;
  operation_success_ = (result == ConnectionResponse::kAuthOk);
  LogConnectionStatus(current_op_, result, false, operation_success_);
}

void ClientServices::HandleCharCreateResult(std::uint8_t result) {
  InvokeAndClearCleanup();
  auto r = static_cast<ConnectionResponse>(result);
  expected_response_ = result;
  operation_complete_ = true;
  operation_success_ = (result == 47);
  LogConnectionStatus(current_op_, r, false, operation_success_);
}

void ClientServices::HandleCharDeleteResult(std::uint8_t result) {
  InvokeAndClearCleanup();
  auto r = static_cast<ConnectionResponse>(result);
  expected_response_ = result;
  operation_complete_ = true;
  operation_success_ = (result == 71);
  LogConnectionStatus(current_op_, r, false, operation_success_);
}

void ClientServices::HandleLoginResult(std::uint8_t reason) {
  if (world_session_ready_) {
    CompleteCharacterLoginTransition(false);
  }
  is_character_login_pending_ = false;
  world_session_ready_ = false;

  std::int32_t mapped;
  switch (reason) {
  case 1:
    mapped = static_cast<std::int32_t>(ConnectionResponse::kCharLoginNoWorld);
    break;
  case 2:
    mapped = static_cast<std::int32_t>(ConnectionResponse::kCharLoginDuplicateCharacter);
    break;
  case 3:
    mapped = static_cast<std::int32_t>(ConnectionResponse::kCharLoginNoInstances);
    break;
  case 4:
    mapped = static_cast<std::int32_t>(ConnectionResponse::kCharLoginDisabled);
    break;
  case 5:
    mapped = static_cast<std::int32_t>(ConnectionResponse::kCharLoginNoCharacter);
    break;
  case 6:
    mapped = static_cast<std::int32_t>(ConnectionResponse::kCharLoginLockedForTransfer);
    break;
  case 7:
    mapped = static_cast<std::int32_t>(ConnectionResponse::kCharLoginLockedByBilling);
    break;
  case 8:
    mapped = static_cast<std::int32_t>(ConnectionResponse::kCharLoginLockedByMobileAh);
    break;
  default:
    mapped = static_cast<std::int32_t>(ConnectionResponse::kCharLoginFailed);
    break;
  }
  CompletePendingOperation(static_cast<ConnectionResponse>(mapped));
}

void ClientServices::CompletePendingOperation(const ConnectionResponse response) {
  InvokeAndClearCleanup();
  operation_success_ = IsSuccessfulConnectionResponse(response);
  expected_response_ = static_cast<std::int32_t>(response);
  operation_complete_ = true;
  LogConnectionStatus(current_op_, response, false, operation_success_);
}

void ClientServices::SetCleanupCallback(CleanupCallback cb) {
  cleanup_callback_ = std::move(cb);
}

void ClientServices::SetLoginFileTransferResponseSender(
    LoginFileTransferResponseSender sender) {
  login_file_transfer_response_sender_ = std::move(sender);
}

void ClientServices::SendLoginFileTransferResponse(
    const bool transfer_needed,
    const std::uint64_t resume_offset) {
  if (has_connection_ && login_type_ == LoginConnectionType::kGrunt &&
      login_file_transfer_response_sender_) {
    (void)login_file_transfer_response_sender_(transfer_needed, resume_offset);
  }
  file_transfer_active_on_login_net_ = true;
}

void ClientServices::CancelLoginFileTransfer() {

  if (has_connection_ && login_type_ == LoginConnectionType::kGrunt &&
      login_file_transfer_response_sender_) {
    (void)login_file_transfer_response_sender_(false, 0);
  }
  file_transfer_active_on_login_net_ = false;
}

void ClientServices::SetFileTransferActiveOnLoginNet() {
  file_transfer_active_on_login_net_ = true;
}

bool ClientServices::IsFileTransferActiveOnLoginNet() const {
  return file_transfer_active_on_login_net_;
}

void ClientServices::ClearFileTransferActiveOnLoginNet() {
  file_transfer_active_on_login_net_ = false;
}

bool ClientServices::HasLoginConnection() const {
  return has_connection_;
}
bool ClientServices::IsWorldConnected() const {
  return is_world_connected_;
}
bool ClientServices::HasPendingLogoutRequest() const {
  return logout_request_active_;
}
ClientOperation ClientServices::GetCurrentOperation() const {
  return current_op_;
}
bool ClientServices::IsOperationComplete() const {
  return operation_complete_;
}
bool ClientServices::IsOperationSuccess() const {
  return operation_success_;
}
std::int32_t ClientServices::GetExpectedResponseCode() const {
  return expected_response_;
}
const std::string &ClientServices::GetAccountName() const {
  return account_name_;
}
openwow::game::BattlenetLogin *ClientServices::GetBattlenetLogin() {
  if (!has_connection_ || login_type_ != LoginConnectionType::kBattleNet) {
    return nullptr;
  }

  return battlenet_login_.get();
}
const openwow::game::BattlenetLogin *ClientServices::GetBattlenetLogin() const {
  if (!has_connection_ || login_type_ != LoginConnectionType::kBattleNet) {
    return nullptr;
  }

  return battlenet_login_.get();
}
bool ClientServices::HasBattleNetRidTransport() const {
  if (!IsBNLogin()) {
    return false;
  }

  const auto *battlenet_login = GetBattlenetLogin();

  return battlenet_login != nullptr && !battlenet_login->HasRidFeatureBlockFlag();
}
void ClientServices::SetAccountName(const std::string &account_name) {
  account_name_ = openwow::text::ToUpperAscii(account_name);
}
WowLocale ClientServices::GetCurrentLocale() const {
  return locale_;
}
void ClientServices::SetCurrentLocale(WowLocale locale) {
  locale_ = locale;
}
void ClientServices::SetRegionId(std::int32_t region) {
  region_id_ = region;
}
std::int32_t ClientServices::GetRegionId() const {
  return region_id_;
}
void ClientServices::SetBNetLoginRequired(bool required) {
  bnet_login_required_ = required;
}
bool ClientServices::IsBNetLoginRequired() const {
  return bnet_login_required_;
}
void ClientServices::SetLoginConnectionForTesting(LoginConnectionType type, bool has_connection) {
  login_type_ = type;
  has_connection_ = has_connection;
  if (!has_connection_) {
    login_account_flags_ = 0;
  }
  SyncLoginTransport();
}
void ClientServices::SetExpansionLevel(std::uint8_t level) {
  expansion_level_ = level;
}
std::uint8_t ClientServices::GetExpansionLevel() const {
  return expansion_level_;
}

void ClientServices::Disconnect() {
  auth::AuthSession::Get().Reset();
  has_connection_ = false;
  current_op_ = ClientOperation::kNone;
  expected_response_ = 0;
  login_account_flags_ = 0;
  file_transfer_active_on_login_net_ = false;
  login_file_transfer_response_sender_ = {};
  is_character_login_pending_ = false;
  world_session_ready_ = false;
  ResetLogoutRequestState();
  battlenet_login_.reset();
  ClearPendingPatchDownloadInfo();
}

void ClientServices::Reset() {
  login_type_ = LoginConnectionType::kGrunt;
  current_op_ = ClientOperation::kNone;
  expected_response_ = 0;
  account_name_.clear();
  has_connection_ = false;
  is_world_connected_ = false;
  is_character_login_pending_ = false;
  operation_complete_ = true;
  operation_success_ = true;
  world_session_ready_ = false;
  login_account_flags_ = 0;
  file_transfer_active_on_login_net_ = false;
  some_flag_ = false;

  SetWorldAccountBilling(0, 0, 0);
  cleanup_callback_ = nullptr;
  login_file_transfer_response_sender_ = {};
  ResetLogoutRequestState();
  battlenet_login_.reset();
  ClearPendingPatchDownloadInfo();
  ClearSelectedRealmScriptMetadata();
}

std::int32_t ClientServices::GetTimezoneBias() {
  return QueryRetailLogonChallengeTimezoneBiasMinutes();
}

const char *GetRealmName() {
  thread_local std::string realm_name;
  realm_name = openwow::ui::game::CVarSystem::Instance().GetCVar("realmName");
  return realm_name.c_str();
}

std::uint8_t GetClientExpansionLevel() {
  return s_expansion_byte;
}

int ClientServices__RegisterOpcodeHandlerWrapper(int opcode,
                                                 OpcodeHandlerFn_t handler,
                                                 std::uintptr_t context) {
  assert(handler && "ClientServices: RegisterOpcodeHandler called with null handler");
  auto *conn = ClientServices::GetConnectionObject();
  assert(conn && "ClientServices: RegisterOpcodeHandler called with no active connection");
  return WowClientConnection_RegisterOpcodeHandler(
      conn, opcode, handler, context);
}

int ClientServices__UnregisterOpcodeHandler(int opcode) {
  auto *conn = ClientServices::GetConnectionObject();
  assert(conn && "ClientServices: UnregisterOpcodeHandler called with no active connection");
  return WowClientConnection_UnregisterOpcodeHandler(conn, opcode);
}

}

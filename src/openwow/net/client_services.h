
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace openwow::game {
class BattlenetLogin;
}

namespace openwow::net {

struct WowClientConnection;

enum class LoginConnectionType : std::int32_t {
  kGrunt = 0,
  kBattleNet = 1,
};

struct SelectedRealmScriptMetadata {
  std::uint8_t category{0};
  std::uint32_t realm_type{0};
  bool is_pvp_flag{false};
};

struct PendingPatchDownloadInfo {
  std::string filename;
  std::array<std::uint8_t, 16> digest{};
  std::uint64_t expected_size{0};
};

enum class ClientOperation : std::int32_t {
  kNone = 0,
  kInit = 1,
  kConnect = 2,
  kAuthenticate = 3,
  kCreateAccount = 4,
  kCreateCharacter = 5,
  kGetCharacters = 6,
  kDeleteCharacter = 7,
  kLoginCharacter = 8,
  kGetRealms = 9,
  kWaitQueue = 10,
};

enum class ConnectionResponse : std::int32_t {
  kSuccess = 0,
  kFailure = 1,
  kCancelled = 2,
  kDisconnected = 3,
  kFailedToConnect = 4,
  kConnected = 5,
  kVersionMismatch = 6,
  kConnecting = 7,
  kNegotiatingSecurity = 8,
  kNegotiationComplete = 9,
  kNegotiationFailed = 10,
  kAuthenticating = 11,
  kAuthOk = 12,
  kAuthFailed = 13,
  kAuthReject = 14,
  kAuthBadServerProof = 15,
  kAuthUnavailable = 16,
  kAuthSystemError = 17,
  kAuthBillingError = 18,
  kAuthBillingExpired = 19,
  kAuthVersionMismatch = 20,
  kAuthUnknownAccount = 21,
  kAuthIncorrectPassword = 22,
  kAuthSessionExpired = 23,
  kAuthServerShuttingDown = 24,
  kAuthAlreadyLoggingIn = 25,
  kAuthLoginServerNotFound = 26,
  kAuthWaitQueue = 27,
  kAuthBanned = 28,
  kAuthAlreadyOnline = 29,
  kAuthNoTime = 30,
  kAuthDbBusy = 31,
  kAuthSuspended = 32,
  kAuthParentalControl = 33,
  kAuthLockedEnforced = 34,
  kRealmListInProgress = 35,
  kRealmListSuccess = 36,
  kRealmListFailed = 37,
  kRealmListInvalid = 38,
  kRealmListRealmNotFound = 39,
  kAccountCreateInProgress = 40,
  kAccountCreateSuccess = 41,
  kAccountCreateFailed = 42,
  kCharListRetrieving = 43,
  kCharListRetrieved = 44,
  kCharListFailed = 45,
  kCharCreateInProgress = 46,
  kCharCreateSuccess = 47,
  kCharCreateError = 48,
  kCharCreateFailed = 49,
  kCharCreateNameInUse = 50,
  kCharCreateDisabled = 51,
  kCharCreatePvpTeamsViolation = 52,
  kCharCreateServerLimit = 53,
  kCharCreateAccountLimit = 54,
  kCharCreateServerQueue = 55,
  kCharCreateOnlyExisting = 56,
  kCharCreateExpansion = 57,
  kCharCreateExpansionClass = 58,
  kCharCreateLevelRequirement = 59,
  kCharCreateUniqueClassLimit = 60,
  kCharCreateCharacterInGuild = 61,
  kCharCreateRestrictedRaceclass = 62,
  kCharCreateCharacterChooseRace = 63,
  kCharCreateCharacterArenaLeader = 64,
  kCharCreateCharacterDeleteMail = 65,
  kCharCreateCharacterSwapFaction = 66,
  kCharCreateCharacterRaceOnly = 67,
  kCharCreateCharacterGoldLimit = 68,
  kCharCreateForceLogin = 69,
  kCharDeleteInProgress = 70,
  kCharDeleteSuccess = 71,
  kCharDeleteFailed = 72,
  kCharDeleteFailedLockedForTransfer = 73,
  kCharDeleteFailedGuildLeader = 74,
  kCharDeleteFailedArenaCaptain = 75,
  kCharLoginInProgress = 76,
  kCharLoginSuccess = 77,
  kCharLoginNoWorld = 78,
  kCharLoginDuplicateCharacter = 79,
  kCharLoginNoInstances = 80,
  kCharLoginFailed = 81,
  kCharLoginDisabled = 82,
  kCharLoginNoCharacter = 83,
  kCharLoginLockedForTransfer = 84,
  kCharLoginLockedByBilling = 85,
  kCharLoginLockedByMobileAh = 86,
  kCharNameSuccess = 87,
  kCharNameFailure = 88,
  kCharNameNoName = 89,
  kCharNameTooShort = 90,
  kCharNameTooLong = 91,
  kCharNameInvalidCharacter = 92,
  kCharNameMixedLanguages = 93,
  kCharNameProfane = 94,
  kCharNameReserved = 95,
  kCharNameInvalidApostrophe = 96,
  kCharNameMultipleApostrophes = 97,
  kCharNameThreeConsecutive = 98,
  kCharNameInvalidSpace = 99,
  kCharNameConsecutiveSpaces = 100,
  kCharNameRussianConsecutiveSilentCharacters = 101,
  kCharNameRussianSilentCharacterAtBeginningOrEnd = 102,
  kCharNameDeclensionDoesntMatchBaseName = 103,
};

enum class WowLocale : std::int32_t {
  kEnUS = 0,
  kKoKR = 1,
  kFrFR = 2,
  kDeDE = 3,
  kZhCN = 4,
  kZhTW = 5,
  kEsES = 6,
  kEnGB = 7,
  kRuRU = 8,
};

struct DisconnectDialogInfo {
  bool has_pending_result{false};
  ClientOperation current_op{ClientOperation::kNone};
  bool operation_success{true};
  std::int32_t expected_response{0};
  std::string message;
};

class ClientServices {
public:
  struct BillingPlanInfo {
    std::int32_t plan{0};
    bool is_game_room{false};
    bool is_paid_time{false};
  };

  static ClientServices &Instance();

  static void Initialize();

  [[nodiscard]] LoginConnectionType GetLoginConnectionType() const;

  [[nodiscard]] static WowClientConnection *GetConnectionObject();
  static void SetConnectionObject(WowClientConnection *conn);

  [[nodiscard]] static bool IsActiveConnectionObject(const void *connection);

  void BeginAuthenticateOperation(const char *account_name, const char *password);

  void Login(const std::string &account_name, const std::string &password);

  void LoginCharacter(std::uint64_t guid);

  void CreateCharacter(const std::string &char_data);

  void DeleteCharacter(std::uint64_t guid);

  void GetCharacters();

  void GetRealmList();

  void ConnectToRealm();

  void HandleEnterWorldInit();

  void CompleteCharacterLoginTransition(bool world_session_ready);

  void HandleDisconnectWithCleanup();

  void DisconnectAndCleanup();

  bool RequestLogout();
  bool RequestQuit();
  bool RequestLogoutCancel();
  bool ForceLogout();

  void OnLogoutResponse(std::uint32_t result, std::uint8_t instant_flag);

  void HandleLogoutComplete();
  void HandleLogoutCancelAck();

  void FullLogout();

  static void LogConnectionStatus(ClientOperation op,
                                  ConnectionResponse status,
                                  bool initiating,
                                  bool result_success);

  [[nodiscard]] static const char *GetResultString(std::int32_t result_code);

  [[nodiscard]] DisconnectDialogInfo GetDisconnectDialogInfo() const;

  [[nodiscard]] bool IsBNLogin() const;
  [[nodiscard]] bool IsWorldSessionReady() const { return world_session_ready_; }

  [[nodiscard]] bool IsLoginConnectionTrialAccount() const;

  void SetLoginConnectionAccountFlags(std::uint64_t account_flags);
  void SetLoginConnectionTrialAccount(bool is_trial_account);

  [[nodiscard]] bool BypassesRealmCategoryLocaleValidation() const;

  [[nodiscard]] bool BypassesTournamentRealmCategoryValidation() const;
  void SetRealmCategoryValidationBypassFlagsForTesting(bool locale_validation_bypass,
                                                       bool tournament_validation_bypass);

  [[nodiscard]] bool HasLoginConnection() const;
  [[nodiscard]] bool IsWorldConnected() const;
  [[nodiscard]] bool HasPendingLogoutRequest() const;
  [[nodiscard]] ClientOperation GetCurrentOperation() const;
  [[nodiscard]] bool IsOperationComplete() const;
  [[nodiscard]] bool IsOperationSuccess() const;
  [[nodiscard]] std::int32_t GetExpectedResponseCode() const;
  [[nodiscard]] const std::string &GetAccountName() const;
  [[nodiscard]] openwow::game::BattlenetLogin *GetBattlenetLogin();
  [[nodiscard]] const openwow::game::BattlenetLogin *GetBattlenetLogin() const;
  [[nodiscard]] bool HasBattleNetRidTransport() const;
  void SetAccountName(const std::string &account_name);
  [[nodiscard]] WowLocale GetCurrentLocale() const;
  void SetCurrentLocale(WowLocale locale);
  void SetRegionId(std::int32_t region);
  [[nodiscard]] std::int32_t GetRegionId() const;
  void SetBNetLoginRequired(bool required);
  [[nodiscard]] bool IsBNetLoginRequired() const;
  void SetExpansionLevel(std::uint8_t level);
  [[nodiscard]] std::uint8_t GetExpansionLevel() const;
  void SetSelectedRealmAddress(std::string address);
  [[nodiscard]] const std::string &GetSelectedRealmAddress() const;
  void SetSelectedRealmAuthSessionSeedWords(std::uint32_t seed0,
                                            std::uint32_t seed1,
                                            std::uint32_t seed2);
  [[nodiscard]] const std::array<std::uint32_t, 3> &GetSelectedRealmAuthSessionSeedWords() const;
  void SetSelectedRealmScriptMetadata(SelectedRealmScriptMetadata metadata);
  void ClearSelectedRealmScriptMetadata();
  [[nodiscard]] std::optional<SelectedRealmScriptMetadata> GetSelectedRealmScriptMetadata() const;
  [[nodiscard]] BillingPlanInfo GetBillingPlan() const;
  [[nodiscard]] std::uint32_t GetBillingTimeRemaining() const;
  [[nodiscard]] std::uint32_t GetBillingTimeRested() const;
  [[nodiscard]] std::uint8_t GetBillingFlags() const;
  void SetWorldAccountBilling(std::uint32_t time_remaining,
                              std::uint8_t flags,
                              std::uint32_t rested_time);
  void SetPendingPatchDownloadInfo(PendingPatchDownloadInfo info);
  [[nodiscard]] std::optional<PendingPatchDownloadInfo>
  GetPendingPatchDownloadInfo() const;
  void ClearPendingPatchDownloadInfo();
  void SetLoginConnectionForTesting(LoginConnectionType type, bool has_connection);

  void ResetConnectOperationState();

  void Disconnect();
  void Reset();

  [[nodiscard]] static std::int32_t GetTimezoneBias();

  [[nodiscard]] static constexpr std::int32_t GetArchitectureId() {
    return 0x577ACE;
  }

  [[nodiscard]] std::string ApplyLocaleSuffix(const std::string &account, WowLocale locale,
                                              std::int32_t region_id, bool bnet_required) const;

  void OnAuthResponse(ConnectionResponse result);

  void HandleAuthResult(ConnectionResponse result);
  void HandleCharCreateResult(std::uint8_t result);
  void HandleCharDeleteResult(std::uint8_t result);

  void HandleLoginResult(std::uint8_t reason);

  void CompletePendingOperation(ConnectionResponse response);

  using CleanupCallback = std::function<void()>;
  void SetCleanupCallback(CleanupCallback cb);

  using LoginFileTransferResponseSender =
      std::function<bool(bool transfer_needed, std::uint64_t resume_offset)>;
  void SetLoginFileTransferResponseSender(LoginFileTransferResponseSender sender);

  void SendLoginFileTransferResponse(bool transfer_needed,
                                     std::uint64_t resume_offset);

  void CancelLoginFileTransfer();

  void SetFileTransferActiveOnLoginNet();

  [[nodiscard]] bool IsFileTransferActiveOnLoginNet() const;
  void ClearFileTransferActiveOnLoginNet();

private:
  ClientServices() = default;

  [[nodiscard]] bool SendLogoutPacket(bool shutdown_after_logout, bool force_logout);
  void InvokeAndClearCleanup();
  void PublishLogoutCancellationIfPending();
  void ResetLogoutRequestState();
  void SetOperationState(ClientOperation op, std::int32_t expected_response);
  void SyncLoginTransport();

  LoginConnectionType login_type_{LoginConnectionType::kGrunt};
  bool is_world_connected_{false};
  bool is_character_login_pending_{false};
  bool operation_complete_{true};
  bool operation_success_{true};
  ClientOperation current_op_{ClientOperation::kNone};
  std::int32_t expected_response_{0};
  bool world_session_ready_{false};
  CleanupCallback cleanup_callback_;
  LoginFileTransferResponseSender login_file_transfer_response_sender_;
  bool shutdown_after_logout_{false};
  bool logout_request_active_{false};
  bool some_flag_{false};

  std::uint32_t billing_time_remaining_{0};
  std::uint32_t billing_time_rested_{0};
  std::uint8_t billing_flags_{0};

  std::string account_name_;
  WowLocale locale_{WowLocale::kEnUS};
  std::int32_t region_id_{0};
  bool bnet_login_required_{false};

  std::uint8_t expansion_level_{0};
  bool has_connection_{false};

  std::uint64_t login_account_flags_{0};
  bool initialized_{false};
  std::unique_ptr<openwow::game::BattlenetLogin> battlenet_login_;
  std::optional<PendingPatchDownloadInfo> pending_patch_download_info_;
  std::string selected_realm_address_;
  std::array<std::uint32_t, 3> selected_realm_auth_session_seed_words_{};
  std::optional<SelectedRealmScriptMetadata> selected_realm_script_metadata_;

  static inline WowClientConnection *connection_object_{nullptr};

  bool file_transfer_active_on_login_net_{false};
};

const char *GetRealmName();

std::uint8_t GetClientExpansionLevel();

using OpcodeHandlerFn_t = int (*)(std::uintptr_t context, int opcode,
                                  int connection_id, void *data_store);

int ClientServices__RegisterOpcodeHandlerWrapper(int opcode,
                                                 OpcodeHandlerFn_t handler,
                                                 std::uintptr_t context);

int ClientServices__UnregisterOpcodeHandler(int opcode);

}

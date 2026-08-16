#pragma once

#include <array>
#include <cstddef>
#include <deque>
#include <functional>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace openwow::net {
class WowConnection;
}

namespace openwow::game {

struct LoginEventSink;

struct GameAccountEntry {
  uint32_t id = 0;
  char name[256] = {};
};

struct PatchInstruction {
  uint32_t program;
  uint32_t platform;
  uint32_t version;
};

inline constexpr std::size_t kWoWSMaxComponents      = 63;
inline constexpr std::size_t kWoWSRandomBufferBytes   = 1024;
inline constexpr std::size_t kWoWSChallengeRandomSize = 32;

struct WoWSComponentEntry {
  std::uint32_t program  = 0;
  std::uint32_t platform = 0;
  std::uint32_t build    = 0;
};
static_assert(sizeof(WoWSComponentEntry) == 12);

struct WoWSDispatcherPayload {

  std::uint32_t random_length = 0;
  std::array<std::uint8_t, kWoWSRandomBufferBytes> random_bytes{};
  std::uint32_t program         = 0;
  std::uint32_t platform        = 0;
  std::uint32_t country         = 0;
  std::uint32_t language        = 0;
  std::uint32_t component_count = 0;
  std::array<WoWSComponentEntry, kWoWSMaxComponents> components{};
};
static_assert(sizeof(WoWSDispatcherPayload) == 1804,
              "Must match IDA stack frame: 0x70C bytes");

enum class BattlenetDispatchValueType : std::uint8_t {
  kString,
  kUInt8,
  kUInt32,
  kBytes,
};

struct BattlenetDispatchValue {
  BattlenetDispatchValueType type{BattlenetDispatchValueType::kBytes};
  std::string string_value;
  std::uint32_t integer_value{0};
  std::vector<std::uint8_t> bytes_value;

  [[nodiscard]] static BattlenetDispatchValue String(std::string value);
  [[nodiscard]] static BattlenetDispatchValue UInt8(std::uint8_t value);
  [[nodiscard]] static BattlenetDispatchValue UInt32(std::uint32_t value);
  [[nodiscard]] static BattlenetDispatchValue Bytes(
      const void *data, std::size_t size);
};

struct BattlenetDispatchEvent {
  std::uint32_t dispatcher_route{6};
  std::uint32_t event_type{0};
  std::uint32_t module_type{0};
  std::uint32_t request_id{0};
  std::string command;
  std::vector<BattlenetDispatchValue> values;
};

class BattlenetDispatcherBackend {
 public:
  virtual ~BattlenetDispatcherBackend() = default;
  [[nodiscard]] virtual bool Create(const WoWSDispatcherPayload &payload) = 0;
  virtual void Shutdown() = 0;
  [[nodiscard]] virtual bool Send(const BattlenetDispatchEvent &event) = 0;
  virtual void OnConnected(openwow::net::WowConnection *connection,
                           const void *address) = 0;
  virtual void OnDisconnected(openwow::net::WowConnection *connection) = 0;
  virtual void OnConnectionClosed(openwow::net::WowConnection *connection) = 0;
  virtual void OnData(openwow::net::WowConnection *connection,
                      const void *data, std::size_t size) = 0;
  virtual void Tick(std::uint32_t now) = 0;
};

enum class BattlenetGlueEvent : std::uint8_t {
  kGameAccountsUpdated,
  kSurveyRequested,
};

struct PendingGameAccountSelection {
  uint32_t id = 0;
  std::string name;
};

struct RealmRecommendedEntry {
  std::uint32_t category_id = 0;
  std::uint32_t sort_key1   = 0;
  std::uint32_t sort_key2   = 0;
  std::uint32_t recommended = 0;
};
static_assert(sizeof(RealmRecommendedEntry) == 16,
              "Must match IDA 16-byte stride in sub_8C9310 fill loop");

struct BnRealmEntry {
  std::uint8_t  type              = 0;
  std::uint8_t  flags             = 0;
  char          name[256]         = {};
  char          address[32]       = {};
  float         population        = 0.0f;
  std::uint8_t  recommended       = 0;
  std::uint8_t  timezone          = 0;
  std::uint32_t category_id       = 0;
  std::uint32_t sort_key1         = 0;
  std::uint32_t sort_key2         = 0;
  std::uint32_t num_characters    = 0;
  std::uint8_t  locked            = 0;
  std::uint8_t  version_major     = 0;
  std::uint8_t  version_minor     = 0;
  std::uint8_t  version_patch     = 0;
  std::uint16_t version_build     = 0;
};

inline constexpr std::size_t kBNetEventBufDwords = 195581;
inline constexpr std::size_t kBNetEventBufBytes  = kBNetEventBufDwords * sizeof(std::uint32_t);
inline constexpr std::size_t kBNetVariantSlotCount = 4;
inline constexpr std::size_t kBNetVariantSlotDwords = 260;
inline constexpr std::size_t kBNetVariantSlotDataBytes = 0x40C;
inline constexpr std::size_t kBNetCommandDescriptorBytes = 0x44;

struct BNetVariantSlot {
  std::int32_t type = -1;
  std::array<std::uint8_t, kBNetVariantSlotDataBytes> data{};
};
static_assert(sizeof(BNetVariantSlot) == kBNetVariantSlotDwords * sizeof(std::uint32_t),
              "Must match IDA variant slot stride: 260 DWORDs");

struct BNetVariantSlotArray {
  std::uint32_t count = 0;
  std::array<BNetVariantSlot, kBNetVariantSlotCount> slots{};
};

struct BNetGameAccountModule {
  std::uint32_t module_type = 2;
  std::uint32_t request_id = 0;
  std::uint32_t reserved_dword = 0;
  std::uint8_t  reserved_flag = 0;
  std::array<std::uint8_t, 3> pad_to_dword_4_{};

  std::array<std::uint8_t, kBNetCommandDescriptorBytes - 8> command_descriptor_tail{};
  BNetVariantSlotArray variant_slots;

  void Init();
};

struct BNetEventBufHeader {
  std::uint32_t event_type = 0;
  BNetGameAccountModule game_account_module;

  void Init();
};

void BNetEventBuf_InitVariantSlots(BNetVariantSlotArray &slots);

void BNetEventBuf_InitGameAccountModule(BNetGameAccountModule &mod);

void BNetEventBuf_InitAuthHeader(BNetEventBufHeader &header);

std::uint8_t* BNetEventField_InitTag(std::int32_t* tag_ptr, std::int32_t tag);

void BNetEventField_CopyBoundedString(std::uint8_t* data_area,
                                       const char* src,
                                       std::size_t max_chars);

inline constexpr std::size_t kBNetFieldMaxChars320 = 320;

inline constexpr std::size_t kBNetFieldMaxChars255 = 255;

inline constexpr std::size_t kBNetFieldMaxChars31  = 31;

inline constexpr std::uint32_t kBNetLoginRedirectEventType = 12;
inline constexpr std::size_t   kBNetLoginRedirectDwords    = 11;
inline constexpr std::size_t   kBNetLoginRedirectBytes     =
    kBNetLoginRedirectDwords * sizeof(std::uint32_t);

struct BNetLoginRedirectEventBuf {
  std::uint32_t event_type = kBNetLoginRedirectEventType;
  std::int32_t  sentinel   = -1;
  std::array<std::uint32_t, 9> reserved{};

  void Init();
};
static_assert(sizeof(BNetLoginRedirectEventBuf) == kBNetLoginRedirectBytes,
              "Must match IDA: 11 DWORDs = 44 bytes");

std::int32_t *BNetEventBuf_InitLoginRedirectEvent(
    BNetLoginRedirectEventBuf &buf);

inline constexpr std::size_t kBattlenetPatchInstructionSlotBytes = 255;

struct PatchDownloadManifestEntry {
  std::string url;
  std::string destination;
  std::string detail_2;
  std::string detail_3;
};

struct PatchDownloadManifestPlan {
  bool uses_redirect_monolithic = false;
  std::vector<PatchDownloadManifestEntry> downloads;
};

struct BnEventNode {
  int event_type = 0;
  std::vector<std::uint8_t> payload;
};

inline constexpr int kBnEventQueueCount = 2;

class BattlenetLogin;

namespace detail {

struct BattlenetDescriptorMessageBinding {
  std::uint32_t schema_id = 0;
};

struct BattlenetDescriptorMessageCursor {
  BattlenetDescriptorMessageBinding *binding = nullptr;
  void *payload = nullptr;
};

class BattlenetDescriptorMessageEnvelope {
public:
  BattlenetDescriptorMessageEnvelope(std::uint32_t schema_id, void *payload) noexcept;

  [[nodiscard]] BattlenetDescriptorMessageCursor *GetEmbeddedVariant() noexcept;
  [[nodiscard]] const BattlenetDescriptorMessageCursor *GetEmbeddedVariant() const noexcept;
  [[nodiscard]] BattlenetDescriptorMessageBinding *GetEmbeddedBinding() noexcept;
  [[nodiscard]] const BattlenetDescriptorMessageBinding *GetEmbeddedBinding() const noexcept;

private:
  BattlenetDescriptorMessageBinding binding_{};
  BattlenetDescriptorMessageCursor cursor_{};
};

struct BattlenetRecoveredResponsePayloadStorage {
  std::int32_t root_tag = -1;
  std::array<std::uint8_t, 0x27124> payload{};
};

static_assert(sizeof(BattlenetRecoveredResponsePayloadStorage) == 0x27128);

class BattlenetRecoveredResponseEnvelope {
public:
  static constexpr std::uint32_t kSchemaId = 1672u;

  BattlenetRecoveredResponseEnvelope() noexcept;

  [[nodiscard]] BattlenetDescriptorMessageCursor *GetEmbeddedVariant() noexcept;
  [[nodiscard]] const BattlenetDescriptorMessageCursor *GetEmbeddedVariant() const noexcept;
  [[nodiscard]] BattlenetDescriptorMessageBinding *GetEmbeddedBinding() noexcept;
  [[nodiscard]] const BattlenetDescriptorMessageBinding *GetEmbeddedBinding() const noexcept;
  [[nodiscard]] BattlenetRecoveredResponsePayloadStorage &payload_storage() noexcept;
  [[nodiscard]] const BattlenetRecoveredResponsePayloadStorage &payload_storage() const noexcept;

private:
  BattlenetRecoveredResponsePayloadStorage payload_storage_{};
  BattlenetDescriptorMessageEnvelope envelope_;
};

class BattlenetLoginTransportService {
public:
  BattlenetLoginTransportService() = default;
  BattlenetLoginTransportService(const BattlenetLoginTransportService &) = delete;
  BattlenetLoginTransportService &operator=(const BattlenetLoginTransportService &) = delete;
  BattlenetLoginTransportService(BattlenetLoginTransportService &&) = delete;
  BattlenetLoginTransportService &operator=(BattlenetLoginTransportService &&) = delete;
  ~BattlenetLoginTransportService();

  [[nodiscard]] std::int32_t request_cookie() const noexcept {
    return request_cookie_;
  }

  [[nodiscard]] void *pending_state() const noexcept {
    return pending_state_;
  }

  void SetPendingState(void *state) noexcept {
    pending_state_ = state;
  }

  [[nodiscard]] BattlenetLogin *owner() const noexcept {
    return owner_;
  }

  void SetOwner(BattlenetLogin *owner) noexcept {
    owner_ = owner;
  }

  [[nodiscard]] openwow::net::WowConnection *connection() const noexcept {
    return connection_;
  }

  void SetConnection(openwow::net::WowConnection *connection) noexcept {
    connection_ = connection;
  }

  void OnConnected(const void *connection_data, std::size_t data_size);

  void OnDisconnected();

  void OnConnectionClosed();

  void OnDataReceived(const void *data, std::size_t size);

  int SendData(std::uint32_t session_id, uint8_t *buf, std::size_t len);

  [[nodiscard]] std::uint32_t connection_session_id() const noexcept {
    return connection_session_id_;
  }

  void SetConnectionSessionId(std::uint32_t id) noexcept {
    connection_session_id_ = id;
  }

  [[nodiscard]] const std::array<std::uint8_t, 4> &server_ip() const noexcept {
    return server_ip_;
  }
  [[nodiscard]] std::uint16_t server_port_raw() const noexcept {
    return server_port_raw_;
  }
  [[nodiscard]] std::uint16_t server_port() const noexcept {
    return server_port_;
  }

private:
  std::int32_t request_cookie_ = -1;
  void *pending_state_ = nullptr;
  BattlenetLogin *owner_ = nullptr;
  openwow::net::WowConnection *connection_ = nullptr;

  std::uint32_t connection_session_id_ = 0;

  std::array<std::uint8_t, 4> server_ip_{};

  std::uint16_t server_port_raw_ = 0;

  std::uint16_t server_port_ = 0;
};

}

class BattlenetLogin {
public:
  using StatusCallback =
      std::function<void(std::uint32_t state,
                         std::uint32_t result,
                         std::int32_t error_code,
                         const std::string &state_name,
                         const std::string &result_name,
                         std::int32_t extra)>;

  using RealmListPacketCallback =
      std::function<void(const std::uint8_t* data, std::uint32_t size)>;
  using GlueEventCallback = std::function<void(BattlenetGlueEvent event)>;

  ~BattlenetLogin();

  bool OnConnected(const void *address);

  bool OnDisconnected();

  static void *Alloc(int size);
  static void Free(void *block);

  [[noreturn]] static void AssertAndCrash(const char *message, const char *file, uint32_t line);

  static int ParseComponentVersion(const char *component_name, uint32_t *out_version,
                                   const void *xml_data, size_t xml_size);

  static int GetWowDataVersion();

  static int GetLocaleComponentVersion(const char *locale);

  void ParseMatrixCardChallenge(const char *params_string, const char *cell_ids_string);

  bool GetMatrixCardInfo(std::uint32_t &out_num_rows,
                         std::uint32_t &out_num_cols,
                         std::uint32_t &out_digit_count,
                         std::uint32_t &out_challenge_size,
                         bool &out_has_challenge,
                         std::uint32_t &out_cell_count) const;

  bool GetMatrixCardCellCoordinate(std::uint32_t index,
                                   std::uint32_t &out_row,
                                   std::uint32_t &out_col) const;

  void InitiateAuth(const char *redirect_url);

  int Connect(const uint8_t *address, int flags);

  int CreateDispatcher();

  [[nodiscard]] const std::optional<WoWSDispatcherPayload>&
  GetLastDispatcherPayloadForTests() const noexcept;

  uint32_t GetNumGameAccounts() const;

  const char *GetGameAccountName(uint32_t index) const;

  uint32_t GetGameAccountId(uint32_t index) const;

  [[nodiscard]] const char *GetPatchInstructionString(uint32_t index) const;

  void ResizeGameAccountArray(uint32_t new_capacity);

  GameAccountEntry *AppendGameAccount();

  void ClearGameAccounts();

  void SetGameAccounts(std::vector<GameAccountEntry> accounts);
  void ClearPatchInstructionStrings();
  void SetPatchInstructionStrings(std::vector<std::string> instructions);
  [[nodiscard]] PatchDownloadManifestPlan BuildPatchDownloadPlan() const;
  void SetDispatcherAvailable(bool available);
  [[nodiscard]] bool IsDispatcherAvailable() const;
  void SetDispatcherBackend(BattlenetDispatcherBackend *backend);
  void SetDispatcherRequestId(std::uint32_t request_id);
  [[nodiscard]] std::vector<BattlenetDispatchEvent> TakeDispatchedEvents();
  void SetStatusCallback(StatusCallback callback);
  void SetRealmListPacketCallback(RealmListPacketCallback callback);
  void SetGlueEventCallback(GlueEventCallback callback);

  void SetAccountName(std::string name);
  [[nodiscard]] const std::string &account_name() const;
  [[nodiscard]] std::uint32_t login_state() const;
  [[nodiscard]] std::uint32_t login_result() const;
  [[nodiscard]] std::uint32_t translated_auth_result_code() const;
  [[nodiscard]] bool HasSunkenConnectFailure() const;

  void SetLoginStateForTesting(std::uint32_t state);

  void SetLoginByte5FlagForTesting(bool flag);

  void RequestGameLogin();

  [[nodiscard]] bool game_login_requested() const;
  void SetRidFeatureBlockFlag(bool blocked);
  [[nodiscard]] bool HasRidFeatureBlockFlag() const;

  bool SetGameAccount(uint32_t index);

  int SubmitAuthenticator(const uint8_t *code);

  [[nodiscard]] bool has_authenticator_submission() const;
  [[nodiscard]] std::array<std::uint8_t, 10> authenticator_digits() const;
  void ClearAuthenticatorSubmission();

  void SerializeRealmList();

  void SubmitPassword();

  void SetPendingPassword(std::string password);
  [[nodiscard]] bool HasPendingPassword() const;

  static void *GetServiceNotificationField(void *repeated_field, uint32_t index);

  static void *GetServiceResponse(void *table, uint32_t index);

  static void *ValidateServiceState(void *service);

  void TranslateAuthResult(std::uint32_t auth_result_code);

  void HandleSunkenConnectFailure();

  static void *ConstructService(void *self);

  static void DestroyDispatcher(void *service, void **dispatcher);

  void QueueEvent(int event_type);
  void QueueEvent(int event_type, const void *data, std::size_t data_size);

  void HandleRealmUpdate(int category_id, int sort_key1, int sort_key2, std::intptr_t server_data,
                         uint8_t flags, bool remove);

  [[nodiscard]] std::uint8_t LookupRealmRecommended(std::uint32_t category_id,
                                                     std::uint32_t sort_key1,
                                                     std::uint32_t sort_key2) const;

  void SetRealmRecommendedTable(std::vector<RealmRecommendedEntry> entries);

  bool RemoveRealmEntry(std::uint32_t category_id, std::uint32_t sort_key1,
                        std::uint32_t sort_key2);

  void Init();

  void Cleanup();

  void StartLogin(int event_sink);

  void TickAndReconnect();

  void ProcessEventQueue(int queue_index);

  [[noreturn]] void GruntLoginBuildPacket();

  void RequestVirtualKeypadPIN(unsigned int digit_count,
                               const std::uint8_t *digits);

  void CommitMatrixCard();

  void EnterMatrixCard(std::uint8_t digit);

  void RevertMatrixCard();

  void FinalizeMatrixCard();

  [[nodiscard]] bool HasPendingMatrixCardEntry() const;
  [[nodiscard]] bool GetPendingMatrixCardCoordinates(std::uint32_t &out_first,
                                                     std::uint32_t &out_second) const;
  [[nodiscard]] bool CommitPendingMatrixCardEntry();

  void SetMatrixChallengeActive(bool active);
  [[nodiscard]] bool IsMatrixChallengeActive() const;

  void ParseTokenChallenge(bool active, std::uint8_t token_type);

  void SubmitToken(const char *token_text);

  void SetTokenChallengeActive(bool active);
  [[nodiscard]] bool IsTokenChallengeActive() const;
  void SetTokenChallengeType(std::uint8_t type);
  [[nodiscard]] std::uint8_t token_challenge_type() const;

  static int GetPresenceValueSize();
  static int GetPresenceFieldCount(const void *self);

private:
  [[nodiscard]] bool DispatchEvent(BattlenetDispatchEvent event);
  [[nodiscard]] std::uint32_t CurrentRequestId() const;
  void DispatchStatus(std::uint32_t state,
                      std::uint32_t result,
                      std::int32_t error_code = 0,
                      std::int32_t extra = 0);

  uint32_t state_ = 0;
  std::vector<GameAccountEntry> game_accounts_;
  std::vector<std::array<char, kBattlenetPatchInstructionSlotBytes>>
      patch_instruction_strings_;

  std::string account_name_;

  std::optional<PendingGameAccountSelection> pending_game_account_selection_;
  bool game_account_selection_pending_ = false;
  std::uint32_t dispatcher_request_id_ = 0;
  bool dispatcher_available_ = true;
  BattlenetDispatcherBackend *dispatcher_backend_ = nullptr;
  mutable std::mutex dispatched_events_mutex_;
  std::vector<BattlenetDispatchEvent> dispatched_events_;
  std::optional<WoWSDispatcherPayload> last_dispatcher_payload_;
  std::uint32_t rid_feature_gate_flags_ = 0;
  StatusCallback status_callback_;
  RealmListPacketCallback realm_list_packet_callback_;
  GlueEventCallback glue_event_callback_;
  std::uint32_t login_state_ = 0;
  std::uint32_t login_result_ = 0;
  std::uint32_t translated_auth_result_code_ = 0;
  bool login_byte_4_flag_ = false;
  bool login_byte_5_flag_ = false;
  bool game_login_requested_ = false;

  bool sunken_connect_failure_latched_ = false;
  std::uint32_t connect_tick_ = 0;

  std::array<std::uint8_t, 6> connect_addr_raw_{};

  std::unique_ptr<detail::BattlenetLoginTransportService> transport_service_;

  bool authenticator_submitted_ = false;

  std::array<std::uint8_t, 10> authenticator_digits_{};

  std::string pending_password_;

  bool matrix_challenge_active_ = false;

  std::uint8_t matrix_num_rows_ = 0;

  std::uint8_t matrix_num_cols_ = 0;

  std::uint8_t matrix_digit_count_ = 0;

  std::uint8_t matrix_challenge_size_ = 0;

  std::uint8_t matrix_cell_count_ = 0;

  std::uint8_t matrix_entries_remaining_ = 0;

  bool matrix_has_challenge_ = false;

  std::vector<std::uint32_t> matrix_cell_ids_;

  bool token_challenge_active_ = false;

  std::uint8_t token_challenge_type_ = 0;

  std::vector<RealmRecommendedEntry> realm_recommended_table_;

  mutable std::mutex realm_list_mutex_;
  std::vector<BnRealmEntry> realm_entries_;

  bool reconnect_pending_ = false;

  std::uint32_t reconnect_tick_ = 0;

  bool survey_requested_ = false;
  std::uint32_t survey_request_tick_ = 0;

  mutable std::mutex event_queue_mutex_;
  std::array<std::deque<BnEventNode>, kBnEventQueueCount> event_queues_;
  int current_event_queue_index_ = 0;

};

void RealmList_SortRealms(void *login_object);

}

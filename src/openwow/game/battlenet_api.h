
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace openwow::game {

enum class BNetConnectionState : std::int32_t {
  kDisconnected = 0,
  kConnecting = 1,
  kAuthenticating = 2,
  kConnectedIdle = 3,
  kLoggedIn = 4,
  kDisconnecting = 5,
  kConnectedReady = 6,
  kConnectedActive = 7,
};

enum class BNetVariantType : std::int32_t {
  kNone = 0,
  kBool = 1,
  kInt32 = 2,
  kPresenceId = 3,
  kFloat64 = 4,
  kInlineString = 5,
  kStringPtr = 6,
  kRefCounted = 7,
  kCallback = 8,

  kPresenceFlag = 11,

  kPresenceText = 13,
};

struct BNetVariant {
  BNetVariantType type{BNetVariantType::kNone};
  bool bool_val{false};
  std::int32_t int_val{0};
  double float_val{0.0};
  const char *str_ptr{nullptr};
  std::int32_t extra{0};
  std::string owned_string;

  BNetVariant() = default;
  explicit BNetVariant(bool v) : type(BNetVariantType::kBool), bool_val(v) {}
  explicit BNetVariant(std::int32_t v) : type(BNetVariantType::kInt32), int_val(v) {}

  static BNetVariant PresenceFlag(bool value) {
    BNetVariant result;
    result.type = BNetVariantType::kPresenceFlag;
    result.bool_val = value;
    return result;
  }
  BNetVariant(const BNetVariant &other) { AssignFrom(other); }
  BNetVariant(BNetVariant &&other) noexcept { AssignFrom(std::move(other)); }

  BNetVariant &operator=(const BNetVariant &other) {
    if (this != &other) {
      AssignFrom(other);
    }
    return *this;
  }

  BNetVariant &operator=(BNetVariant &&other) noexcept {
    if (this != &other) {
      AssignFrom(std::move(other));
    }
    return *this;
  }

  static BNetVariant PresenceId(std::int32_t id) {
    BNetVariant v;
    v.type = BNetVariantType::kPresenceId;
    v.int_val = id;
    return v;
  }

  static BNetVariant StringPtr(const char *s) {
    BNetVariant v;
    v.type = BNetVariantType::kStringPtr;
    if (s != nullptr) {
      v.owned_string = s;
      v.str_ptr = v.owned_string.c_str();
    }
    return v;
  }

  static BNetVariant InlineString(std::string_view s) {
    BNetVariant v;
    v.SetStringPayload(BNetVariantType::kInlineString, s);
    return v;
  }

  static BNetVariant PresenceText(std::string_view text) {
    BNetVariant v;
    v.SetStringPayload(BNetVariantType::kPresenceText, text);
    return v;
  }

  [[nodiscard]] std::int32_t ToInt() const;

  [[nodiscard]] std::int32_t ToSignedInt() const;

private:
  [[nodiscard]] bool IsStringLike() const {
    return type == BNetVariantType::kStringPtr || type == BNetVariantType::kInlineString ||
           type == BNetVariantType::kPresenceText;
  }

  void SetStringPayload(BNetVariantType string_type, std::string_view value) {
    type = string_type;
    bool_val = false;
    int_val = 0;
    float_val = 0.0;
    extra = 0;
    owned_string.assign(value);
    str_ptr = owned_string.c_str();
  }

  void RebindStringPointer(const char *fallback_ptr) {
    if (!IsStringLike()) {
      str_ptr = nullptr;
      owned_string.clear();
      return;
    }

    if (fallback_ptr != nullptr && owned_string.empty()) {
      owned_string = fallback_ptr;
    }

    str_ptr = fallback_ptr == nullptr ? nullptr : owned_string.c_str();
  }

  void AssignFrom(const BNetVariant &other) {
    type = other.type;
    bool_val = other.bool_val;
    int_val = other.int_val;
    float_val = other.float_val;
    extra = other.extra;
    owned_string = other.owned_string;
    RebindStringPointer(other.str_ptr);
  }

  void AssignFrom(BNetVariant &&other) {
    type = other.type;
    bool_val = other.bool_val;
    int_val = other.int_val;
    float_val = other.float_val;
    extra = other.extra;
    owned_string = std::move(other.owned_string);
    RebindStringPointer(other.str_ptr);
    other.str_ptr = nullptr;
  }
};

static constexpr std::size_t kBNetFriendMaxCount = 100;

struct BNetFriendInfo {
  std::int32_t presence_id{0};
  std::int32_t game_account{0};
  std::int32_t display_name_length{0};
  std::int32_t display_name_bytes{0};
  char display_name[289]{};
  char display_name_pad_[3]{};
  std::int32_t formatted_name_left_length{0};
  std::int32_t formatted_name_left_bytes{0};
  char formatted_name_left[160]{};
  char formatted_name_left_pad_[4]{};
  std::int32_t formatted_name_right_length{0};
  std::int32_t formatted_name_right_bytes{0};
  char formatted_name_right[160]{};
  char formatted_name_right_pad_[4]{};
  std::int32_t formatted_name_right_base_length{0};
  std::int32_t formatted_name_right_base_bytes{0};
  char formatted_name_right_base[160]{};
  char formatted_name_right_base_pad_[4]{};
};
static_assert(sizeof(BNetFriendInfo) == 824);

static constexpr std::size_t kBNetBlockMaxCount = 201;

struct BNetBlockInfo {
  std::int32_t presence_id{0};
};
static_assert(sizeof(BNetBlockInfo) == 4);

struct BNetBlockList {
  std::int32_t count{0};
  std::array<BNetBlockInfo, kBNetBlockMaxCount> entries{};
};
static_assert(sizeof(BNetBlockList) == 808);

static constexpr std::size_t kBNetConversationChannelCount = 6;
static constexpr std::size_t kBNetConversationMaxMembers = 12;
static constexpr std::int32_t kBNetConversationChannelTypeRegular = 2;

struct BNetConversation {
  std::int32_t channel_type{0};
  std::vector<std::int32_t> member_presence_ids;
};

struct BNetPresenceRecord {
  std::int32_t game_account{0};
  std::string display_name;
  std::string formatted_name_left;
  std::string formatted_name_right;
  std::string formatted_name_right_base;
  std::int32_t toon_name_realm_id{-1};

  bool has_formatted_name{false};
  bool online{false};
};

struct BNetFriendListInitializedPayload {
  std::vector<std::int32_t> presence_ids;
};

struct BNetOnlineStatusChangedPayload {
  std::int32_t presence_id{0};
  bool has_online_state{false};
  bool is_online{false};
};

struct BNetOnlineTimeChangedPayload {
  std::int32_t presence_id{0};
  bool has_online_time{false};
  std::int32_t online_time{0};
};

struct BNetCustomMessageChangedPayload {
  std::int32_t presence_id{0};
  bool has_custom_message{false};
  std::string custom_message;
};

struct BNetChatWhisperPayload {
  std::int32_t presence_id{0};
  std::string text;
};

struct BNetConversationMessagePayload {
  std::uint8_t conversation_id{0};
  std::int32_t presence_id{0};
  std::string text;
};

struct BNetFriendsOfFriendInfoPayload {
  std::uint16_t error_code{0};
  std::int32_t source_presence_id{0};
  std::vector<std::int32_t> presence_ids;
};

struct BNetFriendInvite {
  std::int32_t presence_id{0};
  bool has_message{false};
  std::string message;
  std::int32_t timestamp{0};
};

struct BNetFriendInviteView {
  std::int32_t presence_id{0};
  bool has_name{false};
  std::string formatted_name_left;
  std::string formatted_name_right;
  bool has_message{false};
  std::string message;
  std::int32_t timestamp{0};
};

struct BNetToonInfo {
  std::int32_t toon_presence_id{0};
  std::uint32_t realm_id{0};
  bool is_focused{false};
};

using BNetCurrentToonInfo = BNetToonInfo;

struct BNetFriendLuaInfo {
  std::int32_t presence_id{0};
  std::string first_name;
  std::string last_name;
  bool has_toon{false};
  std::string toon_name;
  std::int32_t toon_presence_id{0};
  std::string realm_id;
  bool is_online{false};
  std::int32_t num_game_accounts{0};
  bool is_afk{false};
  bool is_dnd{false};
  std::optional<std::string> custom_message;
  std::optional<std::string> note_text;
  bool is_friend{false};
  std::int32_t last_online{0};
};

static constexpr std::size_t kBNetFriendOfFriendMaxCount = 105;

struct BNetFriendOfFriendView {
  std::int32_t presence_id{0};
  bool is_mutual{false};
  bool has_name{false};
  std::string formatted_name_left;
  std::string formatted_name_right;
};

enum class BNetPresenceKey : std::int32_t {
  kOnline = 0x10002,
  kOnlineTime = 0x10003,
  kDisplayName = 0x10004,
  kGameAccount = 0x10005,
  kFocus = 0x10009,
  kClient = 0x30001,
  kRealmName = 0x30002,
  kRace = 0x30003,
  kClass = 0x30004,
  kLevel = 0x30005,
  kGuild = 0x30006,
  kZone = 0x1000A,
  kCustomMessage = 0x1000C,
  kLastOnline = 0x1000D,
  kToonName = 0x1000E,
  kAFK = 0x10010,
  kDND = 0x10011,
  kFaction = 0x40001,
};

struct BNetPresenceFormattedName {
  std::string display_name;
  std::string formatted_name_left;
  std::string formatted_name_right;
  std::string formatted_name_right_base;
};

struct BNetPresenceValue {
  enum class Type : std::int32_t {
    kNone = 0,
    kBool = 1,

    kInt32 = 6,

    kPresenceFlag = 11,

    kString = 13,

    kStringPtr = 6,
    kToonName = 17,
    kCustom = 18,
  };

  Type type{Type::kNone};
  std::uint8_t byte_val{0};
  bool bool_val{false};
  std::int32_t int_val{0};
  std::int32_t aux_int{-1};
  std::string str_val;
  BNetPresenceFormattedName formatted_name;

  static BNetPresenceValue Boolean(bool value) {
    return Byte(value ? 1 : 0);
  }

  static BNetPresenceValue Byte(std::uint8_t value) {
    BNetPresenceValue result;
    result.type = Type::kBool;
    result.byte_val = value;
    result.bool_val = value != 0;
    return result;
  }

  static BNetPresenceValue PresenceFlag(std::uint8_t value) {
    BNetPresenceValue result;
    result.type = Type::kPresenceFlag;
    result.byte_val = value;
    result.bool_val = value != 0;
    return result;
  }

  static BNetPresenceValue Int32(std::int32_t value) {
    BNetPresenceValue result;
    result.type = Type::kInt32;
    result.int_val = value;
    return result;
  }

  static BNetPresenceValue String(std::string value) {
    BNetPresenceValue result;
    result.type = Type::kString;
    result.str_val = std::move(value);
    return result;
  }

  static BNetPresenceValue ToonName(std::string value, std::int32_t realm_id = -1) {
    BNetPresenceValue result;
    result.type = Type::kToonName;
    result.str_val = std::move(value);
    result.aux_int = realm_id;
    return result;
  }

  static BNetPresenceValue FormattedName(BNetPresenceFormattedName value) {
    BNetPresenceValue result;
    result.type = Type::kCustom;
    result.formatted_name = std::move(value);
    return result;
  }
};

struct BNetUiEventArg {
  enum class Kind : std::uint8_t {
    kNil = 0,
    kString = 1,
    kNumber = 2,
    kBoolean = 3,
  };

  Kind kind{Kind::kNil};
  std::string string_value;
  double number_value{0.0};
  bool bool_value{false};

  static BNetUiEventArg Nil() {
    return {};
  }

  static BNetUiEventArg String(std::string value) {
    BNetUiEventArg arg;
    arg.kind = Kind::kString;
    arg.string_value = std::move(value);
    return arg;
  }

  static BNetUiEventArg Number(double value) {
    BNetUiEventArg arg;
    arg.kind = Kind::kNumber;
    arg.number_value = value;
    return arg;
  }

  static BNetUiEventArg Boolean(bool value) {
    BNetUiEventArg arg;
    arg.kind = Kind::kBoolean;
    arg.bool_value = value;
    return arg;
  }
};

using BNetUiEventSink =
    std::function<void(const std::string &event_name, const std::vector<BNetUiEventArg> &args)>;

struct BNetChatDisplayRequest {
  std::string message;
  int chat_type{0};
  std::optional<std::string> sender_name;
  std::optional<std::string> flag_tag;
  std::optional<std::array<std::byte, 9>> extra_data;
};

[[nodiscard]] std::array<std::byte, 9>
BuildBNetChatDisplayExtraData(std::uint32_t primary, std::uint32_t secondary,
                              std::uint8_t flags);

using BNetChatDisplayHandler = std::function<void(const BNetChatDisplayRequest &request)>;

using BNetErrorCode = std::uint16_t;
using BNetConversationCreateHandler =
    std::function<BNetErrorCode(const BNetPresenceValue &first_toon_name,
                                const BNetPresenceValue &second_toon_name)>;
using BNetConversationInviteHandler =
    std::function<BNetErrorCode(std::uint8_t conversation_id,
                                const BNetPresenceValue &invitee_toon_name)>;
using BNetConversationLeaveHandler = std::function<BNetErrorCode(std::uint8_t conversation_id)>;
using BNetConversationSendHandler =
    std::function<BNetErrorCode(std::uint8_t conversation_id, std::string_view text)>;
using BNetFriendNoteHandler =
    std::function<BNetErrorCode(std::int32_t presence_id, std::string_view note)>;
using BNetPresenceActionHandler = std::function<BNetErrorCode(std::int32_t presence_id)>;
using BNetCidBlockHandler = std::function<BNetErrorCode(std::int32_t cid)>;
using BNetRidBlockHandler = BNetPresenceActionHandler;
using BNetFriendInviteActionHandler = BNetPresenceActionHandler;
using BNetFriendInviteByEmailHandler =
    std::function<BNetErrorCode(std::string_view email, std::string_view note)>;
using BNetFriendInviteByPresenceIdHandler =
    std::function<BNetErrorCode(std::int32_t presence_id, std::string_view note)>;
using BNetFriendsOfFriendRequestHandler = std::function<BNetErrorCode(std::int32_t presence_id)>;
using BNetReportPlayerHandler = std::function<BNetErrorCode(
    std::int32_t presence_id, std::int32_t report_type, const char *note)>;
using BNetWhisperSendHandler =
    std::function<BNetErrorCode(std::int32_t presence_id, std::string_view text)>;
using BNetPresenceValueSetHandler =
    std::function<BNetVariant(std::int32_t key, const BNetVariant &value)>;

class BattleNetUI;

class BattleNetApi {
public:
  static BattleNetApi &Instance();

  [[nodiscard]] BNetConnectionState connection_state() const {
    return connection_state_;
  }

  void SetConnectionState(BNetConnectionState state) {
    connection_state_ = state;
  }

  void SetDispatcherAvailable(bool available) {
    dispatcher_available_ = available;
  }

  [[nodiscard]] bool IsDispatcherAvailable() const {
    return dispatcher_available_;
  }

  [[nodiscard]] bool IsConnectedState() const;

  [[nodiscard]] std::int32_t GetFriendCount() const {
    return static_cast<std::int32_t>(friends_.size());
  }

  [[nodiscard]] std::int32_t FindFriendIndexByPresenceID(std::int32_t presence_id) const;

  [[nodiscard]] const BNetFriendInfo *FindFriendRecordByPresenceID(
      std::int32_t presence_id) const;

  [[nodiscard]] const BNetFriendInfo *GetFriend(std::int32_t index) const;
  [[nodiscard]] std::int32_t GetOnlineFriendCount() const;

  bool AddFriendToList(std::int32_t presence_id);

  bool SetPresenceValue(std::int32_t key, const BNetVariant &value);
  BNetPresenceValue GetPresenceValue(std::int32_t presence_id, std::int32_t key) const;

  std::int32_t GetPresenceIDForCurrentAccount() const;
  std::int32_t GetPresenceIDForCurrentToon() const;
  void SetCurrentAccountPresenceID(std::int32_t presence_id);
  void SetCurrentToonPresenceID(std::int32_t presence_id);
  void SetRIDEnabled(bool enabled);

  void SetPresenceRecord(std::int32_t presence_id, BNetPresenceRecord record);
  void SetPresenceDisplayName(std::int32_t presence_id, std::string name);
  void SetErrorStringResponse(BNetErrorCode code, std::string message);

  void SetAccountToons(std::int32_t presence_id, std::vector<BNetToonInfo> toon_infos);
  [[nodiscard]] std::optional<BNetToonInfo>
  FindToonInfoByPresenceId(std::int32_t toon_presence_id) const;

  void SetCurrentToonInfo(std::int32_t presence_id, BNetCurrentToonInfo toon_info);

  void SetPresenceValueResponse(std::int32_t presence_id, std::int32_t key,
                                BNetPresenceValue value);

  void SetPresenceFriendship(std::int32_t presence_id, bool is_friend);
  void SetCIDFriendship(std::int32_t presence_id, bool is_friend);
  void SetConversation(std::uint8_t channel, std::int32_t channel_type,
                       std::vector<std::int32_t> member_presence_ids);

  void SetFriendInvites(std::vector<BNetFriendInvite> invites);

  void AppendFriendInviteOrFatal(BNetFriendInvite invite);

  void SetFriendsOfFriendList(std::int32_t source_presence_id,
                              std::vector<std::int32_t> presence_ids);
  void SetBlockList(BNetBlockList block_list);
  void SetSelectedBlockPresenceId(std::int32_t presence_id);
  void SetSelectedToonBlockPresenceId(std::int32_t presence_id);
  void SetSelectedFriendPresenceId(std::int32_t presence_id);
  [[nodiscard]] std::optional<BNetFriendLuaInfo> GetFriendLuaInfo(std::int32_t index) const;
  [[nodiscard]] std::int32_t GetFriendInviteCount() const;
  [[nodiscard]] std::optional<BNetFriendInviteView> GetFriendInviteInfo(std::int32_t index) const;
  [[nodiscard]] std::int32_t GetFriendsOfFriendSourcePresenceId() const;
  [[nodiscard]] std::pair<std::int32_t, std::int32_t> GetFriendsOfFriendCounts() const;
  [[nodiscard]] std::optional<BNetFriendOfFriendView>
  GetFriendOfFriendInfo(std::int32_t filtered_index, bool include_mutual,
                        bool include_non_mutual) const;
  [[nodiscard]] std::int32_t GetSelectedFriendLuaIndex() const;
  [[nodiscard]] bool IsFriendListInitialized() const {
    return friend_list_initialized_;
  }

  void ResetFriendListStateForGlueTransition();

  void ClearUiSocialCaches();

  bool IsFriendPresenceID(std::int32_t presence_id) const;
  bool IsPresenceIDFriend(std::int32_t presence_id) const;
  bool IsPresenceIDSelf(std::int32_t presence_id) const;
  bool IsPresenceIDBlocked(std::int32_t presence_id) const;
  bool IsRIDEnabled() const;

  BNetBlockList GetBlockList() const;
  [[nodiscard]] std::int32_t GetSelectedBlockLuaIndex() const;
  [[nodiscard]] std::int32_t GetSelectedToonBlockLuaIndex() const;
  BNetErrorCode AddRIDBlock(std::int32_t presence_id);
  BNetErrorCode RemoveRIDBlock(std::int32_t presence_id);

  BNetErrorCode SendChatWhisper(std::int32_t presence_id, const std::string &text);

  BNetErrorCode SendRIDFriendInviteByEmail(const std::string &email, const std::string &note);
  BNetErrorCode SendRIDFriendInviteByPresenceId(std::int32_t presence_id, const std::string &note);

  BNetErrorCode RemoveFriend(std::int32_t presence_id);
  BNetErrorCode AcceptFriendInvite(std::int32_t presence_id);
  BNetErrorCode DeclineFriendInvite(std::int32_t presence_id);
  BNetErrorCode ReportFriendInvite(std::int32_t presence_id);

  BNetErrorCode ReportPlayer(std::int32_t presence_id, std::int32_t report_type, const char *note);

  BNetErrorCode CreateConversation(const BNetPresenceValue &first_toon_name,
                                   const BNetPresenceValue &second_toon_name);

  BNetErrorCode InviteToConversation(std::uint8_t conversation_id,
                                     const BNetPresenceValue &invitee_toon_name);

  BNetErrorCode LeaveConversation(std::uint8_t conversation_id);

  BNetErrorCode SendConversationMessage(std::uint8_t conversation_id, const std::string &text);

  [[nodiscard]] std::int32_t GetNumConversationMembers(std::uint8_t conversation_id) const;
  [[nodiscard]] std::vector<std::int32_t>
  GetConversationMemberList(std::uint8_t conversation_id) const;

  BNetErrorCode SetFriendNote(std::int32_t presence_id, const std::string &note);

  BNetErrorCode SetCustomMessage(const std::string &message);

  std::int32_t RegisterEvent(std::string_view event_name, std::string_view handler_label);
  std::int32_t RegisterPresenceUpdateCallback(std::int32_t presence_id, std::int32_t key,
                                              std::string_view handler_label);
  [[nodiscard]] std::size_t GetRegisteredEventBindingCount() const;
  [[nodiscard]] std::size_t GetRegisteredPresenceUpdateBindingCount() const;
  [[nodiscard]] bool HasRegisteredEventBinding(std::string_view event_name,
                                               std::string_view handler_label) const;
  [[nodiscard]] bool HasRegisteredPresenceUpdateBinding(std::int32_t presence_id, std::int32_t key,
                                                        std::string_view handler_label) const;

  const char *GetErrorString(BNetErrorCode code) const;
  void HandleError(BNetErrorCode code, std::int32_t context);

  void UpdateRecentWhispers(std::int32_t presence_id, std::uint32_t status_flags,
                            bool update_timestamp, std::string_view display_name);
  [[nodiscard]] std::vector<std::string>
  GetRecentWhisperAutoCompleteResults(std::string_view text, std::uint32_t include_flags,
                                      std::uint32_t exclude_flags, std::size_t max_results,
                                      std::size_t cursor_position, bool allow_full_match) const;
  [[nodiscard]] std::optional<std::int32_t>
  GetRecentWhisperPresenceIdForName(std::string_view name) const;

  void ClearRecentWhispers();

  void FireDisconnectedEventAndClearRecentWhispers(bool event_flag);

  [[nodiscard]] bool IsFullyConnected() const;

  [[nodiscard]] std::int32_t GetAccountPresenceId(std::int32_t presence_id) const;

  [[nodiscard]] const char *GetNameForPresenceId(std::int32_t presence_id, bool exact) const;

  [[nodiscard]] const char *GetExactNameForPresenceId(std::int32_t presence_id, bool exact) const;

  [[nodiscard]] const char *GetToonNameForPresenceId(std::int32_t presence_id, bool exact) const;

  [[nodiscard]] std::int32_t GetNumToons(std::int32_t presence_id) const;

  [[nodiscard]] std::int32_t GetToon(std::int32_t presence_id, std::int32_t index) const;

  [[nodiscard]] std::int32_t GetFocusedToon(std::int32_t presence_id) const;

  [[nodiscard]] bool IsCIDFriend(std::int32_t cid) const;

  [[nodiscard]] BNetErrorCode AddCIDBlock(std::int32_t cid);

  [[nodiscard]] BNetErrorCode RemoveCIDBlock(std::int32_t cid);

  [[nodiscard]] bool IsCIDBlock(std::int32_t cid) const;

  [[nodiscard]] std::int32_t GetChatChannelType(std::uint8_t channel) const;

  bool UnregisterPresenceUpdateCallback(std::int32_t handle);

  bool UnregisterEvent(std::int32_t handle);

  void SetAccountNameFormatString(const char *fmt);

  void SetSetting(std::string_view setting_name, const BNetVariant &value, bool persist);
  [[nodiscard]] BNetVariant GetSetting(std::string_view setting_name) const;

  BNetErrorCode RequestFriendsOfFriendInfo(std::int32_t presence_id);
  void SetConversationCreateHandler(BNetConversationCreateHandler handler);
  void SetConversationInviteHandler(BNetConversationInviteHandler handler);
  void SetConversationLeaveHandler(BNetConversationLeaveHandler handler);
  void SetConversationSendHandler(BNetConversationSendHandler handler);
  void SetFriendNoteHandler(BNetFriendNoteHandler handler);
  void SetRemoveFriendHandler(BNetPresenceActionHandler handler);
  void SetAcceptFriendInviteHandler(BNetFriendInviteActionHandler handler);
  void SetDeclineFriendInviteHandler(BNetFriendInviteActionHandler handler);
  void SetReportFriendInviteHandler(BNetFriendInviteActionHandler handler);
  void SetRIDFriendInviteByEmailHandler(BNetFriendInviteByEmailHandler handler);
  void SetRIDFriendInviteByPresenceIdHandler(BNetFriendInviteByPresenceIdHandler handler);
  void SetFriendsOfFriendRequestHandler(BNetFriendsOfFriendRequestHandler handler);
  void SetAddCIDBlockHandler(BNetCidBlockHandler handler);
  void SetRemoveCIDBlockHandler(BNetCidBlockHandler handler);
  void SetAddRIDBlockHandler(BNetRidBlockHandler handler);
  void SetRemoveRIDBlockHandler(BNetRidBlockHandler handler);
  void SetReportPlayerHandler(BNetReportPlayerHandler handler);
  void SetWhisperSendHandler(BNetWhisperSendHandler handler);
  void SetPresenceValueHandler(BNetPresenceValueSetHandler handler);

  [[nodiscard]] const char *GetNameForPresenceID(std::int32_t presence_id) const;

  void UpdateFriendOnlineToons();

  void SetEventSink(BNetUiEventSink sink);
  void SetChatDisplayHandler(BNetChatDisplayHandler handler);
  void DisplayChatMessage(const BNetChatDisplayRequest &request) const;
  void SetUiEventDispatchEnabled(bool enabled) {
    ui_event_dispatch_enabled_ = enabled;
  }
  [[nodiscard]] bool IsUiEventDispatchEnabled() const {
    return ui_event_dispatch_enabled_;
  }
  static void FireEvent(std::int32_t event_id, const char *fmt, ...);

  static void OnToonOnline(std::int32_t a1, std::int32_t a2, const BNetVariant *a3);
  static void OnNewPresence(std::int32_t a1, std::int32_t a2, const BNetVariant *a3);
  static void OnToonNameChanged(std::int32_t presence_id);
  static void OnFactionChanged(std::int32_t presence_id);

  static void OnGenericPresenceFieldUpdated(std::int32_t a1, std::int32_t a2,
                                            const BNetVariant *a3);
  static void OnChatWhisperSent(std::int32_t a1, std::int32_t a2,
                                const BNetChatWhisperPayload *a3);
  static void OnChatWhisperReceived(std::int32_t a1, std::int32_t a2,
                                    const BNetChatWhisperPayload *a3);
  static void OnChatMessage(std::int32_t a1, std::int32_t a2,
                            const BNetConversationMessagePayload *a3);
  static void OnOnlineTimeChanged(std::int32_t a1, std::int32_t a2,
                                  const BNetOnlineTimeChangedPayload *a3);
  static void OnCustomMessageChanged(std::int32_t a1, std::int32_t a2,
                                     const BNetCustomMessageChangedPayload *a3);
  static void OnOnlineStatusChanged(std::int32_t a1, std::int32_t a2,
                                    const BNetOnlineStatusChangedPayload *a3);
  static void OnHandleError(BNetErrorCode code, std::int32_t context);
  static void OnFOFInfoReceived(std::int32_t a1, std::int32_t a2,
                                const BNetFriendsOfFriendInfoPayload *a3);
  static void OnFriendListInitialized(std::int32_t a1, std::int32_t a2, void *a3);
  static void OnFriendAdded(std::int32_t a1, std::int32_t a2, const BNetVariant *a3);
  static void OnFriendRemoved(std::int32_t a1, std::int32_t a2, const BNetVariant *a3);

  void Clear();

private:
  friend class BattleNetUI;

  BattleNetApi() = default;

  struct ToonNameLookupSource {
    const char *text{nullptr};
    const void *identity{nullptr};
    std::int32_t realm_id{-1};

    [[nodiscard]] explicit operator bool() const {
      return identity != nullptr;
    }
  };

  [[nodiscard]] const BNetPresenceRecord *FindPresenceRecord(std::int32_t presence_id) const;
  [[nodiscard]] ToonNameLookupSource ResolveToonNameLookupSource(
      std::int32_t presence_id) const;
  [[nodiscard]] ToonNameLookupSource GetCurrentToonNameForPresenceLookup() const;
  [[nodiscard]] BNetPresenceValue ResolvePresenceValueSnapshot(std::int32_t presence_id,
                                                              std::int32_t key) const;
  [[nodiscard]] std::optional<BNetPresenceValue> FindStoredPresenceValue(std::int32_t presence_id,
                                                                         std::int32_t key) const;
  [[nodiscard]] std::int32_t FindFriendInviteIndexByPresenceID(std::int32_t presence_id) const;
  [[nodiscard]] bool IsPresenceOnline(std::int32_t presence_id) const;
  [[nodiscard]] std::int32_t NormalizeRecentWhisperPresenceId(std::int32_t presence_id) const;
  void ClearRecentWhisperContextBits(std::int32_t presence_id, std::uint32_t context_bits);
  void SetPresenceOnlineState(std::int32_t presence_id, bool is_online);
  void RemoveFriendAtIndex(std::int32_t index);
  void RemoveFriendInviteAtIndex(std::int32_t index);
  void ApplyPresenceRecordToFriend(BNetFriendInfo &info, const BNetPresenceRecord &record);
  void RefreshFriendDisplayName(BNetFriendInfo &info);
  void RefreshRecentWhisperTargets();
  void DisplayFriendListChatNotice(const char *message_key, std::int32_t presence_id) const;
  void SortFriendsForDisplayOrder();
  void SortFriendsForEventOrder();
  void ClearAccountToons(std::int32_t presence_id);
  void SetCIDBlockedState(std::int32_t cid, bool blocked);

  struct EventBinding {
    std::int32_t handle{0};
    std::string event_name;
    std::string handler_label;
  };

  struct PresenceUpdateBinding {
    std::int32_t handle{0};
    std::int32_t presence_id{0};
    std::int32_t key{0};
    std::string handler_label;
  };

  struct StoredSettingValue {
    BNetVariant value{};
    bool persist{false};
  };

  BNetConnectionState connection_state_{BNetConnectionState::kDisconnected};
  std::int32_t current_account_presence_id_{0};
  std::int32_t current_toon_presence_id_{0};
  bool rid_enabled_{false};
  bool dispatcher_available_{false};
  bool friend_list_initialized_{false};
  std::vector<BNetFriendInfo> friends_;
  std::vector<std::int32_t> cid_blocked_presence_ids_;
  std::unordered_set<std::int32_t> rid_friend_account_presence_ids_;
  std::unordered_set<std::int32_t> cid_friend_presence_ids_;
  std::vector<BNetFriendInvite> friend_invites_;
  std::int32_t fof_source_presence_id_{0};
  std::vector<std::int32_t> fof_presence_ids_;
  BNetBlockList block_list_;
  std::int32_t selected_block_presence_id_{0};
  std::int32_t selected_toon_block_presence_id_{0};
  std::int32_t selected_friend_presence_id_{0};
  std::string account_name_format_;
  std::map<std::string, StoredSettingValue> settings_;

  std::unordered_map<std::int32_t, std::string> presence_api_names_;
  std::unordered_map<BNetErrorCode, std::string> error_string_responses_;
  std::unordered_map<std::int32_t, BNetPresenceRecord> presence_records_;
  std::unordered_map<std::int32_t, std::vector<BNetToonInfo>> account_toon_infos_;
  std::unordered_map<std::int32_t, std::int32_t> toon_account_presence_ids_;
  std::unordered_map<std::uint64_t, BNetPresenceValue> presence_values_;
  mutable bool cached_current_toon_name_for_lookup_valid_{false};
  mutable const void *cached_current_toon_name_for_lookup_identity_{nullptr};
  mutable std::string cached_current_toon_name_for_lookup_;
  mutable std::string decorated_presence_name_buffer_;
  std::array<BNetConversation, kBNetConversationChannelCount> conversations_{};
  std::int32_t next_registration_handle_{1};
  std::vector<EventBinding> event_bindings_;
  std::vector<PresenceUpdateBinding> presence_bindings_;
  bool ui_event_dispatch_enabled_{false};
  BNetUiEventSink event_sink_;
  BNetChatDisplayHandler chat_display_handler_;
  BNetConversationCreateHandler conversation_create_handler_;
  BNetConversationInviteHandler conversation_invite_handler_;
  BNetConversationLeaveHandler conversation_leave_handler_;
  BNetConversationSendHandler conversation_send_handler_;
  BNetFriendNoteHandler friend_note_handler_;
  BNetPresenceActionHandler remove_friend_handler_;
  BNetFriendInviteActionHandler accept_friend_invite_handler_;
  BNetFriendInviteActionHandler decline_friend_invite_handler_;
  BNetFriendInviteActionHandler report_friend_invite_handler_;
  BNetFriendInviteByEmailHandler friend_invite_by_email_handler_;
  BNetFriendInviteByPresenceIdHandler friend_invite_by_presence_id_handler_;
  BNetFriendsOfFriendRequestHandler fof_request_handler_;
  BNetCidBlockHandler add_cid_block_handler_;
  BNetCidBlockHandler remove_cid_block_handler_;
  BNetRidBlockHandler add_rid_block_handler_;
  BNetRidBlockHandler remove_rid_block_handler_;
  BNetReportPlayerHandler report_player_handler_;
  BNetWhisperSendHandler whisper_send_handler_;
  BNetPresenceValueSetHandler presence_value_set_handler_;
};

int BN_CountFormatSpecifiers(const char *str);

}

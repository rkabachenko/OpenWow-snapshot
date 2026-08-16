
#include "openwow/game/battlenet_api.h"

#include "openwow/core/storm_error.h"
#include "openwow/core/storm_string.h"
#include "openwow/game/battlenet_events.h"
#include "openwow/game/battlenet_utf8.h"
#include "openwow/game/chat_display.h"
#include "openwow/game/localization.h"
#include "openwow/net/client_services.h"
#include "openwow/ui/game/autocomplete.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/game/ui_error_manager.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <variant>

namespace openwow::game {

namespace {

constexpr char kEmptyBNetName[] = "";
constexpr std::int32_t kFirstBNetUiEventId = 675;
constexpr std::int32_t kBNetEventDisconnected = 676;
constexpr std::int32_t kBNetEventSelfOnline = 677;
constexpr std::int32_t kBNetEventFriendInfoChangedLegacy = 684;
constexpr std::int32_t kBNetEventCustomMessageChanged = 685;
constexpr std::int32_t kBNetEventCustomMessageLoaded = 686;
constexpr std::int32_t kBNetEventBlockListUpdated = 705;
constexpr std::int32_t kBNetEventSystemMessage = 706;
constexpr std::int32_t kBNetEventRequestFofSucceeded = 707;
constexpr std::int32_t kBNetEventRequestFofFailed = 708;
constexpr std::int32_t kBNetEventNewPresence = 709;
constexpr std::int32_t kBNetEventToonNameUpdated = 710;
constexpr std::int32_t kBNetEventFriendAccountOnline = 711;
constexpr std::int32_t kBNetEventFriendAccountOffline = 712;
constexpr std::int32_t kBNetEventFriendToonOnline = 713;
constexpr std::int32_t kBNetEventFriendToonOffline = 714;
constexpr std::int32_t kBNetEventMatureLanguageFilter = 715;
constexpr std::size_t kBNetDisplayNameMaxCodepoints = 72;
constexpr std::size_t kBNetFormattedNameMaxCodepoints = 40;
constexpr std::size_t kBNetFriendInviteMaxCount = 100;
constexpr BNetErrorCode kBNetUiInfoSuppressedCode = 318;
constexpr std::size_t kBNetUiErrorBufferSize = 1024;

std::int32_t NormalizeBNetBlockListCount(const std::int32_t count) {
  return std::clamp<std::int32_t>(count, 0, static_cast<std::int32_t>(kBNetBlockMaxCount));
}

std::int32_t FindSelectedBlockLuaIndex(const BNetBlockList &block_list,
                                       const std::int32_t selected_presence_id) {
  if (selected_presence_id == 0) {
    return 0;
  }

  const auto block_count = NormalizeBNetBlockListCount(block_list.count);
  for (std::int32_t index = 0; index < block_count; ++index) {
    if (block_list.entries[static_cast<std::size_t>(index)].presence_id == selected_presence_id) {
      return index + 1;
    }
  }

  return 0;
}

std::uint64_t MakePresenceValueStorageKey(std::int32_t presence_id, std::int32_t key) {
  return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(presence_id)) << 32) |
         static_cast<std::uint32_t>(key);
}

bool HasBNetRidTransportAccess(const BattleNetApi &api) {
  return openwow::net::ClientServices::Instance().HasBattleNetRidTransport() &&
         api.IsRIDEnabled();
}

std::optional<std::string> FormatBNetErrorMessage(BattleNetApi &api, const BNetErrorCode code,
                                                  const std::int32_t context) {
  const char *format = api.GetErrorString(code);
  if (format == nullptr) {
    return std::nullopt;
  }

  std::array<char, kBNetUiErrorBufferSize> buffer{};
  const int specifier_count = BN_CountFormatSpecifiers(format);
  if (specifier_count == 0) {
    core::SStrCopyUTF8(buffer.data(), format, buffer.size(), buffer.size());
    return std::string(buffer.data());
  }

  auto &localization = Localization::Get();
  if (specifier_count == 1) {
    const char *presence_name = api.GetNameForPresenceID(context);
    const std::string fallback_name = localization.GetString("UNKNOWNOBJECT", "UNKNOWNOBJECT");
    const char *resolved_name =
        (presence_name != nullptr && presence_name[0] != '\0') ? presence_name : fallback_name.c_str();
    core::SStrPrintf(buffer.data(), buffer.size(), format, resolved_name);
    return std::string(buffer.data());
  }

  const std::string internal_string_error =
      localization.GetString("INTERNAL_STRING_ERROR", "INTERNAL_STRING_ERROR");
  core::SStrPrintf(buffer.data(), buffer.size(), internal_string_error.c_str(),
                   static_cast<unsigned int>(code));
  return std::string(buffer.data());
}

constexpr std::array<const char *, 30> kBNetUiEventNames = {
    "BN_CONNECTED",
    "BN_DISCONNECTED",
    "BN_SELF_ONLINE",
    "BN_SELF_OFFLINE",
    "BN_FRIEND_LIST_SIZE_CHANGED",
    "BN_FRIEND_INVITE_LIST_INITIALIZED",
    "BN_FRIEND_INVITE_SEND_RESULT",
    "BN_FRIEND_INVITE_ADDED",
    "BN_FRIEND_INVITE_REMOVED",
    "BN_FRIEND_INFO_CHANGED",
    "BN_CUSTOM_MESSAGE_CHANGED",
    "BN_CUSTOM_MESSAGE_LOADED",
    "CHAT_MSG_BN_WHISPER",
    "CHAT_MSG_BN_WHISPER_INFORM",
    "BN_CHAT_WHISPER_UNDELIVERABLE",
    "BN_CHAT_CHANNEL_JOINED",
    "BN_CHAT_CHANNEL_LEFT",
    "BN_CHAT_CHANNEL_CLOSED",
    "CHAT_MSG_BN_CONVERSATION",
    "CHAT_MSG_BN_CONVERSATION_NOTICE",
    "CHAT_MSG_BN_CONVERSATION_LIST",
    "BN_CHAT_CHANNEL_MESSAGE_UNDELIVERABLE",
    "BN_CHAT_CHANNEL_MESSAGE_BLOCKED",
    "BN_CHAT_CHANNEL_MEMBER_JOINED",
    "BN_CHAT_CHANNEL_MEMBER_LEFT",
    "BN_CHAT_CHANNEL_MEMBER_UPDATED",
    "BN_CHAT_CHANNEL_CREATE_SUCCEEDED",
    "BN_CHAT_CHANNEL_CREATE_FAILED",
    "BN_CHAT_CHANNEL_INVITE_SUCCEEDED",
    "BN_CHAT_CHANNEL_INVITE_FAILED",
};

const char *LookupBNetUiEventName(std::int32_t event_id) {
  switch (event_id) {
  case kBNetEventFriendInfoChangedLegacy:
    return "BN_FRIEND_INFO_CHANGED";
  case kBNetEventCustomMessageChanged:
    return "BN_CUSTOM_MESSAGE_CHANGED";
  case kBNetEventCustomMessageLoaded:
    return "BN_CUSTOM_MESSAGE_LOADED";
  case kBNetEventBlockListUpdated:
    return "BN_BLOCK_LIST_UPDATED";
  case kBNetEventSystemMessage:
    return "BN_SYSTEM_MESSAGE";
  case kBNetEventRequestFofSucceeded:
    return "BN_REQUEST_FOF_SUCCEEDED";
  case kBNetEventRequestFofFailed:
    return "BN_REQUEST_FOF_FAILED";
  case kBNetEventNewPresence:
    return "BN_NEW_PRESENCE";
  case kBNetEventToonNameUpdated:
    return "BN_TOON_NAME_UPDATED";
  case kBNetEventFriendAccountOnline:
    return "BN_FRIEND_ACCOUNT_ONLINE";
  case kBNetEventFriendAccountOffline:
    return "BN_FRIEND_ACCOUNT_OFFLINE";
  case kBNetEventFriendToonOnline:
    return "BN_FRIEND_TOON_ONLINE";
  case kBNetEventFriendToonOffline:
    return "BN_FRIEND_TOON_OFFLINE";
  case kBNetEventMatureLanguageFilter:
    return "BN_MATURE_LANGUAGE_FILTER";
  default:
    break;
  }

  const auto index = event_id - kFirstBNetUiEventId;
  if (index < 0 || index >= static_cast<std::int32_t>(kBNetUiEventNames.size())) {
    return nullptr;
  }
  return kBNetUiEventNames[static_cast<std::size_t>(index)];
}

void CopyUtf8Field(char *dest, std::size_t dest_size, std::int32_t &logical_length,
                   std::int32_t &byte_length, std::string_view source, std::size_t max_codepoints) {
  const auto copy_result = CopyBoundedLegacyUtf8Text(dest, dest_size, source, max_codepoints);
  logical_length = copy_result.logical_length;
  byte_length = copy_result.byte_length;
}

void ClearFriendFormatterFields(BNetFriendInfo &info) {
  CopyUtf8Field(info.formatted_name_left, sizeof(info.formatted_name_left),
                info.formatted_name_left_length, info.formatted_name_left_bytes, std::string_view{},
                kBNetFormattedNameMaxCodepoints);
  CopyUtf8Field(info.formatted_name_right, sizeof(info.formatted_name_right),
                info.formatted_name_right_length, info.formatted_name_right_bytes, std::string_view{},
                kBNetFormattedNameMaxCodepoints);
  CopyUtf8Field(info.formatted_name_right_base, sizeof(info.formatted_name_right_base),
                info.formatted_name_right_base_length, info.formatted_name_right_base_bytes,
                std::string_view{}, kBNetFormattedNameMaxCodepoints);
}

void ApplyStoredFriendDisplaySnapshot(BNetFriendInfo &info, const BNetPresenceValue &value) {
  switch (value.type) {
  case BNetPresenceValue::Type::kCustom: {
    const auto &formatted = value.formatted_name;
    CopyUtf8Field(info.formatted_name_left, sizeof(info.formatted_name_left),
                  info.formatted_name_left_length, info.formatted_name_left_bytes,
                  formatted.formatted_name_left, kBNetFormattedNameMaxCodepoints);
    CopyUtf8Field(info.formatted_name_right, sizeof(info.formatted_name_right),
                  info.formatted_name_right_length, info.formatted_name_right_bytes,
                  formatted.formatted_name_right, kBNetFormattedNameMaxCodepoints);
    const std::string_view right_base =
        formatted.formatted_name_right_base.empty()
            ? std::string_view{formatted.formatted_name_right}
            : std::string_view{formatted.formatted_name_right_base};
    CopyUtf8Field(info.formatted_name_right_base, sizeof(info.formatted_name_right_base),
                  info.formatted_name_right_base_length, info.formatted_name_right_base_bytes,
                  right_base, kBNetFormattedNameMaxCodepoints);

    char formatted_display_name[sizeof(info.display_name)]{};
    const std::string name_format = Localization::Get().GetString("BATTLENET_NAME_FORMAT", "");
    core::SStrPrintf(formatted_display_name, sizeof(formatted_display_name), name_format.c_str(),
                     info.formatted_name_left, info.formatted_name_right);
    CopyUtf8Field(info.display_name, sizeof(info.display_name), info.display_name_length,
                  info.display_name_bytes, formatted_display_name,
                  kBNetDisplayNameMaxCodepoints);
    return;
  }

  case BNetPresenceValue::Type::kString:
  case BNetPresenceValue::Type::kStringPtr:
  case BNetPresenceValue::Type::kToonName:
    CopyUtf8Field(info.display_name, sizeof(info.display_name), info.display_name_length,
                  info.display_name_bytes, value.str_val, kBNetDisplayNameMaxCodepoints);
    ClearFriendFormatterFields(info);
    return;

  default:
    CopyUtf8Field(info.display_name, sizeof(info.display_name), info.display_name_length,
                  info.display_name_bytes, std::string_view{}, kBNetDisplayNameMaxCodepoints);
    ClearFriendFormatterFields(info);
    return;
  }
}

int CompareUtf8NameFieldsNoCase(const char *left, const char *right,
                                std::size_t max_codepoints) {
  return core::SStrCmpUTF8NoCase(left ? left : "", right ? right : "", max_codepoints);
}

bool CompareFriendInviteTimestampDescending(const BNetFriendInvite &left,
                                           const BNetFriendInvite &right) {
  return left.timestamp > right.timestamp;
}

void NormalizeFriendInvite(BNetFriendInvite &invite) {
  if (!invite.has_message) {
    invite.message.clear();
  }
}

void NormalizeFriendInvites(std::vector<BNetFriendInvite> &invites) {
  for (auto &invite : invites) {
    NormalizeFriendInvite(invite);
  }

  if (invites.size() > kBNetFriendInviteMaxCount) {
    invites.resize(kBNetFriendInviteMaxCount);
  }

  std::sort(invites.begin(), invites.end(), CompareFriendInviteTimestampDescending);
}

template <typename T>
void EraseVectorEntryOrFatal(std::vector<T> &entries, std::int32_t index) {
  if (index < 0 || static_cast<std::size_t>(index) >= entries.size()) {
    openwow::core::SErrFatalCondition("%s", "");
  }

  entries.erase(entries.begin() + index);
}

bool GetCVarBoolOrDefault(const char *name, bool default_value) {
  auto &cvars = ui::game::CVarSystem::Instance();
  if (cvars.GetCVar(name).empty()) {
    return default_value;
  }
  return cvars.GetCVarBool(name);
}

bool IsTruthyBNetPresenceValue(const BNetPresenceValue &value) {
  return value.type == BNetPresenceValue::Type::kBool && value.byte_val != 0;
}

const char *ResolveBNetChatFlagTag(BattleNetApi &api, std::int32_t presence_id) {
  constexpr auto kAFKKey = static_cast<std::int32_t>(BNetPresenceKey::kAFK);
  constexpr auto kDNDKey = static_cast<std::int32_t>(BNetPresenceKey::kDND);

  if (IsTruthyBNetPresenceValue(api.GetPresenceValue(presence_id, kAFKKey))) {
    return "AFK";
  }
  if (IsTruthyBNetPresenceValue(api.GetPresenceValue(presence_id, kDNDKey))) {
    return "DND";
  }
  return nullptr;
}

std::array<std::byte, 9> EncodeBNetChatDisplayExtraData(std::uint32_t primary,
                                                        std::uint32_t secondary,
                                                        std::uint8_t flags) {
  std::array<std::byte, 9> storage{};
  std::memcpy(storage.data(), &primary, sizeof(primary));
  std::memcpy(storage.data() + sizeof(primary), &secondary, sizeof(secondary));
  std::memcpy(storage.data() + sizeof(primary) + sizeof(secondary), &flags, sizeof(flags));
  return storage;
}

void DisplayBNetWhisperMessage(BattleNetApi &api, const BNetChatWhisperPayload &payload,
                               int chat_type) {
  const char *display_name = api.GetNameForPresenceID(payload.presence_id);
  api.UpdateRecentWhispers(payload.presence_id, 48, true, display_name ? display_name : "");

  const std::string sanitized_text = openwow::game::CopySanitizedBNetChatText(payload.text, 1021);
  const auto extra_data =
      EncodeBNetChatDisplayExtraData(0, static_cast<std::uint32_t>(payload.presence_id), 1);
  const char *flag_tag = ResolveBNetChatFlagTag(api, payload.presence_id);
  api.DisplayChatMessage({
      .message = sanitized_text,
      .chat_type = chat_type,
      .sender_name = display_name ? std::optional<std::string>{display_name} : std::nullopt,
      .flag_tag = flag_tag ? std::optional<std::string>{flag_tag} : std::nullopt,
      .extra_data = extra_data,
  });
}

void DisplayBNetConversationMessage(BattleNetApi &api,
                                    const BNetConversationMessagePayload &payload) {
  if (payload.conversation_id >= kBNetConversationChannelCount) {
    openwow::core::SErrFatalCondition("%s",
                                      "BattleNetApi::OnChatMessage invalid conversation id");
  }

  const char *display_name = api.GetNameForPresenceID(payload.presence_id);
  api.UpdateRecentWhispers(payload.presence_id, 48, true, display_name ? display_name : "");

  const std::string sanitized_text = openwow::game::CopySanitizedBNetChatText(payload.text, 1021);
  const auto extra_data = EncodeBNetChatDisplayExtraData(
      static_cast<std::uint32_t>(payload.conversation_id),
      static_cast<std::uint32_t>(payload.presence_id), 0);
  api.DisplayChatMessage({
      .message = sanitized_text,
      .chat_type = ChatDisplayType::kBnConversation,
      .sender_name = display_name ? std::optional<std::string>{display_name} : std::nullopt,
      .extra_data = extra_data,
  });
}

std::vector<BNetUiEventArg> BuildBNetUiEventArgs(const char *fmt, va_list args) {
  std::vector<BNetUiEventArg> values;
  if (!fmt) {
    return values;
  }

  for (const char *cursor = fmt; *cursor != '\0'; ++cursor) {
    if (*cursor != '%') {
      continue;
    }

    ++cursor;
    if (*cursor == '\0') {
      break;
    }

    switch (*cursor) {
    case 'b':
      values.push_back(BNetUiEventArg::Boolean(va_arg(args, int) != 0));
      break;
    case 'd':
      values.push_back(BNetUiEventArg::Number(static_cast<double>(va_arg(args, int))));
      break;
    case 'f':
      values.push_back(BNetUiEventArg::Number(va_arg(args, double)));
      break;
    case 's': {
      const char *value = va_arg(args, const char *);
      if (value == nullptr) {
        values.push_back(BNetUiEventArg::Nil());
      } else {
        values.push_back(BNetUiEventArg::String(value));
      }
      break;
    }
    case 'u':
      values.push_back(BNetUiEventArg::Number(static_cast<double>(va_arg(args, unsigned int))));
      break;
    default:
      break;
    }
  }

  return values;
}

std::vector<openwow::ui::game::EventArg>
BuildGameUiEventArgs(const std::vector<BNetUiEventArg> &args) {
  std::vector<openwow::ui::game::EventArg> values;
  values.reserve(args.size());
  for (const auto &arg : args) {
    switch (arg.kind) {
    case BNetUiEventArg::Kind::kNil:
      values.emplace_back(std::monostate{});
      break;
    case BNetUiEventArg::Kind::kString:
      values.emplace_back(arg.string_value);
      break;
    case BNetUiEventArg::Kind::kNumber:
      values.emplace_back(arg.number_value);
      break;
    case BNetUiEventArg::Kind::kBoolean:
      values.emplace_back(arg.bool_value);
      break;
    }
  }
  return values;
}

bool HasBattleNetRegistrationDispatcher(bool explicit_dispatcher_available) {
  return explicit_dispatcher_available ||
         openwow::net::ClientServices::Instance().IsBNLogin();
}

std::string EncodeBNetRealmId(std::uint32_t realm_id) {
  std::string value;
  value.reserve(4);

  for (int shift = 24; shift >= 0; shift -= 8) {
    const auto byte = static_cast<char>((realm_id >> shift) & 0xFFu);
    if (byte != '\0') {
      value.push_back(byte);
    }
  }

  return value;
}

}

std::array<std::byte, 9> BuildBNetChatDisplayExtraData(const std::uint32_t primary,
                                                       const std::uint32_t secondary,
                                                       const std::uint8_t flags) {
  return EncodeBNetChatDisplayExtraData(primary, secondary, flags);
}

std::int32_t BNetVariant::ToInt() const {
  switch (type) {
  case BNetVariantType::kBool:
  case BNetVariantType::kPresenceFlag:
    return static_cast<std::int32_t>(bool_val);
  case BNetVariantType::kInt32:
  case BNetVariantType::kPresenceId:
    return int_val;
  case BNetVariantType::kFloat64:
    return static_cast<std::int32_t>(float_val);
  case BNetVariantType::kInlineString: {

    const char *s = str_ptr;
    if (!s)
      return 0;
    std::int32_t result = 0;
    while (*s) {
      result = (result << 8) | static_cast<unsigned char>(*s);
      ++s;
    }
    return result;
  }
  case BNetVariantType::kStringPtr: {
    if (!str_ptr)
      return 0;
    return static_cast<std::int32_t>(std::strtoull(str_ptr, nullptr, 0));
  }
  default:
    return 0;
  }
}

std::int32_t BNetVariant::ToSignedInt() const {
  switch (type) {
  case BNetVariantType::kBool:
  case BNetVariantType::kPresenceFlag:
    return static_cast<std::int32_t>(bool_val);
  case BNetVariantType::kInt32:
  case BNetVariantType::kPresenceId:
    return int_val;
  case BNetVariantType::kFloat64:
    return static_cast<std::int32_t>(float_val);
  case BNetVariantType::kInlineString: {
    const char *s = str_ptr;
    if (!s)
      return 0;
    std::int32_t result = 0;
    while (*s) {
      result = (result << 8) | static_cast<unsigned char>(*s);
      ++s;
    }
    return result;
  }
  case BNetVariantType::kStringPtr: {
    if (!str_ptr)
      return 0;
    return static_cast<std::int32_t>(std::strtoll(str_ptr, nullptr, 0));
  }
  default:
    return 0;
  }
}

int BN_CountFormatSpecifiers(const char *str) {
  if (!str)
    return 0;
  const int len = static_cast<int>(std::strlen(str));
  if (len <= 0)
    return 0;

  int count = 0;
  bool in_format = false;

  for (int i = 0; i < len; ++i) {
    const char ch = str[i];
    switch (ch) {
    case '%':
      in_format = !in_format;
      break;
    case 's':
    case 'S':
      if (in_format) {
        ++count;
        in_format = false;
      }
      break;
    case ' ':
    case '#':
    case '$':
    case '+':
    case '-':
    case '.':
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':

      break;
    default:
      if (in_format)
        return -1;
      break;
    }
  }
  return count;
}

BattleNetApi &BattleNetApi::Instance() {
  static BattleNetApi instance;
  return instance;
}

bool BattleNetApi::IsConnectedState() const {
  const auto s = static_cast<std::int32_t>(connection_state_);
  return s == 7 || s == 6 || s == 3;
}

std::int32_t BattleNetApi::FindFriendIndexByPresenceID(std::int32_t presence_id) const {
  if (friends_.empty())
    return -1;
  for (std::int32_t i = 0; i < static_cast<std::int32_t>(friends_.size()); ++i) {
    if (friends_[i].presence_id == presence_id)
      return i;
  }
  return -1;
}

const BNetFriendInfo *BattleNetApi::FindFriendRecordByPresenceID(std::int32_t presence_id) const {
  if (friends_.empty())
    return nullptr;
  for (const auto &friend_info : friends_) {
    if (friend_info.presence_id == presence_id)
      return &friend_info;
  }
  return nullptr;
}

const BNetFriendInfo *BattleNetApi::GetFriend(std::int32_t index) const {
  if (index < 0 || index >= static_cast<std::int32_t>(friends_.size()))
    return nullptr;
  return &friends_[index];
}

std::int32_t BattleNetApi::GetOnlineFriendCount() const {
  static constexpr std::int32_t kOnlineStatusKey =
      static_cast<std::int32_t>(BNetPresenceKey::kOnline);

  if (!IsConnectedState()) {
    return 0;
  }

  std::int32_t online_friend_count = 0;
  for (const auto &friend_info : friends_) {
    const auto value = ResolvePresenceValueSnapshot(friend_info.presence_id, kOnlineStatusKey);
    if (value.type == BNetPresenceValue::Type::kBool && value.byte_val != 0) {
      ++online_friend_count;
    }
  }

  return online_friend_count;
}

bool BattleNetApi::AddFriendToList(std::int32_t presence_id) {
  if (FindFriendIndexByPresenceID(presence_id) >= 0)
    return true;
  if (friends_.size() >= kBNetFriendMaxCount) {
    openwow::core::SErrFatalCondition("%s", "");
  }

  BNetFriendInfo info{};
  info.presence_id = presence_id;

  if (const auto *record = FindPresenceRecord(presence_id)) {
    ApplyPresenceRecordToFriend(info, *record);
  }

  if (const auto display_name_value = FindStoredPresenceValue(
          presence_id, static_cast<std::int32_t>(BNetPresenceKey::kDisplayName));
      display_name_value.has_value()) {
    ApplyStoredFriendDisplaySnapshot(info, *display_name_value);
  }

  if (const auto game_account_value = FindStoredPresenceValue(
          presence_id, static_cast<std::int32_t>(BNetPresenceKey::kGameAccount));
      game_account_value.has_value()) {
    info.game_account =
        game_account_value->type == BNetPresenceValue::Type::kInt32 ? game_account_value->int_val
                                                                    : 0;
  }

  friends_.push_back(info);
  return true;
}

bool BattleNetApi::SetPresenceValue(std::int32_t key, const BNetVariant &value) {
  if (!presence_value_set_handler_) {
    return false;
  }

  const auto result = presence_value_set_handler_(key, value);
  if (result.type == BNetVariantType::kBool) {
    return result.bool_val;
  }

  return result.ToInt() != 0;
}

BNetPresenceValue BattleNetApi::GetPresenceValue(std::int32_t presence_id, std::int32_t key) const {
  return ResolvePresenceValueSnapshot(presence_id, key);
}

BNetPresenceValue BattleNetApi::ResolvePresenceValueSnapshot(std::int32_t presence_id,
                                                             std::int32_t key) const {
  if (const auto stored_value = FindStoredPresenceValue(presence_id, key);
      stored_value.has_value()) {
    return *stored_value;
  }

  switch (static_cast<BNetPresenceKey>(key)) {
  case BNetPresenceKey::kDisplayName:
    if (const auto *record = FindPresenceRecord(presence_id)) {
      if (record->has_formatted_name) {
        BNetPresenceFormattedName formatted_name;
        formatted_name.display_name = record->display_name;
        formatted_name.formatted_name_left = record->formatted_name_left;
        formatted_name.formatted_name_right = record->formatted_name_right;
        formatted_name.formatted_name_right_base = record->formatted_name_right_base;
        return BNetPresenceValue::FormattedName(std::move(formatted_name));
      }

      if (!record->display_name.empty()) {
        return BNetPresenceValue::String(record->display_name);
      }
    }

    if (const auto it = presence_api_names_.find(presence_id); it != presence_api_names_.end()) {
      return BNetPresenceValue::String(it->second);
    }
    break;

  case BNetPresenceKey::kToonName:
    if (const auto *record = FindPresenceRecord(presence_id)) {
      if (!record->formatted_name_right.empty()) {
        return BNetPresenceValue::ToonName(record->formatted_name_right,
                                          record->toon_name_realm_id);
      }

      if (!record->formatted_name_right_base.empty()) {
        return BNetPresenceValue::ToonName(record->formatted_name_right_base,
                                          record->toon_name_realm_id);
      }
    }
    break;

  case BNetPresenceKey::kGameAccount:
    if (const auto *record = FindPresenceRecord(presence_id)) {
      return BNetPresenceValue::Int32(record->game_account);
    }
    break;

  default:
    break;
  }

  return {};
}

std::int32_t BattleNetApi::GetPresenceIDForCurrentAccount() const {
  if (!IsConnectedState())
    return 0;
  return current_account_presence_id_;
}

std::int32_t BattleNetApi::GetPresenceIDForCurrentToon() const {
  if (!IsConnectedState())
    return 0;
  return current_toon_presence_id_;
}

void BattleNetApi::SetCurrentAccountPresenceID(std::int32_t presence_id) {
  current_account_presence_id_ = presence_id;
}

void BattleNetApi::SetCurrentToonPresenceID(std::int32_t presence_id) {
  current_toon_presence_id_ = presence_id;
}

void BattleNetApi::SetRIDEnabled(bool enabled) {
  rid_enabled_ = enabled;
}

void BattleNetApi::SetPresenceRecord(std::int32_t presence_id, BNetPresenceRecord record) {
  record.has_formatted_name = record.has_formatted_name || !record.formatted_name_left.empty() ||
                              !record.formatted_name_right.empty() ||
                              !record.formatted_name_right_base.empty();

  if (record.formatted_name_right.empty()) {
    record.formatted_name_right = record.formatted_name_right_base;
  }
  if (record.formatted_name_right_base.empty()) {
    record.formatted_name_right_base = record.formatted_name_right;
  }

  if (!record.display_name.empty()) {
    presence_api_names_[presence_id] = record.display_name;
  }

  presence_records_[presence_id] = std::move(record);

  const auto idx = FindFriendIndexByPresenceID(presence_id);
  if (idx < 0) {
    return;
  }

  ApplyPresenceRecordToFriend(friends_[idx], presence_records_.at(presence_id));
  if (friend_list_initialized_) {
    UpdateFriendOnlineToons();
    SortFriendsForEventOrder();
    return;
  }

  RefreshFriendDisplayName(friends_[idx]);
}

void BattleNetApi::SetPresenceDisplayName(std::int32_t presence_id, std::string name) {
  presence_api_names_[presence_id] = name;
  presence_records_[presence_id].display_name = std::move(name);

  const auto idx = FindFriendIndexByPresenceID(presence_id);
  if (idx < 0) {
    return;
  }

  CopyUtf8Field(friends_[idx].display_name, sizeof(friends_[idx].display_name),
                friends_[idx].display_name_length, friends_[idx].display_name_bytes,
                presence_api_names_[presence_id], kBNetDisplayNameMaxCodepoints);
}

void BattleNetApi::SetErrorStringResponse(const BNetErrorCode code, std::string message) {
  error_string_responses_[code] = std::move(message);
}

void BattleNetApi::ClearAccountToons(std::int32_t presence_id) {
  if (const auto it = account_toon_infos_.find(presence_id); it != account_toon_infos_.end()) {
    for (const auto &toon_info : it->second) {
      toon_account_presence_ids_.erase(toon_info.toon_presence_id);
    }
    account_toon_infos_.erase(it);
  }
}

void BattleNetApi::SetAccountToons(std::int32_t presence_id, std::vector<BNetToonInfo> toon_infos) {
  ClearAccountToons(presence_id);

  if (toon_infos.empty()) {
    return;
  }

  if (toon_infos.size() == 1 && !toon_infos.front().is_focused) {
    toon_infos.front().is_focused = true;
  }

  for (const auto &toon_info : toon_infos) {
    if (toon_info.toon_presence_id == 0) {
      continue;
    }
    toon_account_presence_ids_[toon_info.toon_presence_id] = presence_id;
  }

  account_toon_infos_[presence_id] = std::move(toon_infos);
}

std::optional<BNetToonInfo> BattleNetApi::FindToonInfoByPresenceId(
    std::int32_t toon_presence_id) const {
  const auto account_it = toon_account_presence_ids_.find(toon_presence_id);
  if (account_it == toon_account_presence_ids_.end()) {
    return std::nullopt;
  }

  const auto toon_list_it = account_toon_infos_.find(account_it->second);
  if (toon_list_it == account_toon_infos_.end()) {
    return std::nullopt;
  }

  for (const auto &toon_info : toon_list_it->second) {
    if (toon_info.toon_presence_id == toon_presence_id) {
      return toon_info;
    }
  }

  return std::nullopt;
}

void BattleNetApi::SetCurrentToonInfo(std::int32_t presence_id, BNetCurrentToonInfo toon_info) {
  if (toon_info.toon_presence_id == 0) {
    ClearAccountToons(presence_id);
    return;
  }

  toon_info.is_focused = true;
  SetAccountToons(presence_id, {toon_info});
}

void BattleNetApi::SetPresenceValueResponse(std::int32_t presence_id, std::int32_t key,
                                            BNetPresenceValue value) {
  presence_values_[MakePresenceValueStorageKey(presence_id, key)] = std::move(value);
}

std::optional<BNetFriendLuaInfo> BattleNetApi::GetFriendLuaInfo(std::int32_t index) const {
  static constexpr std::int32_t kOnlineStatusKey =
      static_cast<std::int32_t>(BNetPresenceKey::kOnline);
  static constexpr std::int32_t kNumGameAccountsKey = 0x10007;
  static constexpr std::int32_t kFriendNoteKey = 0x10008;
  static constexpr std::int32_t kLastOnlineKey =
      static_cast<std::int32_t>(BNetPresenceKey::kLastOnline);
  static constexpr std::int32_t kCustomMessageKey =
      static_cast<std::int32_t>(BNetPresenceKey::kCustomMessage);
  static constexpr std::int32_t kAfkKey = static_cast<std::int32_t>(BNetPresenceKey::kAFK);
  static constexpr std::int32_t kDndKey = static_cast<std::int32_t>(BNetPresenceKey::kDND);
  static constexpr std::int32_t kToonNameKey =
      static_cast<std::int32_t>(BNetPresenceKey::kToonName);

  const auto *friend_info = GetFriend(index);
  if (!friend_info) {
    return std::nullopt;
  }

  BNetFriendLuaInfo result{};
  result.presence_id = friend_info->presence_id;
  result.first_name = friend_info->formatted_name_left;
  result.last_name = friend_info->formatted_name_right;
  result.is_online = IsPresenceOnline(friend_info->presence_id);
  result.is_friend = IsFriendPresenceID(friend_info->presence_id);

  if (const auto stored_online = FindStoredPresenceValue(friend_info->presence_id, kOnlineStatusKey);
      stored_online.has_value() && stored_online->type == BNetPresenceValue::Type::kBool) {
    result.is_online = stored_online->byte_val != 0;
  }

  if (const auto stored_count =
          FindStoredPresenceValue(friend_info->presence_id, kNumGameAccountsKey);
      stored_count.has_value() && stored_count->type == BNetPresenceValue::Type::kInt32) {
    result.num_game_accounts = stored_count->int_val;
  }

  if (const auto stored_afk = FindStoredPresenceValue(friend_info->presence_id, kAfkKey);
      stored_afk.has_value() && stored_afk->type == BNetPresenceValue::Type::kPresenceFlag) {
    result.is_afk = stored_afk->byte_val != 0;
  }

  if (const auto stored_dnd = FindStoredPresenceValue(friend_info->presence_id, kDndKey);
      stored_dnd.has_value() && stored_dnd->type == BNetPresenceValue::Type::kPresenceFlag) {
    result.is_dnd = stored_dnd->byte_val != 0;
  }

  if (const auto stored_custom =
          FindStoredPresenceValue(friend_info->presence_id, kCustomMessageKey);
      stored_custom.has_value() && stored_custom->type == BNetPresenceValue::Type::kString) {
    result.custom_message = stored_custom->str_val;
  }

  if (const auto stored_note = FindStoredPresenceValue(friend_info->presence_id, kFriendNoteKey);
      stored_note.has_value() && stored_note->type == BNetPresenceValue::Type::kString) {
    result.note_text = stored_note->str_val;
  }

  if (const auto stored_last_online =
          FindStoredPresenceValue(friend_info->presence_id, kLastOnlineKey);
      stored_last_online.has_value() &&
      stored_last_online->type == BNetPresenceValue::Type::kInt32) {
    result.last_online = stored_last_online->int_val;
  }

  const auto toon_name_value = GetPresenceValue(friend_info->presence_id, kToonNameKey);
  if (toon_name_value.type == BNetPresenceValue::Type::kToonName) {
    result.has_toon = true;
    result.toon_presence_id = GetFocusedToon(friend_info->presence_id);

    result.realm_id = EncodeBNetRealmId(static_cast<std::uint32_t>(toon_name_value.aux_int));

    if (const auto *toon_name = GetToonNameForPresenceId(friend_info->presence_id, false);
        toon_name != nullptr) {
      result.toon_name = toon_name;
    } else {
      result.toon_name = toon_name_value.str_val;
    }
  }

  return result;
}

void BattleNetApi::SetPresenceFriendship(std::int32_t presence_id, bool is_friend) {
  if (presence_id == 0) {
    return;
  }

  if (is_friend) {
    rid_friend_account_presence_ids_.insert(presence_id);
    return;
  }

  rid_friend_account_presence_ids_.erase(presence_id);
}

void BattleNetApi::SetCIDFriendship(std::int32_t presence_id, bool is_friend) {
  if (presence_id == 0) {
    return;
  }

  if (is_friend) {
    cid_friend_presence_ids_.insert(presence_id);
    return;
  }

  cid_friend_presence_ids_.erase(presence_id);
}

void BattleNetApi::SetCIDBlockedState(std::int32_t cid, bool blocked) {
  const auto it =
      std::find(cid_blocked_presence_ids_.begin(), cid_blocked_presence_ids_.end(), cid);
  if (blocked) {
    if (it == cid_blocked_presence_ids_.end()) {
      cid_blocked_presence_ids_.push_back(cid);
    }
    return;
  }

  if (it != cid_blocked_presence_ids_.end()) {
    cid_blocked_presence_ids_.erase(it);
  }
}

void BattleNetApi::SetConversation(std::uint8_t channel, std::int32_t channel_type,
                                   std::vector<std::int32_t> member_presence_ids) {
  if (channel >= conversations_.size())
    return;
  if (member_presence_ids.size() > kBNetConversationMaxMembers) {
    member_presence_ids.resize(kBNetConversationMaxMembers);
  }
  conversations_[channel].channel_type = channel_type;
  conversations_[channel].member_presence_ids = std::move(member_presence_ids);
}

void BattleNetApi::SetFriendInvites(std::vector<BNetFriendInvite> invites) {
  NormalizeFriendInvites(invites);
  friend_invites_ = std::move(invites);
}

void BattleNetApi::AppendFriendInviteOrFatal(BNetFriendInvite invite) {
  if (friend_invites_.size() >= kBNetFriendInviteMaxCount) {
    openwow::core::SErrFatalCondition("%s", "");
  }

  NormalizeFriendInvite(invite);
  friend_invites_.push_back(std::move(invite));
  std::sort(friend_invites_.begin(), friend_invites_.end(),
            CompareFriendInviteTimestampDescending);
}

void BattleNetApi::SetFriendsOfFriendList(std::int32_t source_presence_id,
                                          std::vector<std::int32_t> presence_ids) {
  if (presence_ids.size() > kBNetFriendOfFriendMaxCount) {
    presence_ids.resize(kBNetFriendOfFriendMaxCount);
  }

  fof_source_presence_id_ = source_presence_id;
  fof_presence_ids_ = std::move(presence_ids);
}

void BattleNetApi::SetBlockList(BNetBlockList block_list) {
  block_list_ = std::move(block_list);
}

void BattleNetApi::SetSelectedBlockPresenceId(const std::int32_t presence_id) {
  selected_block_presence_id_ = presence_id;
}

void BattleNetApi::SetSelectedToonBlockPresenceId(const std::int32_t presence_id) {
  selected_toon_block_presence_id_ = presence_id;
}

void BattleNetApi::SetSelectedFriendPresenceId(const std::int32_t presence_id) {
  selected_friend_presence_id_ = presence_id;
}

std::int32_t BattleNetApi::GetFriendInviteCount() const {
  return static_cast<std::int32_t>(friend_invites_.size());
}

std::optional<BNetFriendInviteView> BattleNetApi::GetFriendInviteInfo(std::int32_t index) const {
  if (index < 0 || index >= static_cast<std::int32_t>(friend_invites_.size())) {
    return std::nullopt;
  }

  const auto &invite = friend_invites_[static_cast<std::size_t>(index)];
  BNetFriendInviteView view{};
  view.presence_id = invite.presence_id;
  view.has_message = invite.has_message;
  view.message = invite.message;
  view.timestamp = invite.timestamp;

  const auto display_name_value = ResolvePresenceValueSnapshot(
      invite.presence_id, static_cast<std::int32_t>(BNetPresenceKey::kDisplayName));
  if (display_name_value.type == BNetPresenceValue::Type::kCustom) {
    view.has_name = true;
    view.formatted_name_left = display_name_value.formatted_name.formatted_name_left;
    view.formatted_name_right = display_name_value.formatted_name.formatted_name_right;
  }

  return view;
}

std::int32_t BattleNetApi::GetFriendsOfFriendSourcePresenceId() const {
  return fof_source_presence_id_;
}

std::pair<std::int32_t, std::int32_t> BattleNetApi::GetFriendsOfFriendCounts() const {
  std::int32_t mutual_count = 0;
  std::int32_t non_mutual_count = 0;

  for (const auto presence_id : fof_presence_ids_) {

    if (IsPresenceIDSelf(presence_id)) {
      continue;
    }

    if (IsPresenceIDFriend(presence_id)) {
      ++mutual_count;
    } else {
      ++non_mutual_count;
    }
  }

  return {mutual_count, non_mutual_count};
}

std::optional<BNetFriendOfFriendView>
BattleNetApi::GetFriendOfFriendInfo(std::int32_t filtered_index, bool include_mutual,
                                    bool include_non_mutual) const {
  if (filtered_index < 0) {
    return std::nullopt;
  }

  std::int32_t current_index = 0;
  for (const auto presence_id : fof_presence_ids_) {

    if (IsPresenceIDSelf(presence_id)) {
      continue;
    }

    const bool is_mutual = IsPresenceIDFriend(presence_id);
    if ((is_mutual && !include_mutual) || (!is_mutual && !include_non_mutual)) {
      continue;
    }

    if (current_index != filtered_index) {
      ++current_index;
      continue;
    }

    BNetFriendOfFriendView view{};
    view.presence_id = presence_id;
    view.is_mutual = is_mutual;

    const auto name_value = ResolvePresenceValueSnapshot(
        presence_id, static_cast<std::int32_t>(BNetPresenceKey::kDisplayName));
    if (name_value.type == BNetPresenceValue::Type::kCustom) {
      view.has_name = true;
      view.formatted_name_left = name_value.formatted_name.formatted_name_left;
      view.formatted_name_right = name_value.formatted_name.formatted_name_right;
    }

    return view;
  }

  return std::nullopt;
}

bool BattleNetApi::IsFriendPresenceID(std::int32_t presence_id) const {
  return rid_friend_account_presence_ids_.find(presence_id) !=
         rid_friend_account_presence_ids_.end();
}

bool BattleNetApi::IsPresenceIDFriend(std::int32_t presence_id) const {
  if (rid_friend_account_presence_ids_.find(presence_id) !=
      rid_friend_account_presence_ids_.end()) {
    return true;
  }

  const auto account_it = toon_account_presence_ids_.find(presence_id);
  return account_it != toon_account_presence_ids_.end() &&
         rid_friend_account_presence_ids_.find(account_it->second) !=
             rid_friend_account_presence_ids_.end();
}

bool BattleNetApi::IsPresenceIDSelf(std::int32_t presence_id) const {
  if (!IsConnectedState())
    return false;
  return presence_id == current_account_presence_id_;
}

bool BattleNetApi::IsPresenceIDBlocked(std::int32_t presence_id) const {
  const auto clamped_count = std::clamp<std::int32_t>(
      block_list_.count, 0, static_cast<std::int32_t>(block_list_.entries.size()));
  for (std::int32_t i = 0; i < clamped_count; ++i) {
    if (block_list_.entries[static_cast<std::size_t>(i)].presence_id == presence_id) {
      return true;
    }
  }

  return false;
}

bool BattleNetApi::IsRIDEnabled() const {
  return rid_enabled_;
}

BNetBlockList BattleNetApi::GetBlockList() const {
  return block_list_;
}

std::int32_t BattleNetApi::GetSelectedBlockLuaIndex() const {
  return FindSelectedBlockLuaIndex(block_list_, selected_block_presence_id_);
}

std::int32_t BattleNetApi::GetSelectedToonBlockLuaIndex() const {
  return FindSelectedBlockLuaIndex(block_list_, selected_toon_block_presence_id_);
}

std::int32_t BattleNetApi::GetSelectedFriendLuaIndex() const {
  if (selected_friend_presence_id_ == 0) {
    return 0;
  }

  const auto index = FindFriendIndexByPresenceID(selected_friend_presence_id_);
  if (index < 0) {
    return 0;
  }

  return index + 1;
}

BNetErrorCode BattleNetApi::AddRIDBlock(std::int32_t presence_id) {

  if (!add_rid_block_handler_) {
    return 0;
  }
  return add_rid_block_handler_(presence_id);
}

BNetErrorCode BattleNetApi::RemoveRIDBlock(std::int32_t presence_id) {
  if (!remove_rid_block_handler_) {
    return 0;
  }
  return remove_rid_block_handler_(presence_id);
}

BNetErrorCode BattleNetApi::SendChatWhisper(std::int32_t presence_id, const std::string &text) {
  if (!whisper_send_handler_)
    return 0;
  return whisper_send_handler_(presence_id, text);
}

BNetErrorCode BattleNetApi::SendRIDFriendInviteByEmail(const std::string &email,
                                                       const std::string &note) {
  if (!friend_invite_by_email_handler_)
    return 0;
  return friend_invite_by_email_handler_(email, note);
}

BNetErrorCode BattleNetApi::SendRIDFriendInviteByPresenceId(std::int32_t presence_id,
                                                            const std::string &note) {
  if (!friend_invite_by_presence_id_handler_)
    return 0;
  return friend_invite_by_presence_id_handler_(presence_id, note);
}

BNetErrorCode BattleNetApi::RemoveFriend(std::int32_t presence_id) {
  if (!remove_friend_handler_)
    return 0;
  return remove_friend_handler_(presence_id);
}

BNetErrorCode BattleNetApi::AcceptFriendInvite(std::int32_t presence_id) {
  if (!accept_friend_invite_handler_)
    return 0;
  return accept_friend_invite_handler_(presence_id);
}

BNetErrorCode BattleNetApi::DeclineFriendInvite(std::int32_t presence_id) {
  if (!decline_friend_invite_handler_)
    return 0;
  return decline_friend_invite_handler_(presence_id);
}

BNetErrorCode BattleNetApi::ReportFriendInvite(std::int32_t presence_id) {
  if (!report_friend_invite_handler_)
    return 0;
  return report_friend_invite_handler_(presence_id);
}

BNetErrorCode BattleNetApi::ReportPlayer(std::int32_t presence_id, std::int32_t report_type,
                                         const char *note) {
  if (!report_player_handler_)
    return 0;
  return report_player_handler_(presence_id, report_type, note);
}

BNetErrorCode BattleNetApi::CreateConversation(const BNetPresenceValue &first_toon_name,
                                               const BNetPresenceValue &second_toon_name) {
  if (!conversation_create_handler_)
    return 0;
  return conversation_create_handler_(first_toon_name, second_toon_name);
}

BNetErrorCode BattleNetApi::InviteToConversation(std::uint8_t conversation_id,
                                                 const BNetPresenceValue &invitee_toon_name) {
  if (!conversation_invite_handler_)
    return 0;
  return conversation_invite_handler_(conversation_id, invitee_toon_name);
}

BNetErrorCode BattleNetApi::LeaveConversation(std::uint8_t conversation_id) {
  if (!conversation_leave_handler_)
    return 0;
  return conversation_leave_handler_(conversation_id);
}

BNetErrorCode BattleNetApi::SendConversationMessage(std::uint8_t conversation_id,
                                                    const std::string &text) {
  if (!conversation_send_handler_)
    return 0;
  return conversation_send_handler_(conversation_id, text);
}

std::int32_t BattleNetApi::GetNumConversationMembers(std::uint8_t conversation_id) const {
  if (!IsConnectedState())
    return 0;
  return static_cast<std::int32_t>(GetConversationMemberList(conversation_id).size());
}

std::vector<std::int32_t>
BattleNetApi::GetConversationMemberList(std::uint8_t conversation_id) const {
  if (!IsConnectedState())
    return {};
  if (conversation_id >= conversations_.size())
    return {};
  return conversations_[conversation_id].member_presence_ids;
}

BNetErrorCode BattleNetApi::SetFriendNote(std::int32_t presence_id, const std::string &note) {

  if (!friend_note_handler_)
    return 0;
  return friend_note_handler_(presence_id, note);
}

BNetErrorCode BattleNetApi::SetCustomMessage(const std::string &message) {
  return static_cast<BNetErrorCode>(SetPresenceValue(
      static_cast<std::int32_t>(BNetPresenceKey::kCustomMessage),
      BNetVariant::PresenceText(message)));
}

std::int32_t BattleNetApi::RegisterEvent(std::string_view event_name,
                                         std::string_view handler_label) {

  if (!HasBattleNetRegistrationDispatcher(dispatcher_available_)) {
    return 0;
  }

  const auto handle = next_registration_handle_++;
  event_bindings_.push_back(EventBinding{
      .handle = handle,
      .event_name = std::string(event_name),
      .handler_label = std::string(handler_label),
  });
  return handle;
}

std::int32_t BattleNetApi::RegisterPresenceUpdateCallback(std::int32_t presence_id,
                                                          std::int32_t key,
                                                          std::string_view handler_label) {
  if (!HasBattleNetRegistrationDispatcher(dispatcher_available_)) {
    return 0;
  }

  const auto handle = next_registration_handle_++;
  presence_bindings_.push_back(PresenceUpdateBinding{
      .handle = handle,
      .presence_id = presence_id,
      .key = key,
      .handler_label = std::string(handler_label),
  });
  return handle;
}

std::size_t BattleNetApi::GetRegisteredEventBindingCount() const {
  return event_bindings_.size();
}

std::size_t BattleNetApi::GetRegisteredPresenceUpdateBindingCount() const {
  return presence_bindings_.size();
}

bool BattleNetApi::HasRegisteredEventBinding(std::string_view event_name,
                                             std::string_view handler_label) const {
  return std::any_of(event_bindings_.begin(), event_bindings_.end(),
                     [event_name, handler_label](const EventBinding &binding) {
                       return binding.event_name == event_name &&
                              binding.handler_label == handler_label;
                     });
}

bool BattleNetApi::HasRegisteredPresenceUpdateBinding(std::int32_t presence_id, std::int32_t key,
                                                      std::string_view handler_label) const {
  return std::any_of(presence_bindings_.begin(), presence_bindings_.end(),
                     [presence_id, key, handler_label](const PresenceUpdateBinding &binding) {
                       return binding.presence_id == presence_id && binding.key == key &&
                              binding.handler_label == handler_label;
                     });
}

const char *BattleNetApi::GetErrorString(const BNetErrorCode code) const {
  if (code == 0) {
    return nullptr;
  }

  const auto it = error_string_responses_.find(code);
  if (it == error_string_responses_.end()) {
    return nullptr;
  }

  return it->second.c_str();
}

void BattleNetApi::HandleError(const BNetErrorCode code, const std::int32_t context) {
  if (code == 0) {
    return;
  }

  const auto message = FormatBNetErrorMessage(*this, code, context);
  if (!message.has_value()) {
    return;
  }

  DisplayChatMessage({
      .message = *message,
      .chat_type = ChatDisplayType::kSystem,
  });
  if (code == kBNetUiInfoSuppressedCode) {
    return;
  }

  openwow::ui::UIErrorManager::Get().AddInfoMessage(*message);
  openwow::ui::game::ScriptEventDispatch::Get().FireUiInfoMessage(*message);
}

void BattleNetApi::UpdateRecentWhispers(std::int32_t presence_id, std::uint32_t status_flags,
                                        bool update_timestamp, std::string_view display_name) {

  if (presence_id == 0)
    return;

  auto &targets = openwow::ui::game::AutoComplete::Get();
  if (IsPresenceIDSelf(presence_id)) {
    targets.ClearRecentPresenceId(presence_id);
    return;
  }

  const std::int32_t normalized_presence_id = NormalizeRecentWhisperPresenceId(presence_id);
  if (normalized_presence_id == 0) {
    return;
  }

  std::string resolved_name;
  if (const char *cached_name = GetNameForPresenceID(normalized_presence_id);
      cached_name && cached_name[0] != '\0') {
    resolved_name = cached_name;
  } else if (!display_name.empty()) {
    resolved_name.assign(display_name);
  }

  targets.TouchRecentPresenceId(normalized_presence_id, status_flags, update_timestamp,
                                resolved_name);
  if (!resolved_name.empty()) {
    targets.UpdateRecentPresenceIdName(normalized_presence_id, resolved_name, true);
  }
}

std::vector<std::string> BattleNetApi::GetRecentWhisperAutoCompleteResults(
    std::string_view text, std::uint32_t include_flags, std::uint32_t exclude_flags,
    std::size_t max_results, std::size_t cursor_position, bool allow_full_match) const {
  return openwow::ui::game::AutoComplete::Get().GetRecentPresenceLuaCompletions(
      text, include_flags, exclude_flags, max_results, cursor_position, allow_full_match);
}

std::optional<std::int32_t> BattleNetApi::GetRecentWhisperPresenceIdForName(
    std::string_view name) const {
  return openwow::ui::game::AutoComplete::Get().GetRecentPresenceIdForName(name);
}

void BattleNetApi::FireDisconnectedEventAndClearRecentWhispers(const bool event_flag) {
  FireEvent(kBNetEventDisconnected, "%b", event_flag);
  ClearRecentWhispers();
}

void BattleNetApi::ClearRecentWhispers() {
  openwow::ui::game::AutoComplete::Get().ClearRecentPresenceTargets();
}

bool BattleNetApi::IsFullyConnected() const {
  return openwow::net::ClientServices::Instance().HasBattleNetRidTransport() && rid_enabled_ &&
         IsConnectedState();
}

std::int32_t BattleNetApi::GetAccountPresenceId(std::int32_t presence_id) const {
  if (!IsConnectedState())
    return 0;
  if (const auto it = toon_account_presence_ids_.find(presence_id);
      it != toon_account_presence_ids_.end()) {
    return it->second;
  }
  return presence_id;
}

const char *BattleNetApi::GetNameForPresenceId(std::int32_t presence_id, bool ) const {
  if (!IsConnectedState()) {
    return nullptr;
  }
  return GetNameForPresenceID(presence_id);
}

const char *BattleNetApi::GetExactNameForPresenceId(std::int32_t presence_id, bool exact) const {
  return GetNameForPresenceId(presence_id, exact);
}

const char *BattleNetApi::GetToonNameForPresenceId(std::int32_t presence_id, bool ) const {
  if (!IsConnectedState())
    return nullptr;

  const auto toon_name = ResolveToonNameLookupSource(presence_id);
  return toon_name ? toon_name.text : nullptr;
}

std::int32_t BattleNetApi::GetNumToons(std::int32_t presence_id) const {
  if (!IsConnectedState())
    return 0;

  const auto it = account_toon_infos_.find(presence_id);
  if (it == account_toon_infos_.end()) {
    return 0;
  }

  return static_cast<std::int32_t>(it->second.size());
}

std::int32_t BattleNetApi::GetToon(std::int32_t presence_id, std::int32_t index) const {
  if (!IsConnectedState())
    return 0;

  const auto it = account_toon_infos_.find(presence_id);
  if (it == account_toon_infos_.end() || index < 0 ||
      index >= static_cast<std::int32_t>(it->second.size())) {
    return 0;
  }

  return it->second[static_cast<std::size_t>(index)].toon_presence_id;
}

std::int32_t BattleNetApi::GetFocusedToon(std::int32_t presence_id) const {
  if (!IsConnectedState())
    return 0;

  const auto it = account_toon_infos_.find(presence_id);
  if (it == account_toon_infos_.end() || it->second.empty()) {
    return 0;
  }

  for (const auto &toon_info : it->second) {
    if (toon_info.is_focused) {
      return toon_info.toon_presence_id;
    }
  }

  return it->second.front().toon_presence_id;
}

bool BattleNetApi::IsCIDFriend(std::int32_t cid) const {
  return cid_friend_presence_ids_.find(cid) != cid_friend_presence_ids_.end();
}

BNetErrorCode BattleNetApi::AddCIDBlock(std::int32_t cid) {

  if (add_cid_block_handler_) {
    const auto error_code = add_cid_block_handler_(cid);
    if (error_code != 0) {
      return error_code;
    }
  }

  SetCIDBlockedState(cid, true);
  return 0;
}

BNetErrorCode BattleNetApi::RemoveCIDBlock(std::int32_t cid) {
  if (remove_cid_block_handler_) {
    const auto error_code = remove_cid_block_handler_(cid);
    if (error_code != 0) {
      return error_code;
    }
  }

  SetCIDBlockedState(cid, false);
  return 0;
}

bool BattleNetApi::IsCIDBlock(std::int32_t cid) const {
  return std::find(cid_blocked_presence_ids_.begin(), cid_blocked_presence_ids_.end(), cid) !=
         cid_blocked_presence_ids_.end();
}

std::int32_t BattleNetApi::GetChatChannelType(std::uint8_t channel) const {
  if (!IsConnectedState())
    return 0;
  if (channel >= conversations_.size())
    return 0;
  return conversations_[channel].channel_type;
}

bool BattleNetApi::UnregisterPresenceUpdateCallback(std::int32_t handle) {
  const auto it = std::find_if(
      presence_bindings_.begin(), presence_bindings_.end(),
      [handle](const PresenceUpdateBinding &binding) { return binding.handle == handle; });
  if (it == presence_bindings_.end()) {
    return false;
  }

  presence_bindings_.erase(it);
  return true;
}

bool BattleNetApi::UnregisterEvent(std::int32_t handle) {
  const auto it =
      std::find_if(event_bindings_.begin(), event_bindings_.end(),
                   [handle](const EventBinding &binding) { return binding.handle == handle; });
  if (it == event_bindings_.end()) {
    return false;
  }

  event_bindings_.erase(it);
  return true;
}

void BattleNetApi::SetAccountNameFormatString(const char *fmt) {
  if (!IsConnectedState())
    return;
  account_name_format_ = fmt ? fmt : "";

  if (friend_list_initialized_) {
    UpdateFriendOnlineToons();
    SortFriendsForEventOrder();
    RefreshRecentWhisperTargets();
    return;
  }

  for (auto &info : friends_) {
    RefreshFriendDisplayName(info);
  }
  RefreshRecentWhisperTargets();
}

void BattleNetApi::SetSetting(std::string_view setting_name, const BNetVariant &value, bool persist) {
  if (!IsConnectedState()) {
    return;
  }

  auto &stored = settings_[std::string(setting_name)];
  stored.value = value;
  stored.persist = persist;
}

BNetVariant BattleNetApi::GetSetting(std::string_view setting_name) const {
  if (!IsConnectedState()) {
    return {};
  }

  const auto it = settings_.find(std::string(setting_name));
  if (it == settings_.end()) {
    return {};
  }

  return it->second.value;
}

BNetErrorCode BattleNetApi::RequestFriendsOfFriendInfo(std::int32_t presence_id) {
  if (!fof_request_handler_)
    return 0;
  return fof_request_handler_(presence_id);
}

void BattleNetApi::SetConversationCreateHandler(BNetConversationCreateHandler handler) {
  conversation_create_handler_ = std::move(handler);
}

void BattleNetApi::SetConversationInviteHandler(BNetConversationInviteHandler handler) {
  conversation_invite_handler_ = std::move(handler);
}

void BattleNetApi::SetConversationLeaveHandler(BNetConversationLeaveHandler handler) {
  conversation_leave_handler_ = std::move(handler);
}

void BattleNetApi::SetConversationSendHandler(BNetConversationSendHandler handler) {
  conversation_send_handler_ = std::move(handler);
}

void BattleNetApi::SetFriendNoteHandler(BNetFriendNoteHandler handler) {
  friend_note_handler_ = std::move(handler);
}

void BattleNetApi::SetRemoveFriendHandler(BNetPresenceActionHandler handler) {
  remove_friend_handler_ = std::move(handler);
}

void BattleNetApi::SetAcceptFriendInviteHandler(BNetFriendInviteActionHandler handler) {
  accept_friend_invite_handler_ = std::move(handler);
}

void BattleNetApi::SetDeclineFriendInviteHandler(BNetFriendInviteActionHandler handler) {
  decline_friend_invite_handler_ = std::move(handler);
}

void BattleNetApi::SetReportFriendInviteHandler(BNetFriendInviteActionHandler handler) {
  report_friend_invite_handler_ = std::move(handler);
}

void BattleNetApi::SetRIDFriendInviteByEmailHandler(BNetFriendInviteByEmailHandler handler) {
  friend_invite_by_email_handler_ = std::move(handler);
}

void BattleNetApi::SetRIDFriendInviteByPresenceIdHandler(
    BNetFriendInviteByPresenceIdHandler handler) {
  friend_invite_by_presence_id_handler_ = std::move(handler);
}

void BattleNetApi::SetFriendsOfFriendRequestHandler(BNetFriendsOfFriendRequestHandler handler) {
  fof_request_handler_ = std::move(handler);
}

void BattleNetApi::SetAddCIDBlockHandler(BNetCidBlockHandler handler) {
  add_cid_block_handler_ = std::move(handler);
}

void BattleNetApi::SetRemoveCIDBlockHandler(BNetCidBlockHandler handler) {
  remove_cid_block_handler_ = std::move(handler);
}

void BattleNetApi::SetAddRIDBlockHandler(BNetRidBlockHandler handler) {
  add_rid_block_handler_ = std::move(handler);
}

void BattleNetApi::SetRemoveRIDBlockHandler(BNetRidBlockHandler handler) {
  remove_rid_block_handler_ = std::move(handler);
}

void BattleNetApi::SetReportPlayerHandler(BNetReportPlayerHandler handler) {
  report_player_handler_ = std::move(handler);
}

void BattleNetApi::SetWhisperSendHandler(BNetWhisperSendHandler handler) {
  whisper_send_handler_ = std::move(handler);
}

void BattleNetApi::SetPresenceValueHandler(BNetPresenceValueSetHandler handler) {
  presence_value_set_handler_ = std::move(handler);
}

const char *BattleNetApi::GetNameForPresenceID(std::int32_t presence_id) const {
  std::int32_t lookup_presence_id = presence_id;
  const bool is_presence_friend = IsPresenceIDFriend(presence_id);
  if (is_presence_friend) {
    if (!IsFriendPresenceID(presence_id)) {
      lookup_presence_id = GetAccountPresenceId(presence_id);
    }
  }

  const char *resolved_name = kEmptyBNetName;
  if (const auto it = presence_api_names_.find(lookup_presence_id);
      it != presence_api_names_.end()) {
    resolved_name = it->second.c_str();
  }

  if (is_presence_friend) {
    if (const auto *friend_info = FindFriendRecordByPresenceID(lookup_presence_id)) {
      return friend_info->display_name;
    }
    return resolved_name;
  }

  if (IsPresenceIDSelf(presence_id)) {
    return resolved_name;
  }

  const auto cached_current_toon_name = GetCurrentToonNameForPresenceLookup();
  if (!cached_current_toon_name) {
    return resolved_name;
  }

  const auto queried_toon_name = ResolveToonNameLookupSource(presence_id);
  if (!queried_toon_name) {
    return resolved_name;
  }

  if (queried_toon_name.identity == cached_current_toon_name.identity &&
      queried_toon_name.realm_id == -1) {
    return resolved_name;
  }

  decorated_presence_name_buffer_.assign(resolved_name);
  decorated_presence_name_buffer_.push_back('*');
  return decorated_presence_name_buffer_.c_str();
}

void BattleNetApi::UpdateFriendOnlineToons() {
  if (friends_.empty())
    return;

  if (friends_.size() > 1) {
    SortFriendsForDisplayOrder();
  }

  const std::string name_format = Localization::Get().GetString("BATTLENET_NAME_FORMAT", "");
  const auto refresh_display_name = [&name_format](BNetFriendInfo &info) {
    char formatted_name[sizeof(info.display_name)]{};
    core::SStrPrintf(formatted_name, sizeof(formatted_name), name_format.c_str(),
                     info.formatted_name_left, info.formatted_name_right);
    CopyUtf8Field(info.display_name, sizeof(info.display_name), info.display_name_length,
                  info.display_name_bytes, formatted_name, kBNetDisplayNameMaxCodepoints);
  };

  std::size_t group_start = 0;
  while (group_start < friends_.size()) {
    std::size_t group_end = group_start + 1;
    while (group_end < friends_.size() &&
           CompareUtf8NameFieldsNoCase(friends_[group_start].formatted_name_left,
                                       friends_[group_end].formatted_name_left,
                                       kBNetFormattedNameMaxCodepoints) == 0 &&
           CompareUtf8NameFieldsNoCase(friends_[group_start].formatted_name_right_base,
                                       friends_[group_end].formatted_name_right_base,
                                       kBNetFormattedNameMaxCodepoints) == 0) {
      ++group_end;
    }

    for (std::size_t i = group_start; i < group_end; ++i) {
      auto &info = friends_[i];
      CopyUtf8Field(info.formatted_name_right, sizeof(info.formatted_name_right),
                    info.formatted_name_right_length, info.formatted_name_right_bytes,
                    info.formatted_name_right_base, kBNetFormattedNameMaxCodepoints);

      if (group_end - group_start > 1) {
        char suffix[16]{};
        std::snprintf(suffix, sizeof(suffix), " %zu", i - group_start + 1);
        std::string suffixed_name = info.formatted_name_right;
        suffixed_name += suffix;
        CopyUtf8Field(info.formatted_name_right, sizeof(info.formatted_name_right),
                      info.formatted_name_right_length, info.formatted_name_right_bytes,
                      suffixed_name, kBNetFormattedNameMaxCodepoints);
      }

      refresh_display_name(info);
    }

    group_start = group_end;
  }
}

void BattleNetApi::SetEventSink(BNetUiEventSink sink) {
  event_sink_ = std::move(sink);
}

void BattleNetApi::SetChatDisplayHandler(BNetChatDisplayHandler handler) {
  chat_display_handler_ = std::move(handler);
}

void BattleNetApi::DisplayChatMessage(const BNetChatDisplayRequest &request) const {
  if (chat_display_handler_) {
    chat_display_handler_(request);
  }
}

void BattleNetApi::FireEvent(std::int32_t event_id, const char *fmt, ...) {
  auto &api = Instance();
  if (!api.ui_event_dispatch_enabled_) {
    return;
  }

  const char *event_name = LookupBNetUiEventName(event_id);
  if (!event_name) {
    return;
  }

  va_list args;
  va_start(args, fmt);
  const auto event_args = BuildBNetUiEventArgs(fmt, args);
  va_end(args);

  if (auto *game_ui = openwow::ui::game::runtime::WorldUiRuntimeContext::FromActiveLua();
      game_ui != nullptr && game_ui->is_loaded()) {
    game_ui->frame_events().dispatcher().FireEventV(
        event_name, BuildGameUiEventArgs(event_args));
  }

  if (api.event_sink_) {
    api.event_sink_(event_name, event_args);
  }
}

void BattleNetApi::OnToonOnline(std::int32_t , std::int32_t ,
                                const BNetVariant *a3) {
  auto &api = Instance();
  if (!HasBNetRidTransportAccess(api))
    return;

  FireEvent(kBNetEventSelfOnline, "%u", static_cast<unsigned int>(a3->ToInt()));
}

void BattleNetApi::OnNewPresence(std::int32_t , std::int32_t ,
                                 const BNetVariant *a3) {
  auto &api = Instance();
  if (!HasBNetRidTransportAccess(api)) {
    return;
  }

  if (!BattleNetUI::IsInitialized()) {
    openwow::core::SErrFatalCondition("%s", "");
  }

  const auto presence_id = a3 ? a3->ToInt() : 0;
  if (!api.IsFriendListInitialized()) {
    return;
  }

  constexpr auto kToonNameKey = static_cast<std::int32_t>(BNetPresenceKey::kToonName);
  const auto toon_name = api.GetPresenceValue(presence_id, kToonNameKey);
  if (toon_name.type == BNetPresenceValue::Type::kToonName) {
    if (api.IsPresenceIDFriend(presence_id)) {
      FireEvent(kBNetEventFriendToonOnline, "%u", static_cast<unsigned int>(presence_id));
      return;
    }

    FireEvent(kBNetEventNewPresence, "%u%s", static_cast<unsigned int>(presence_id),
              toon_name.str_val.c_str());
    return;
  }

  FireEvent(kBNetEventNewPresence, "%u%s", static_cast<unsigned int>(presence_id),
            api.GetNameForPresenceID(presence_id));
}

void BattleNetApi::OnToonNameChanged(std::int32_t presence_id) {
  auto &api = Instance();
  if (!HasBNetRidTransportAccess(api)) {
    return;
  }

  constexpr auto kToonNameKey = static_cast<std::int32_t>(BNetPresenceKey::kToonName);
  const auto value = api.GetPresenceValue(presence_id, kToonNameKey);
  if (value.type == BNetPresenceValue::Type::kToonName) {
    FireEvent(kBNetEventToonNameUpdated, "%u%s%b", static_cast<unsigned int>(presence_id),
              value.str_val.c_str(), api.IsPresenceIDFriend(presence_id));
  }

  if (!api.IsPresenceIDSelf(presence_id)) {
    api.UpdateRecentWhispers(presence_id, 32, false, {});
  }
}

void BattleNetApi::OnFactionChanged(std::int32_t presence_id) {
  auto &api = Instance();
  if (!HasBNetRidTransportAccess(api)) {
    return;
  }

  if (!api.IsPresenceIDSelf(presence_id)) {
    api.UpdateRecentWhispers(presence_id, 32, false, {});
  }
}

void BattleNetApi::OnGenericPresenceFieldUpdated(std::int32_t , std::int32_t ,
                                                 const BNetVariant *a3) {
  auto &api = Instance();
  if (!HasBNetRidTransportAccess(api)) {
    return;
  }

  if (!BattleNetUI::IsInitialized()) {
    openwow::core::SErrFatalCondition("%s", "");
  }

  const auto presence_id = a3 ? a3->ToInt() : 0;
  if (presence_id == 0) {
    return;
  }

  const char *display_name = api.GetNameForPresenceID(presence_id);

  if (!api.IsPresenceIDFriend(presence_id) || api.IsFriendPresenceID(presence_id)) {
    if (display_name) {
      api.UpdateRecentWhispers(presence_id, 32, false, display_name);
    }
  } else {
    api.ClearRecentWhisperContextBits(presence_id, 0x7FFFFFFF);
  }

  const auto friend_index = api.FindFriendIndexByPresenceID(presence_id);
  if (friend_index >= 0) {
    FireEvent(kBNetEventFriendInfoChangedLegacy, "%d", friend_index + 1);
  }
}

void BattleNetApi::OnChatWhisperSent(std::int32_t , std::int32_t ,
                                     const BNetChatWhisperPayload *a3) {
  auto &api = Instance();
  if (!HasBNetRidTransportAccess(api) || !a3) {
    return;
  }

  DisplayBNetWhisperMessage(api, *a3, ChatDisplayType::kBnWhisperInform);
}

void BattleNetApi::OnChatWhisperReceived(std::int32_t , std::int32_t ,
                                         const BNetChatWhisperPayload *a3) {
  auto &api = Instance();
  if (!HasBNetRidTransportAccess(api) || !a3) {
    return;
  }

  DisplayBNetWhisperMessage(api, *a3, ChatDisplayType::kBnWhisper);
}

void BattleNetApi::OnChatMessage(std::int32_t , std::int32_t ,
                                 const BNetConversationMessagePayload *a3) {
  auto &api = Instance();
  if (!HasBNetRidTransportAccess(api) || !a3) {
    return;
  }

  DisplayBNetConversationMessage(api, *a3);
}

void BattleNetApi::OnOnlineTimeChanged(std::int32_t , std::int32_t ,
                                       const BNetOnlineTimeChangedPayload *a3) {
  auto &api = Instance();
  if (!HasBNetRidTransportAccess(api)) {
    return;
  }

  const std::int32_t presence_id = a3 ? a3->presence_id : 0;
  if (!a3 || !a3->has_online_time || api.IsPresenceIDSelf(presence_id)) {
    return;
  }

  const bool is_rid_friend = api.IsFriendPresenceID(presence_id);
  const bool is_cid_friend = api.IsCIDFriend(presence_id);
  if (!is_rid_friend && !is_cid_friend) {
    return;
  }

  constexpr auto kOnlineTimeKey = static_cast<std::int32_t>(BNetPresenceKey::kOnlineTime);
  const auto current_online_time = api.GetPresenceValue(presence_id, kOnlineTimeKey);
  if (current_online_time.type != BNetPresenceValue::Type::kInt32) {
    return;
  }

  const std::int32_t previous_online_time = current_online_time.int_val;
  const std::int32_t incoming_online_time = a3->online_time;
  if (previous_online_time != 0 && incoming_online_time != 0 &&
      incoming_online_time > previous_online_time) {
    if (is_rid_friend) {
      FireEvent(kBNetEventFriendAccountOnline, "%u", static_cast<unsigned int>(presence_id));
      if (GetCVarBoolOrDefault("showToastOnline", true)) {
        api.DisplayFriendListChatNotice("FRIEND_ONLINE", presence_id);
      }
    } else {
      FireEvent(kBNetEventFriendToonOnline, "%u", static_cast<unsigned int>(presence_id));
    }
  }

  FireEvent(kBNetEventFriendInfoChangedLegacy, nullptr);
}

void BattleNetApi::OnCustomMessageChanged(std::int32_t , std::int32_t ,
                                          const BNetCustomMessageChangedPayload *a3) {
  auto &api = Instance();
  if (!HasBNetRidTransportAccess(api)) {
    return;
  }

  if (!BattleNetUI::IsInitialized()) {
    openwow::core::SErrFatalCondition("%s",
        "BNPresenceCallback_CustomMessageChanged: BattleNetUI not initialized");
  }

  const std::int32_t payload_presence_id = a3 ? a3->presence_id : 0;
  const std::string_view message_text =
      (a3 && a3->has_custom_message) ? std::string_view{a3->custom_message} : std::string_view{};

  constexpr auto kOnlineTimeKey = static_cast<std::int32_t>(BNetPresenceKey::kOnlineTime);
  constexpr auto kLastOnlineKey = static_cast<std::int32_t>(BNetPresenceKey::kLastOnline);

  const auto current_toon_id = api.GetPresenceIDForCurrentToon();

  const auto online_time_val = api.GetPresenceValue(current_toon_id, kOnlineTimeKey);
  const std::int32_t current_online_time =
      (online_time_val.type == BNetPresenceValue::Type::kInt32) ? online_time_val.int_val : 0;

  const auto last_online_val = api.GetPresenceValue(payload_presence_id, kLastOnlineKey);
  const std::int32_t last_online_time =
      (last_online_val.type == BNetPresenceValue::Type::kInt32) ? last_online_val.int_val : 0;

  if (current_toon_id == payload_presence_id) {

    if (current_online_time != 0 && last_online_time >= current_online_time) {

      FireEvent(kBNetEventCustomMessageChanged, nullptr);
      const std::string sanitized =
          openwow::game::CopySanitizedBNetChatText(message_text, 509);
      api.DisplayChatMessage({
          .message = sanitized,
          .chat_type = ChatDisplayType::kBnInlineToastBroadcast,
      });
    } else {

      FireEvent(kBNetEventCustomMessageLoaded, nullptr);
    }
    return;
  }

  if (!api.IsFriendPresenceID(payload_presence_id)) {
    return;
  }

  if (current_online_time == 0) {
    return;
  }

  if (last_online_time < current_online_time) {
    return;
  }

  const auto friend_online_val = api.GetPresenceValue(payload_presence_id, kOnlineTimeKey);
  if (friend_online_val.type == BNetPresenceValue::Type::kInt32 &&
      last_online_time < friend_online_val.int_val) {
    return;
  }

  FireEvent(kBNetEventCustomMessageChanged, "%u",
            static_cast<unsigned int>(payload_presence_id));

  if (GetCVarBoolOrDefault("showToastBroadcast", true)) {
    const std::string sanitized =
        openwow::game::CopySanitizedBNetChatText(message_text, 509);
    const auto extra_data = BuildBNetChatDisplayExtraData(
        0, static_cast<std::uint32_t>(payload_presence_id), 1);
    const char *display_name = api.GetNameForPresenceID(payload_presence_id);
    api.DisplayChatMessage({
        .message = sanitized,
        .chat_type = ChatDisplayType::kBnInlineToastAlert,
        .sender_name = display_name ? std::optional<std::string>{display_name} : std::nullopt,
        .extra_data = extra_data,
    });
  }
}

void BattleNetApi::OnOnlineStatusChanged(std::int32_t , std::int32_t ,
                                         const BNetOnlineStatusChangedPayload *a3) {
  auto &api = Instance();
  if (!HasBNetRidTransportAccess(api)) {
    return;
  }

  const std::int32_t presence_id = a3 ? a3->presence_id : 0;
  const bool is_online = a3 && a3->has_online_state && a3->is_online;
  const bool is_rid_friend = api.IsFriendPresenceID(presence_id);
  const bool is_cid_friend = api.IsCIDFriend(presence_id);

  api.SetPresenceOnlineState(presence_id, is_online);
  api.UpdateFriendOnlineToons();
  if (api.GetFriendCount() > 1) {
    api.SortFriendsForEventOrder();
  }

  if (is_online) {
    api.UpdateRecentWhispers(presence_id, 32, false, {});
    FireEvent(kBNetEventFriendInfoChangedLegacy, nullptr);
    return;
  }

  api.ClearRecentWhisperContextBits(presence_id, 32);

  if (is_rid_friend) {
    FireEvent(kBNetEventFriendAccountOffline, "%u", static_cast<unsigned int>(presence_id));
    if (GetCVarBoolOrDefault("showToastOffline", true)) {
      api.DisplayFriendListChatNotice("FRIEND_OFFLINE", presence_id);
    }
  } else if (is_cid_friend) {
    FireEvent(kBNetEventFriendToonOffline, "%u", static_cast<unsigned int>(presence_id));
  }

  FireEvent(kBNetEventFriendInfoChangedLegacy, nullptr);
}

void BattleNetApi::OnHandleError(BNetErrorCode code, std::int32_t context) {
  Instance().HandleError(code, context);
}

void BattleNetApi::OnFOFInfoReceived(std::int32_t , std::int32_t ,
                                     const BNetFriendsOfFriendInfoPayload *a3) {
  auto &api = Instance();
  if (!HasBNetRidTransportAccess(api))
    return;

  const auto error_code = static_cast<BNetErrorCode>(a3 ? a3->error_code : 0);
  if (error_code != 0) {
    api.HandleError(error_code, 0);
    FireEvent(kBNetEventRequestFofFailed, nullptr);
    return;
  }

  if (a3) {
    api.SetFriendsOfFriendList(a3->source_presence_id, a3->presence_ids);
  } else {
    api.SetFriendsOfFriendList(0, {});
  }

  FireEvent(kBNetEventRequestFofSucceeded, nullptr);
}

void BattleNetApi::OnFriendListInitialized(std::int32_t , std::int32_t , void *a3) {
  auto &api = Instance();
  if (!HasBNetRidTransportAccess(api))
    return;

  const auto *payload = static_cast<const BNetFriendListInitializedPayload *>(a3);

  api.friends_.clear();
  api.rid_friend_account_presence_ids_.clear();
  if (payload) {
    for (const auto presence_id : payload->presence_ids) {
      api.SetPresenceFriendship(presence_id, true);
      api.AddFriendToList(presence_id);
    }
  }

  api.UpdateFriendOnlineToons();
  api.SortFriendsForEventOrder();
  api.friend_list_initialized_ = true;

  for (const auto &info : api.friends_) {
    FireEvent(kBNetEventFriendToonOnline, "%u", static_cast<unsigned int>(info.presence_id));
  }

  api.RefreshRecentWhisperTargets();

  FireEvent(679, nullptr);
}

void BattleNetApi::OnFriendAdded(std::int32_t , std::int32_t , const BNetVariant *a3) {
  auto &api = Instance();
  if (!HasBNetRidTransportAccess(api))
    return;

  const auto presence_id = a3 ? a3->ToInt() : 0;
  if (api.IsFriendPresenceID(presence_id)) {
    api.AddFriendToList(presence_id);
    api.UpdateFriendOnlineToons();
    if (api.friends_.size() > 1) {
      api.SortFriendsForEventOrder();
    }
    api.RefreshRecentWhisperTargets();
  }

  FireEvent(679, nullptr);
  api.DisplayFriendListChatNotice("FRIEND_ADDED", presence_id);
}

void BattleNetApi::OnFriendRemoved(std::int32_t , std::int32_t ,
                                   const BNetVariant *a3) {
  auto &api = Instance();
  if (!HasBNetRidTransportAccess(api))
    return;

  const auto presence_id = a3 ? a3->ToInt() : 0;
  const auto friend_index = api.FindFriendIndexByPresenceID(presence_id);
  if (friend_index < 0) {
    return;
  }

  api.RemoveFriendAtIndex(friend_index);
  api.ClearRecentWhisperContextBits(presence_id, 4);
  api.SetPresenceFriendship(presence_id, false);
  api.UpdateFriendOnlineToons();
  if (api.friends_.size() > 1) {
    api.SortFriendsForEventOrder();
  }
  api.RefreshRecentWhisperTargets();

  FireEvent(679, nullptr);
  api.DisplayFriendListChatNotice("FRIEND_REMOVED", presence_id);
}

void BattleNetApi::ResetFriendListStateForGlueTransition() {
  friend_list_initialized_ = false;
  friends_.clear();
  cached_current_toon_name_for_lookup_valid_ = false;
  cached_current_toon_name_for_lookup_identity_ = nullptr;
  cached_current_toon_name_for_lookup_.clear();
  decorated_presence_name_buffer_.clear();
}

void BattleNetApi::ClearUiSocialCaches() {
  friend_invites_.clear();
  fof_source_presence_id_ = 0;
  fof_presence_ids_.clear();
}

void BattleNetApi::Clear() {
  connection_state_ = BNetConnectionState::kDisconnected;
  current_account_presence_id_ = 0;
  current_toon_presence_id_ = 0;
  rid_enabled_ = false;
  dispatcher_available_ = false;
  friend_list_initialized_ = false;
  friends_.clear();
  cid_blocked_presence_ids_.clear();
  rid_friend_account_presence_ids_.clear();
  cid_friend_presence_ids_.clear();
  ClearUiSocialCaches();
  block_list_ = {};
  selected_block_presence_id_ = 0;
  selected_toon_block_presence_id_ = 0;
  selected_friend_presence_id_ = 0;
  account_name_format_.clear();
  settings_.clear();
  presence_api_names_.clear();
  error_string_responses_.clear();
  presence_records_.clear();
  account_toon_infos_.clear();
  toon_account_presence_ids_.clear();
  presence_values_.clear();
  cached_current_toon_name_for_lookup_valid_ = false;
  cached_current_toon_name_for_lookup_identity_ = nullptr;
  cached_current_toon_name_for_lookup_.clear();
  decorated_presence_name_buffer_.clear();
  conversations_ = {};
  ClearRecentWhispers();
  next_registration_handle_ = 1;
  event_bindings_.clear();
  presence_bindings_.clear();
  conversation_create_handler_ = {};
  conversation_invite_handler_ = {};
  conversation_leave_handler_ = {};
  conversation_send_handler_ = {};
  friend_note_handler_ = {};
  remove_friend_handler_ = {};
  accept_friend_invite_handler_ = {};
  decline_friend_invite_handler_ = {};
  report_friend_invite_handler_ = {};
  friend_invite_by_email_handler_ = {};
  friend_invite_by_presence_id_handler_ = {};
  fof_request_handler_ = {};
  add_cid_block_handler_ = {};
  remove_cid_block_handler_ = {};
  add_rid_block_handler_ = {};
  remove_rid_block_handler_ = {};
  report_player_handler_ = {};
  whisper_send_handler_ = {};
  presence_value_set_handler_ = {};
  event_sink_ = {};
  chat_display_handler_ = {};
  ui_event_dispatch_enabled_ = false;
}

const BNetPresenceRecord *BattleNetApi::FindPresenceRecord(std::int32_t presence_id) const {
  const auto it = presence_records_.find(presence_id);
  if (it == presence_records_.end()) {
    return nullptr;
  }
  return &it->second;
}

BattleNetApi::ToonNameLookupSource
BattleNetApi::ResolveToonNameLookupSource(std::int32_t presence_id) const {
  const auto stored_key = MakePresenceValueStorageKey(
      presence_id, static_cast<std::int32_t>(BNetPresenceKey::kToonName));
  if (const auto it = presence_values_.find(stored_key); it != presence_values_.end()) {
    if (it->second.type == BNetPresenceValue::Type::kString ||
        it->second.type == BNetPresenceValue::Type::kStringPtr ||
        it->second.type == BNetPresenceValue::Type::kToonName) {
      return {
          .text = it->second.str_val.c_str(),
          .identity = &it->second.str_val,
          .realm_id = it->second.aux_int,
      };
    }
  }

  if (const auto *record = FindPresenceRecord(presence_id)) {
    if (!record->formatted_name_right.empty()) {
      return {
          .text = record->formatted_name_right.c_str(),
          .identity = &record->formatted_name_right,
          .realm_id = record->toon_name_realm_id,
      };
    }

    if (!record->formatted_name_right_base.empty()) {
      return {
          .text = record->formatted_name_right_base.c_str(),
          .identity = &record->formatted_name_right_base,
          .realm_id = record->toon_name_realm_id,
      };
    }
  }

  return {};
}

BattleNetApi::ToonNameLookupSource BattleNetApi::GetCurrentToonNameForPresenceLookup() const {
  if (cached_current_toon_name_for_lookup_valid_) {
    return {
        .text = cached_current_toon_name_for_lookup_.c_str(),
        .identity = cached_current_toon_name_for_lookup_identity_,
        .realm_id = -1,
    };
  }

  const auto current_toon_presence_id = GetPresenceIDForCurrentToon();
  if (current_toon_presence_id == 0) {
    return {};
  }

  const auto toon_name = ResolveToonNameLookupSource(current_toon_presence_id);
  if (!toon_name) {
    return {};
  }

  cached_current_toon_name_for_lookup_ = toon_name.text;
  cached_current_toon_name_for_lookup_identity_ = toon_name.identity;
  cached_current_toon_name_for_lookup_valid_ = true;
  return {
      .text = cached_current_toon_name_for_lookup_.c_str(),
      .identity = cached_current_toon_name_for_lookup_identity_,
      .realm_id = toon_name.realm_id,
  };
}

std::optional<BNetPresenceValue> BattleNetApi::FindStoredPresenceValue(std::int32_t presence_id,
                                                                       std::int32_t key) const {
  const auto it = presence_values_.find(MakePresenceValueStorageKey(presence_id, key));
  if (it == presence_values_.end()) {
    return std::nullopt;
  }
  return it->second;
}

bool BattleNetApi::IsPresenceOnline(std::int32_t presence_id) const {
  if (const auto *record = FindPresenceRecord(presence_id)) {
    return record->online;
  }
  return false;
}

std::int32_t BattleNetApi::NormalizeRecentWhisperPresenceId(std::int32_t presence_id) const {
  if (presence_id == 0) {
    return 0;
  }

  if (IsPresenceIDFriend(presence_id) && !IsFriendPresenceID(presence_id)) {
    return GetAccountPresenceId(presence_id);
  }

  return presence_id;
}

void BattleNetApi::ClearRecentWhisperContextBits(std::int32_t presence_id,
                                                 std::uint32_t context_bits) {
  const auto normalized_presence_id = NormalizeRecentWhisperPresenceId(presence_id);
  if (normalized_presence_id == 0) {
    return;
  }
  openwow::ui::game::AutoComplete::Get().ClearRecentPresenceIdContextBits(
      normalized_presence_id, context_bits);
}

void BattleNetApi::SetPresenceOnlineState(std::int32_t presence_id, bool is_online) {
  if (presence_id == 0) {
    return;
  }

  presence_records_[presence_id].online = is_online;
}

std::int32_t BattleNetApi::FindFriendInviteIndexByPresenceID(std::int32_t presence_id) const {
  const auto it = std::find_if(friend_invites_.begin(), friend_invites_.end(),
                               [presence_id](const BNetFriendInvite &invite) {
                                 return invite.presence_id == presence_id;
                               });
  if (it == friend_invites_.end()) {
    return -1;
  }

  return static_cast<std::int32_t>(std::distance(friend_invites_.begin(), it));
}

void BattleNetApi::RemoveFriendAtIndex(std::int32_t index) {
  EraseVectorEntryOrFatal(friends_, index);
}

void BattleNetApi::RemoveFriendInviteAtIndex(std::int32_t index) {
  EraseVectorEntryOrFatal(friend_invites_, index);
}

void BattleNetApi::ApplyPresenceRecordToFriend(BNetFriendInfo &info,
                                               const BNetPresenceRecord &record) {
  info.game_account = record.game_account;
  CopyUtf8Field(info.formatted_name_left, sizeof(info.formatted_name_left),
                info.formatted_name_left_length, info.formatted_name_left_bytes,
                record.formatted_name_left, kBNetFormattedNameMaxCodepoints);
  CopyUtf8Field(info.formatted_name_right, sizeof(info.formatted_name_right),
                info.formatted_name_right_length, info.formatted_name_right_bytes,
                record.formatted_name_right, kBNetFormattedNameMaxCodepoints);
  CopyUtf8Field(info.formatted_name_right_base, sizeof(info.formatted_name_right_base),
                info.formatted_name_right_base_length, info.formatted_name_right_base_bytes,
                record.formatted_name_right_base, kBNetFormattedNameMaxCodepoints);
  CopyUtf8Field(info.display_name, sizeof(info.display_name), info.display_name_length,
                info.display_name_bytes, record.display_name, kBNetDisplayNameMaxCodepoints);
}

void BattleNetApi::RefreshFriendDisplayName(BNetFriendInfo &info) {
  if (!account_name_format_.empty() &&
      BN_CountFormatSpecifiers(account_name_format_.c_str()) == 2) {
    char formatted_name[sizeof(info.display_name)]{};
    core::SStrPrintf(formatted_name, sizeof(formatted_name), account_name_format_.c_str(),
                     info.formatted_name_left, info.formatted_name_right);
    CopyUtf8Field(info.display_name, sizeof(info.display_name), info.display_name_length,
                  info.display_name_bytes, formatted_name, kBNetDisplayNameMaxCodepoints);
    return;
  }

  if (const auto *record = FindPresenceRecord(info.presence_id);
      record && !record->display_name.empty()) {
    CopyUtf8Field(info.display_name, sizeof(info.display_name), info.display_name_length,
                  info.display_name_bytes, record->display_name, kBNetDisplayNameMaxCodepoints);
    return;
  }

  CopyUtf8Field(info.display_name, sizeof(info.display_name), info.display_name_length,
                info.display_name_bytes, std::string_view{}, kBNetDisplayNameMaxCodepoints);
}

void BattleNetApi::RefreshRecentWhisperTargets() {
  for (const auto &info : friends_) {
    const char *display_name = GetNameForPresenceID(info.presence_id);
    UpdateRecentWhispers(info.presence_id, IsPresenceOnline(info.presence_id) ? 36 : 4, false,
                         display_name ? display_name : "");
  }

  for (const auto &conversation : conversations_) {
    for (const auto member_presence_id : conversation.member_presence_ids) {
      const char *display_name = GetNameForPresenceID(member_presence_id);
      UpdateRecentWhispers(member_presence_id, 48, true, display_name ? display_name : "");
    }
  }
}

void BattleNetApi::DisplayFriendListChatNotice(const char *message_key,
                                               std::int32_t presence_id) const {
  const char *display_name = GetNameForPresenceID(presence_id);
  DisplayChatMessage({
      .message = message_key,
      .chat_type = ChatDisplayType::kBnConversationNotice,
      .sender_name = display_name ? std::optional<std::string>{display_name} : std::nullopt,
  });
}

void BattleNetApi::SortFriendsForDisplayOrder() {
  std::sort(friends_.begin(), friends_.end(),
            [](const BNetFriendInfo &left, const BNetFriendInfo &right) {
              const int left_compare =
                  CompareUtf8NameFieldsNoCase(left.formatted_name_left,
                                              right.formatted_name_left,
                                              kBNetFormattedNameMaxCodepoints);
              if (left_compare != 0) {
                return left_compare < 0;
              }

              const int right_compare =
                  CompareUtf8NameFieldsNoCase(left.formatted_name_right_base,
                                              right.formatted_name_right_base,
                                              kBNetFormattedNameMaxCodepoints);
              if (right_compare != 0) {
                return right_compare < 0;
              }

              return left.game_account < right.game_account;
            });
}

void BattleNetApi::SortFriendsForEventOrder() {
  std::sort(friends_.begin(), friends_.end(),
            [this](const BNetFriendInfo &left, const BNetFriendInfo &right) {
              const bool left_online = IsPresenceOnline(left.presence_id);
              const bool right_online = IsPresenceOnline(right.presence_id);
              if (left_online != right_online) {
                return left_online && !right_online;
              }

              return core::SStrCmpUTF8NoCase(left.display_name, right.display_name,
                                             kBNetDisplayNameMaxCodepoints) < 0;
            });
}

}

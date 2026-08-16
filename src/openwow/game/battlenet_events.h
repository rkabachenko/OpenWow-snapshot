
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace openwow::game {

struct BNetFriendInvite;
struct BNetVariant;

enum class BNetUIEvent : std::uint32_t {
  kConversationJoined = 690,
  kConversationLeft = 691,
  kConversationClosed = 692,
  kConversationMessageUndeliverable = 696,
  kConversationMessageBlocked = 697,
  kConversationMemberJoined = 698,
  kConversationMemberLeft = 699,
  kConversationMemberUpdated = 700,
  kFriendListSizeChanged = 679,
  kFriendInviteAdded = 682,
  kFriendInviteRemoved = 683,
  kChatWhisperUndeliverable = 689,
  kConversationCreateSucceeded = 701,
  kConversationCreateFailed = 702,
  kConversationInviteSucceeded = 703,
  kConversationInviteFailed = 704,
  kBlockListUpdated = 705,
  kSystemMessage = 706,
  kMatureLanguageFilter = 715,
};

enum class BNetUiEventCallbackId : std::uint8_t {
  kNewPresence,
  kToonOnline,
  kToonOffline,
  kFriendListInitialized,
  kFriendAdded,
  kFriendRemoved,
  kInviteListInitialized,
  kInviteAdded,
  kInviteRemoved,
  kChatWhisperSent,
  kChatWhisperReceived,
  kChatWhisperUndeliverable,
  kChannelJoined,
  kChannelLeft,
  kChannelClosed,
  kChatMessage,
  kChatMessageUndeliverable,
  kChatMessageBlocked,
  kChannelMemberJoined,
  kChannelMemberLeft,
  kChannelMemberUpdated,
  kBlockListInitialized,
  kBlockAdded,
  kBlockRemoved,
  kSystemMessage,
  kSettingChanged,
};

enum class BNetUiPresenceCallbackId : std::uint8_t {
  kOnlineStatusChanged,
  kOnlineTimeChanged,
  kLastOnlineChanged,
  kFriendInfoChanged,
  kCustomMessageChanged,
  kUnused1000D,
  kToonNameChanged,
  kUnused1000B,
  kFactionChanged,
};

enum class BNetUiSpecialCallbackId : std::uint8_t {
  kConversationCreated,
  kConversationInvite,
  kFofInfoReceived,
};

class BattleNetUI {
public:

  static void Init();

  static void OnFriendRemoved(std::int32_t presence_id);

  static void OnFriendInviteAdded(BNetFriendInvite invite);

  static void OnFriendInviteRemoved(std::int32_t presence_id);

  static void OnFriendInviteSendResult(const BNetVariant &result);

  static void OnChatWhisperUndeliverable(std::int32_t presence_id, std::uint16_t error_code);

  static void OnBlockListUpdated();

  static void OnSystemMessage(std::uint16_t message_id, std::uint32_t amount);

  static void OnSettingChanged(std::string_view setting_name, bool enabled);

  static void OnConversationMessageUndeliverable(std::uint8_t channel_index,
                                                 std::uint16_t error_code);
  static void OnConversationMessageBlocked(std::uint8_t channel_index,
                                           std::int32_t presence_id);
  static void OnConversationMemberUpdated(std::uint8_t channel_index,
                                          std::int32_t presence_id);
  static void OnConversationMemberJoined(std::uint8_t channel_index,
                                         std::int32_t presence_id);
  static void OnConversationMemberLeft(std::uint8_t channel_index,
                                       std::int32_t presence_id);
  static void OnConversationJoined(std::uint8_t channel_index, std::int32_t channel_type);
  static void OnConversationLeft(std::uint8_t channel_index);
  static void OnConversationClosed(std::uint8_t channel_index, std::int32_t presence_id);

  static void OnConversationCreated(std::int32_t callback_id, std::int32_t arg_count,
                                    const BNetVariant *args);

  static void OnConversationInvite(std::int32_t callback_id, std::int32_t arg_count,
                                   const BNetVariant *args);

  static void ApplyNameFormat();

  [[nodiscard]] static bool IsInitialized();
  [[nodiscard]] static std::size_t GetRegisteredEventCount();
  [[nodiscard]] static std::size_t GetRegisteredPresenceUpdateCount();
  [[nodiscard]] static bool HasRegisteredEvent(std::string_view event_name,
                                               BNetUiEventCallbackId callback_id);
  [[nodiscard]] static bool HasRegisteredPresenceUpdate(std::int32_t target_id,
                                                        std::uint32_t packed_key,
                                                        BNetUiPresenceCallbackId callback_id);
  [[nodiscard]] static bool HasSpecialCallback(BNetUiSpecialCallbackId callback_id);

  static void Shutdown();
};

}

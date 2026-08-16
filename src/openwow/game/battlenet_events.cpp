
#include "openwow/game/battlenet_events.h"

#include "openwow/core/storm_error.h"
#include "openwow/game/battlenet_api.h"
#include "openwow/game/battlenet_login.h"
#include "openwow/game/chat_display.h"
#include "openwow/game/localization.h"
#include "openwow/net/client_services.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"

#include <array>
#include <string>
#include <vector>

namespace openwow::game {

namespace {

struct InstalledBNetUiEventRegistration {
  std::int32_t handle{0};
  std::string_view event_name;
  BNetUiEventCallbackId callback_id{BNetUiEventCallbackId::kNewPresence};
};

struct InstalledBNetUiPresenceRegistration {
  std::int32_t handle{0};
  std::int32_t target_id{0};
  std::uint32_t packed_key{0};
  BNetUiPresenceCallbackId callback_id{BNetUiPresenceCallbackId::kOnlineStatusChanged};
};

struct RawBNetUiEventRegistration {
  std::string_view event_name;
  BNetUiEventCallbackId callback_id;
};

struct RawBNetUiPresenceRegistration {
  std::int32_t target_id;
  std::uint32_t packed_key;
  BNetUiPresenceCallbackId callback_id;
};

struct BattleNetUiInitState {
  bool initialized{false};
  std::vector<InstalledBNetUiEventRegistration> event_registrations;
  std::vector<InstalledBNetUiPresenceRegistration> presence_registrations;
  std::array<bool, 3> special_callbacks{};
};

constexpr std::array<RawBNetUiEventRegistration, 26> kBNetUiEventRegistrations{{
    {"BattlenetEvent_NewPresence", BNetUiEventCallbackId::kNewPresence},
    {"BattlenetEvent_ToonOnline", BNetUiEventCallbackId::kToonOnline},
    {"BattlenetEvent_ToonOffine", BNetUiEventCallbackId::kToonOffline},
    {"BattlenetEvent_FriendListInitialized", BNetUiEventCallbackId::kFriendListInitialized},
    {"BattlenetEvent_FriendAdded", BNetUiEventCallbackId::kFriendAdded},
    {"BattlenetEvent_FriendRemoved", BNetUiEventCallbackId::kFriendRemoved},
    {"BattlenetEvent_InviteListInitialized", BNetUiEventCallbackId::kInviteListInitialized},
    {"BattlenetEvent_InviteAdded", BNetUiEventCallbackId::kInviteAdded},
    {"BattlenetEvent_InviteRemoved", BNetUiEventCallbackId::kInviteRemoved},
    {"BattlenetEvent_ChatWhisperSent", BNetUiEventCallbackId::kChatWhisperSent},
    {"BattlenetEvent_ChatWhisperReceived", BNetUiEventCallbackId::kChatWhisperReceived},
    {"BattlenetEvent_ChatWhisperUndeliverable",
     BNetUiEventCallbackId::kChatWhisperUndeliverable},
    {"BattlenetEvent_ChannelJoined", BNetUiEventCallbackId::kChannelJoined},
    {"BattlenetEvent_ChannelLeft", BNetUiEventCallbackId::kChannelLeft},
    {"BattlenetEvent_ChannelClosed", BNetUiEventCallbackId::kChannelClosed},
    {"BattlenetEvent_ChatMessage", BNetUiEventCallbackId::kChatMessage},
    {"BattlenetEvent_ChatMessageUndeliverable",
     BNetUiEventCallbackId::kChatMessageUndeliverable},
    {"BattlenetEvent_ChatMessageBlocked", BNetUiEventCallbackId::kChatMessageBlocked},
    {"BattlenetEvent_ChannelMemberJoined", BNetUiEventCallbackId::kChannelMemberJoined},
    {"BattlenetEvent_ChannelMemberLeft", BNetUiEventCallbackId::kChannelMemberLeft},
    {"BattlenetEvent_ChannelMemberUpdated", BNetUiEventCallbackId::kChannelMemberUpdated},
    {"BattlenetEvent_BlockListInitialized", BNetUiEventCallbackId::kBlockListInitialized},
    {"BattlenetEvent_BlockAdded", BNetUiEventCallbackId::kBlockAdded},
    {"BattlenetEvent_BlockRemoved", BNetUiEventCallbackId::kBlockRemoved},
    {"BattlenetEvent_SystemMessage", BNetUiEventCallbackId::kSystemMessage},
    {"BattlenetEvent_SettingChanged", BNetUiEventCallbackId::kSettingChanged},
}};

constexpr std::array<RawBNetUiPresenceRegistration, 21> kBNetUiPresenceRegistrations{{
    {0, 0x00010002u, BNetUiPresenceCallbackId::kOnlineStatusChanged},
    {0, 0x00010003u, BNetUiPresenceCallbackId::kOnlineTimeChanged},
    {0, 0x00010007u, BNetUiPresenceCallbackId::kLastOnlineChanged},
    {0, 0x00010010u, BNetUiPresenceCallbackId::kFriendInfoChanged},
    {0, 0x00010011u, BNetUiPresenceCallbackId::kFriendInfoChanged},
    {0, 0x0001000Cu, BNetUiPresenceCallbackId::kCustomMessageChanged},
    {0, 0x0001000Du, BNetUiPresenceCallbackId::kUnused1000D},
    {0, 0x0001000Eu, BNetUiPresenceCallbackId::kToonNameChanged},
    {0, 0x00010005u, BNetUiPresenceCallbackId::kFriendInfoChanged},
    {0, 0x00010004u, BNetUiPresenceCallbackId::kFriendInfoChanged},
    {0, 0x00010009u, BNetUiPresenceCallbackId::kFriendInfoChanged},
    {0, 0x00010008u, BNetUiPresenceCallbackId::kFriendInfoChanged},
    {0, 0x00030001u, BNetUiPresenceCallbackId::kFriendInfoChanged},
    {0, 0x00030002u, BNetUiPresenceCallbackId::kFriendInfoChanged},
    {0, 0x00030003u, BNetUiPresenceCallbackId::kFriendInfoChanged},
    {0, 0x00030004u, BNetUiPresenceCallbackId::kFriendInfoChanged},
    {0, 0x00030005u, BNetUiPresenceCallbackId::kFriendInfoChanged},
    {0, 0x00030006u, BNetUiPresenceCallbackId::kFriendInfoChanged},
    {0, 0x0001000Au, BNetUiPresenceCallbackId::kFriendInfoChanged},
    {0, 0x0001000Bu, BNetUiPresenceCallbackId::kUnused1000B},
    {0, 0x00040001u, BNetUiPresenceCallbackId::kFactionChanged},
}};

BattleNetUiInitState &GetBattleNetUiInitState() {
  static BattleNetUiInitState state;
  return state;
}

constexpr std::size_t ToIndex(BNetUiSpecialCallbackId callback_id) {
  return static_cast<std::size_t>(callback_id);
}

constexpr std::string_view ToHandlerLabel(BNetUiEventCallbackId callback_id) {
  switch (callback_id) {
  case BNetUiEventCallbackId::kNewPresence:
    return "BNetUiEventCallback::NewPresence";
  case BNetUiEventCallbackId::kToonOnline:
    return "BNetUiEventCallback::ToonOnline";
  case BNetUiEventCallbackId::kToonOffline:
    return "BNetUiEventCallback::ToonOffline";
  case BNetUiEventCallbackId::kFriendListInitialized:
    return "BNetUiEventCallback::FriendListInitialized";
  case BNetUiEventCallbackId::kFriendAdded:
    return "BNetUiEventCallback::FriendAdded";
  case BNetUiEventCallbackId::kFriendRemoved:
    return "BNetUiEventCallback::FriendRemoved";
  case BNetUiEventCallbackId::kInviteListInitialized:
    return "BNetUiEventCallback::InviteListInitialized";
  case BNetUiEventCallbackId::kInviteAdded:
    return "BNetUiEventCallback::InviteAdded";
  case BNetUiEventCallbackId::kInviteRemoved:
    return "BNetUiEventCallback::InviteRemoved";
  case BNetUiEventCallbackId::kChatWhisperSent:
    return "BNetUiEventCallback::ChatWhisperSent";
  case BNetUiEventCallbackId::kChatWhisperReceived:
    return "BNetUiEventCallback::ChatWhisperReceived";
  case BNetUiEventCallbackId::kChatWhisperUndeliverable:
    return "BNetUiEventCallback::ChatWhisperUndeliverable";
  case BNetUiEventCallbackId::kChannelJoined:
    return "BNetUiEventCallback::ChannelJoined";
  case BNetUiEventCallbackId::kChannelLeft:
    return "BNetUiEventCallback::ChannelLeft";
  case BNetUiEventCallbackId::kChannelClosed:
    return "BNetUiEventCallback::ChannelClosed";
  case BNetUiEventCallbackId::kChatMessage:
    return "BNetUiEventCallback::ChatMessage";
  case BNetUiEventCallbackId::kChatMessageUndeliverable:
    return "BNetUiEventCallback::ChatMessageUndeliverable";
  case BNetUiEventCallbackId::kChatMessageBlocked:
    return "BNetUiEventCallback::ChatMessageBlocked";
  case BNetUiEventCallbackId::kChannelMemberJoined:
    return "BNetUiEventCallback::ChannelMemberJoined";
  case BNetUiEventCallbackId::kChannelMemberLeft:
    return "BNetUiEventCallback::ChannelMemberLeft";
  case BNetUiEventCallbackId::kChannelMemberUpdated:
    return "BNetUiEventCallback::ChannelMemberUpdated";
  case BNetUiEventCallbackId::kBlockListInitialized:
    return "BNetUiEventCallback::BlockListInitialized";
  case BNetUiEventCallbackId::kBlockAdded:
    return "BNetUiEventCallback::BlockAdded";
  case BNetUiEventCallbackId::kBlockRemoved:
    return "BNetUiEventCallback::BlockRemoved";
  case BNetUiEventCallbackId::kSystemMessage:
    return "BNetUiEventCallback::SystemMessage";
  case BNetUiEventCallbackId::kSettingChanged:
    return "BNetUiEventCallback::SettingChanged";
  }

  return "BNetUiEventCallback::Unknown";
}

constexpr std::string_view ToHandlerLabel(BNetUiPresenceCallbackId callback_id) {
  switch (callback_id) {
  case BNetUiPresenceCallbackId::kOnlineStatusChanged:
    return "BNetUiPresenceCallback::OnlineStatusChanged";
  case BNetUiPresenceCallbackId::kOnlineTimeChanged:
    return "BNetUiPresenceCallback::OnlineTimeChanged";
  case BNetUiPresenceCallbackId::kLastOnlineChanged:
    return "BNetUiPresenceCallback::LastOnlineChanged";
  case BNetUiPresenceCallbackId::kFriendInfoChanged:
    return "BNetUiPresenceCallback::FriendInfoChanged";
  case BNetUiPresenceCallbackId::kCustomMessageChanged:
    return "BNetUiPresenceCallback::CustomMessageChanged";
  case BNetUiPresenceCallbackId::kUnused1000D:
    return "BNetUiPresenceCallback::Unused1000D";
  case BNetUiPresenceCallbackId::kToonNameChanged:
    return "BNetUiPresenceCallback::ToonNameChanged";
  case BNetUiPresenceCallbackId::kUnused1000B:
    return "BNetUiPresenceCallback::Unused1000B";
  case BNetUiPresenceCallbackId::kFactionChanged:
    return "BNetUiPresenceCallback::FactionChanged";
  }

  return "BNetUiPresenceCallback::Unknown";
}

bool HasConversationCallbackAccess() {
  auto &api = BattleNetApi::Instance();
  const auto &client_services = openwow::net::ClientServices::Instance();
  return client_services.HasBattleNetRidTransport() && api.IsRIDEnabled();
}

bool HasRidTransportCallbackAccess() {
  auto &api = BattleNetApi::Instance();
  const auto &client_services = openwow::net::ClientServices::Instance();
  return client_services.HasBattleNetRidTransport() && api.IsRIDEnabled();
}

bool CanHandleFriendInviteCallbacks() {
  return HasRidTransportCallbackAccess() && BattleNetUI::IsInitialized();
}

bool HasFriendInviteSendResultAccess() {
  auto &api = BattleNetApi::Instance();
  if (!api.IsRIDEnabled()) {
    return false;
  }

  const auto *login = openwow::net::ClientServices::Instance().GetBattlenetLogin();
  return login != nullptr && login->login_state() == 1 && !login->HasRidFeatureBlockFlag();
}

std::uint16_t ReadFriendInviteSendResultCode(const BNetVariant &result) {
  return static_cast<std::uint16_t>(result.ToInt());
}

int ResolveFriendInviteSendResultMessageId(const std::uint16_t error_code) {
  switch (error_code) {
  case 604:
    return 726;
  case 617:
    return 725;
  case 623:
    return 727;
  default:
    return 728;
  }
}

void DisplayFriendInviteChatNotice(const char *message_key) {
  BattleNetApi::Instance().DisplayChatMessage({
      .message = message_key,
      .chat_type = ChatDisplayType::kBnConversationNotice,
  });
}

void ValidateConversationChannelOrFatal(const std::uint8_t channel_index,
                                        const char *message) {
  if (channel_index >= kBNetConversationChannelCount) {
    openwow::core::SErrFatalCondition("%s", message);
  }
}

void DisplayConversationNotice(const char *message_key, const std::uint8_t channel_index,
                               const std::int32_t presence_id) {
  auto &api = BattleNetApi::Instance();
  api.DisplayChatMessage({
      .message = message_key,
      .chat_type = ChatDisplayType::kBnConversationNotice,
      .sender_name = api.GetNameForPresenceID(presence_id),
      .extra_data = BuildBNetChatDisplayExtraData(
          channel_index, static_cast<std::uint32_t>(presence_id), 0),
  });
}

void DisplayJoinedConversationMembers(const std::uint8_t channel_index) {
  auto &api = BattleNetApi::Instance();
  const auto current_account = api.GetPresenceIDForCurrentAccount();
  const auto current_toon = api.GetPresenceIDForCurrentToon();
  const std::string delimiter = Localization::Get().GetString("PLAYER_LIST_DELIMITER", "");

  std::string members_text;
  for (const auto presence_id : api.GetConversationMemberList(channel_index)) {
    if (presence_id == current_account || presence_id == current_toon) {
      continue;
    }
    if (!members_text.empty()) {
      members_text += delimiter;
    }
    members_text += api.GetNameForPresenceID(presence_id);
  }

  api.DisplayChatMessage({
      .message = members_text,
      .chat_type = ChatDisplayType::kBnInlineToastConversation,
      .sender_name = api.GetNameForPresenceID(current_account),
      .extra_data = BuildBNetChatDisplayExtraData(
          channel_index, static_cast<std::uint32_t>(current_account), 0),
  });
}

bool CanApplyBattleNetNameFormat() {
  const auto &client_services = openwow::net::ClientServices::Instance();
  if (!client_services.IsBNLogin()) {
    return false;
  }

  const auto &api = BattleNetApi::Instance();
  return api.IsConnectedState() && api.IsRIDEnabled();
}

std::int32_t ResolveConversationPresenceId(const BNetVariant &context) {
  switch (context.type) {
  case BNetVariantType::kInt32:
  case BNetVariantType::kPresenceId:
    return context.ToInt();
  case BNetVariantType::kRefCounted:

    return context.int_val;
  default:
    return 0;
  }
}

std::uint8_t ReadConversationChannelOrFatal(const BNetVariant &variant, const char *message) {
  const auto channel_index = static_cast<std::uint8_t>(variant.ToInt());
  if (channel_index >= kBNetConversationChannelCount) {
    openwow::core::SErrFatalCondition("%s", message);
  }

  return channel_index;
}

}

void BattleNetUI::Init() {
  auto &state = GetBattleNetUiInitState();
  auto &api = BattleNetApi::Instance();
  if (!openwow::net::ClientServices::Instance().IsBNLogin()) {
    return;
  }

  if (state.initialized || !state.event_registrations.empty() ||
      !state.presence_registrations.empty()) {
    openwow::core::SErrFatalCondition("%s", "BattleNetUI::Init called twice without Shutdown");
  }

  state.event_registrations.reserve(kBNetUiEventRegistrations.size());
  for (const auto &registration : kBNetUiEventRegistrations) {
    const auto handle =
        api.RegisterEvent(registration.event_name, ToHandlerLabel(registration.callback_id));
    if (handle == 0) {
      Shutdown();
      return;
    }

    state.event_registrations.push_back(InstalledBNetUiEventRegistration{
        handle, registration.event_name, registration.callback_id});
  }

  state.presence_registrations.reserve(kBNetUiPresenceRegistrations.size());
  for (const auto &registration : kBNetUiPresenceRegistrations) {
    const auto handle = api.RegisterPresenceUpdateCallback(
        registration.target_id, static_cast<std::int32_t>(registration.packed_key),
        ToHandlerLabel(registration.callback_id));
    if (handle == 0) {
      Shutdown();
      return;
    }

    state.presence_registrations.push_back(InstalledBNetUiPresenceRegistration{
        handle, registration.target_id, registration.packed_key,
        registration.callback_id});
  }

  state.special_callbacks[ToIndex(BNetUiSpecialCallbackId::kConversationCreated)] = true;
  state.special_callbacks[ToIndex(BNetUiSpecialCallbackId::kConversationInvite)] = true;
  state.special_callbacks[ToIndex(BNetUiSpecialCallbackId::kFofInfoReceived)] = true;
  state.initialized = true;
}

void BattleNetUI::OnFriendRemoved(std::int32_t presence_id) {
  const BNetVariant variant = BNetVariant::PresenceId(presence_id);
  BattleNetApi::OnFriendRemoved(0, 0, &variant);
}

void BattleNetUI::OnFriendInviteAdded(BNetFriendInvite invite) {
  if (!CanHandleFriendInviteCallbacks() || invite.presence_id == 0) {
    return;
  }

  auto &api = BattleNetApi::Instance();
  const auto invite_id = static_cast<unsigned int>(invite.presence_id);
  bool should_notify = false;
  const auto current_toon_presence_id = api.GetPresenceIDForCurrentToon();
  if (current_toon_presence_id != 0) {
    const auto current_toon_online_time = api.ResolvePresenceValueSnapshot(
        current_toon_presence_id, static_cast<std::int32_t>(BNetPresenceKey::kOnlineTime));
    should_notify = current_toon_online_time.type == BNetPresenceValue::Type::kInt32 &&
                    invite.timestamp >= current_toon_online_time.int_val;
  }
  api.AppendFriendInviteOrFatal(std::move(invite));
  if (should_notify) {
    BattleNetApi::FireEvent(static_cast<std::int32_t>(BNetUIEvent::kFriendInviteAdded), "%u",
                            invite_id);
    DisplayFriendInviteChatNotice("FRIEND_REQUEST");
  }
}

void BattleNetUI::OnFriendInviteRemoved(std::int32_t presence_id) {
  if (!CanHandleFriendInviteCallbacks() || presence_id == 0) {
    return;
  }

  auto &api = BattleNetApi::Instance();
  const auto invite_index = api.FindFriendInviteIndexByPresenceID(presence_id);
  if (invite_index < 0) {
    return;
  }

  api.RemoveFriendInviteAtIndex(invite_index);
  BattleNetApi::FireEvent(static_cast<std::int32_t>(BNetUIEvent::kFriendInviteRemoved), "%d%u",
                          invite_index, static_cast<unsigned int>(presence_id));
}

void BattleNetUI::OnFriendInviteSendResult(const BNetVariant &result) {
  if (!HasFriendInviteSendResultAccess()) {
    return;
  }

  const auto error_code = ReadFriendInviteSendResultCode(result);
  openwow::ui::game::DisplaySystemMessage(ResolveFriendInviteSendResultMessageId(error_code));
}

void BattleNetUI::OnChatWhisperUndeliverable(std::int32_t presence_id, std::uint16_t error_code) {
  if (!HasRidTransportCallbackAccess()) {
    return;
  }

  auto &api = BattleNetApi::Instance();
  api.HandleError(error_code, presence_id);
  BattleNetApi::FireEvent(static_cast<std::int32_t>(BNetUIEvent::kChatWhisperUndeliverable), "%u",
                          static_cast<unsigned int>(presence_id));
}

void BattleNetUI::OnBlockListUpdated() {
  if (!HasRidTransportCallbackAccess()) {
    return;
  }
  BattleNetApi::FireEvent(static_cast<std::int32_t>(BNetUIEvent::kBlockListUpdated), nullptr);
}

void BattleNetUI::OnSystemMessage(const std::uint16_t message_id,
                                  const std::uint32_t amount) {
  if (!HasRidTransportCallbackAccess()) {
    return;
  }

  const char *message = "unknown";
  std::uint32_t event_amount = 0;
  switch (message_id) {
  case 0x0ed8:
    message = "shutdown minutes";
    event_amount = amount;
    break;
  case 0x0ed9:
    message = "shutdown seconds";
    event_amount = amount;
    break;
  case 0x0eda:
    message = "shutdown now";
    break;
  default:
    break;
  }
  BattleNetApi::FireEvent(static_cast<std::int32_t>(BNetUIEvent::kSystemMessage), "%s%u",
                          message, event_amount);
}

void BattleNetUI::OnSettingChanged(const std::string_view setting_name, const bool enabled) {
  if (!HasRidTransportCallbackAccess() || setting_name != "Chat.ProfanityFilterEnabled") {
    return;
  }
  BattleNetApi::FireEvent(static_cast<std::int32_t>(BNetUIEvent::kMatureLanguageFilter), "%b",
                          enabled ? 1 : 0);
}

void BattleNetUI::OnConversationMessageUndeliverable(const std::uint8_t channel_index,
                                                      const std::uint16_t error_code) {
  if (!HasRidTransportCallbackAccess()) {
    return;
  }
  ValidateConversationChannelOrFatal(
      channel_index, "BattleNetUI::OnConversationMessageUndeliverable invalid channel");
  BattleNetApi::Instance().HandleError(error_code, 0);
  BattleNetApi::FireEvent(
      static_cast<std::int32_t>(BNetUIEvent::kConversationMessageUndeliverable), "%d",
      static_cast<int>(channel_index + 1));
}

void BattleNetUI::OnConversationMessageBlocked(const std::uint8_t channel_index,
                                               const std::int32_t presence_id) {
  if (!HasRidTransportCallbackAccess()) {
    return;
  }
  ValidateConversationChannelOrFatal(channel_index,
                                     "BattleNetUI::OnConversationMessageBlocked invalid channel");
  BattleNetApi::FireEvent(static_cast<std::int32_t>(BNetUIEvent::kConversationMessageBlocked),
                          "%d%u", static_cast<int>(channel_index + 1),
                          static_cast<std::uint32_t>(presence_id));
}

void BattleNetUI::OnConversationMemberUpdated(const std::uint8_t channel_index,
                                              const std::int32_t presence_id) {
  if (!HasRidTransportCallbackAccess()) {
    return;
  }
  ValidateConversationChannelOrFatal(channel_index,
                                     "BattleNetUI::OnConversationMemberUpdated invalid channel");
  BattleNetApi::FireEvent(static_cast<std::int32_t>(BNetUIEvent::kConversationMemberUpdated),
                          "%d%u", static_cast<int>(channel_index + 1),
                          static_cast<std::uint32_t>(presence_id));
}

void BattleNetUI::OnConversationMemberJoined(const std::uint8_t channel_index,
                                             const std::int32_t presence_id) {
  if (!HasRidTransportCallbackAccess()) {
    return;
  }
  ValidateConversationChannelOrFatal(channel_index,
                                     "BattleNetUI::OnConversationMemberJoined invalid channel");
  auto &api = BattleNetApi::Instance();
  api.UpdateRecentWhispers(presence_id, 0x30, true, api.GetNameForPresenceID(presence_id));
  DisplayConversationNotice("MEMBER_JOINED", channel_index, presence_id);
  BattleNetApi::FireEvent(static_cast<std::int32_t>(BNetUIEvent::kConversationMemberJoined),
                          "%d%u", static_cast<int>(channel_index + 1),
                          static_cast<std::uint32_t>(presence_id));
}

void BattleNetUI::OnConversationMemberLeft(const std::uint8_t channel_index,
                                           const std::int32_t presence_id) {
  if (!HasRidTransportCallbackAccess()) {
    return;
  }
  ValidateConversationChannelOrFatal(channel_index,
                                     "BattleNetUI::OnConversationMemberLeft invalid channel");
  DisplayConversationNotice("MEMBER_LEFT", channel_index, presence_id);
  BattleNetApi::FireEvent(static_cast<std::int32_t>(BNetUIEvent::kConversationMemberLeft), "%d%u",
                          static_cast<int>(channel_index + 1),
                          static_cast<std::uint32_t>(presence_id));
}

void BattleNetUI::OnConversationJoined(const std::uint8_t channel_index,
                                       const std::int32_t channel_type) {
  if (!HasRidTransportCallbackAccess()) {
    return;
  }
  ValidateConversationChannelOrFatal(channel_index,
                                     "BattleNetUI::OnConversationJoined invalid channel");
  auto &api = BattleNetApi::Instance();
  for (const auto presence_id : api.GetConversationMemberList(channel_index)) {
    api.UpdateRecentWhispers(presence_id, 0x30, true, api.GetNameForPresenceID(presence_id));
  }
  if (channel_type != kBNetConversationChannelTypeRegular) {
    return;
  }

  const auto current_account = api.GetPresenceIDForCurrentAccount();
  DisplayConversationNotice("YOU_JOINED_CONVERSATION", channel_index, current_account);
  BattleNetApi::FireEvent(static_cast<std::int32_t>(BNetUIEvent::kConversationJoined), "%d",
                          static_cast<int>(channel_index + 1));
  DisplayJoinedConversationMembers(channel_index);
}

void BattleNetUI::OnConversationLeft(const std::uint8_t channel_index) {
  if (!HasRidTransportCallbackAccess()) {
    return;
  }
  ValidateConversationChannelOrFatal(channel_index,
                                     "BattleNetUI::OnConversationLeft invalid channel");
  const auto current_account = BattleNetApi::Instance().GetPresenceIDForCurrentAccount();
  DisplayConversationNotice("YOU_LEFT_CONVERSATION", channel_index, current_account);
  BattleNetApi::FireEvent(static_cast<std::int32_t>(BNetUIEvent::kConversationLeft), "%d",
                          static_cast<int>(channel_index + 1));
}

void BattleNetUI::OnConversationClosed(const std::uint8_t channel_index,
                                       const std::int32_t presence_id) {
  if (!HasRidTransportCallbackAccess()) {
    return;
  }
  ValidateConversationChannelOrFatal(channel_index,
                                     "BattleNetUI::OnConversationClosed invalid channel");
  auto &api = BattleNetApi::Instance();
  DisplayConversationNotice("CONVERSATION_CONVERTED_TO_WHISPER", channel_index, presence_id);
  BattleNetApi::FireEvent(static_cast<std::int32_t>(BNetUIEvent::kConversationClosed), "%d%s",
                          static_cast<int>(channel_index + 1),
                          api.GetNameForPresenceID(presence_id));
}

void BattleNetUI::OnConversationCreated(std::int32_t , std::int32_t arg_count,
                                        const BNetVariant *args) {
  if (!HasConversationCallbackAccess() || !args)
    return;

  auto &api = BattleNetApi::Instance();
  const auto error_code = static_cast<BNetErrorCode>(args[0].ToInt());
  if (error_code == 0) {
    const auto channel_index =
        static_cast<std::uint8_t>(arg_count >= 2 ? args[1].ToInt() : 0);
    if (channel_index >= kBNetConversationChannelCount) {
      openwow::core::SErrFatalCondition("%s",
                                        "BattleNetUI::OnConversationCreated invalid channel");
    }

    BattleNetApi::FireEvent(static_cast<std::int32_t>(BNetUIEvent::kConversationCreateSucceeded),
                            "%d", static_cast<int>(channel_index + 1));
    return;
  }

  const auto context =
      arg_count >= 2 ? ResolveConversationPresenceId(args[1]) : 0;
  api.HandleError(error_code, context);
  BattleNetApi::FireEvent(static_cast<std::int32_t>(BNetUIEvent::kConversationCreateFailed), "%u",
                          static_cast<unsigned int>(context));
}

void BattleNetUI::OnConversationInvite(std::int32_t , std::int32_t ,
                                       const BNetVariant *args) {
  if (!HasConversationCallbackAccess() || !args)
    return;

  auto &api = BattleNetApi::Instance();
  const auto error_code = static_cast<BNetErrorCode>(args[0].ToInt());
  const auto invitee_id = ResolveConversationPresenceId(args[1]);
  const auto channel_index =
      ReadConversationChannelOrFatal(args[2], "BattleNetUI::OnConversationInvite invalid channel");
  const auto channel_id = static_cast<int>(channel_index + 1);

  if (error_code == 0) {
    BattleNetApi::FireEvent(static_cast<std::int32_t>(BNetUIEvent::kConversationInviteSucceeded),
                            "%d%u", channel_id, static_cast<unsigned int>(invitee_id));
    return;
  }

  api.HandleError(error_code, invitee_id);
  BattleNetApi::FireEvent(static_cast<std::int32_t>(BNetUIEvent::kConversationInviteFailed),
                          "%d%u", channel_id, static_cast<unsigned int>(invitee_id));
}

void BattleNetUI::ApplyNameFormat() {
  if (!CanApplyBattleNetNameFormat()) {
    return;
  }

  auto &api = BattleNetApi::Instance();
  api.RefreshRecentWhisperTargets();

  const std::string name_format =
      Localization::Get().GetString("BATTLENET_NAME_FORMAT", "");
  if (name_format.empty()) {
    return;
  }

  api.SetAccountNameFormatString(name_format.c_str());
}

bool BattleNetUI::IsInitialized() {
  return GetBattleNetUiInitState().initialized;
}

std::size_t BattleNetUI::GetRegisteredEventCount() {
  return GetBattleNetUiInitState().event_registrations.size();
}

std::size_t BattleNetUI::GetRegisteredPresenceUpdateCount() {
  return GetBattleNetUiInitState().presence_registrations.size();
}

bool BattleNetUI::HasRegisteredEvent(std::string_view event_name,
                                     BNetUiEventCallbackId callback_id) {
  const auto &state = GetBattleNetUiInitState();
  for (const auto &registration : state.event_registrations) {
    if (registration.event_name == event_name && registration.callback_id == callback_id) {
      return true;
    }
  }
  return false;
}

bool BattleNetUI::HasRegisteredPresenceUpdate(std::int32_t target_id, std::uint32_t packed_key,
                                              BNetUiPresenceCallbackId callback_id) {
  const auto &state = GetBattleNetUiInitState();
  for (const auto &registration : state.presence_registrations) {
    if (registration.target_id == target_id && registration.packed_key == packed_key &&
        registration.callback_id == callback_id) {
      return true;
    }
  }
  return false;
}

bool BattleNetUI::HasSpecialCallback(BNetUiSpecialCallbackId callback_id) {
  return GetBattleNetUiInitState().special_callbacks[ToIndex(callback_id)];
}

void BattleNetUI::Shutdown() {
  auto &state = GetBattleNetUiInitState();
  auto &api = BattleNetApi::Instance();
  state.initialized = false;

  for (const auto &registration : state.event_registrations) {
    api.UnregisterEvent(registration.handle);
  }
  for (const auto &registration : state.presence_registrations) {
    api.UnregisterPresenceUpdateCallback(registration.handle);
  }

  api.ClearUiSocialCaches();
  state.event_registrations.clear();
  state.presence_registrations.clear();

  state.special_callbacks.fill(false);
}

}

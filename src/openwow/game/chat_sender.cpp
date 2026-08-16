
#include "openwow/game/chat_sender.h"

#include "openwow/game/character_map_runtime.h"
#include "openwow/game/chat_display.h"
#include "openwow/game/localization.h"
#include "openwow/game/chat_manager.h"
#include "openwow/game/chat_system.h"
#include "openwow/game/chat_types.h"
#include "openwow/game/player_chat_flags.h"
#include "openwow/game/tutorial_system.h"
#include "openwow/core/storm_string.h"
#include "openwow/game/object_manager.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"

#include <utility>

namespace openwow::game {

namespace {

constexpr std::uint32_t kThrottleWindowMs = 10000;

}

void ChatSender::BindRuntime(SendPacketFn send_packet,
                             CharacterMapRuntime& map_runtime,
                             ChatManager& chat, ClientTimeFn client_time) {
  send_packet_ = std::move(send_packet);
  map_runtime_ = &map_runtime;
  chat_ = &chat;
  client_time_ = std::move(client_time);
  Reset();
}

void ChatSender::SetActivityCallback(std::function<void(std::uint32_t)> callback) {
  activity_callback_ = std::move(callback);
}

void ChatSender::SendTyped(const ChatMsg type, const Language language,
                           const std::string& target,
                           const std::string& message) {
  SendChatMessage(static_cast<std::uint32_t>(type),
                  static_cast<std::uint32_t>(language), target, message);
}

void ChatSender::Update(const std::uint32_t current_time_ms) {
  if (!send_packet_) {
    return;
  }

  ProcessPending(current_time_ms);
}

void ChatSender::Reset() {
  shared_whisper_window_.Reset();
  per_target_windows_.clear();
  pending_messages_.clear();
  local_afk_display_active_ = false;
}

bool ChatSender::IsLocalAfkDisplayed() const {
  return local_afk_display_active_;
}

void ChatSender::SyncLocalAfkDisplayState(const std::uint32_t player_flags) {
  local_afk_display_active_ = (player_flags & PlayerFlagBits::kAFK) != 0;
}

void ChatSender::OnOutgoingChatActivity(const std::uint32_t current_time_ms) {
  if (activity_callback_) {
    activity_callback_(current_time_ms);
  }
}

void ChatSender::DisplaySystemMessage(const std::string& message) const {
  if (message.empty()) {
    return;
  }

  ChatFrame_DisplayMessage(map_runtime_->objects(), message.c_str(), ChatDisplayType::kSystem, nullptr, 0, nullptr, nullptr,
                           nullptr, 0, 0, 0, 0, 0, nullptr);
}

std::string ChatSender::LocalizedString(const std::string& key, const std::string& fallback) const {
  auto& localization = Localization::Get();
  if (localization.HasString(key)) {
    return localization.GetString(key);
  }
  return fallback;
}

void ChatSender::SendAfkTogglePacket(const std::string& message) {
  SendPacket(ChatMsg::kAfk, Language::kUniversal, "", message);
}

void ChatSender::ClearLocalAfkIfNeeded(const bool force_clear) {
  if (!local_afk_display_active_) {
    return;
  }

  if (!force_clear && !openwow::ui::game::CVarSystem::Instance().GetCVarBool("autoClearAFK")) {
    return;
  }

  local_afk_display_active_ = false;
  DisplaySystemMessage(LocalizedString("CLEARED_AFK"));
  SendAfkTogglePacket("");
}

void ChatSender::HandleAfkSend(const std::string& message) {
  const auto *active_player = map_runtime_ != nullptr
                                  ? map_runtime_->objects().GetActivePlayer()
                                  : nullptr;
  const std::uint32_t player_flags = active_player != nullptr ? active_player->GetPlayerFlags() : 0;

  std::string effective_message = message;
  if (!local_afk_display_active_ && effective_message.empty()) {
    effective_message = LocalizedString("DEFAULT_AFK_MESSAGE");
  }
  if (effective_message.empty()) {
    ClearLocalAfkIfNeeded(true);
    return;
  }

  if (!local_afk_display_active_) {
    if ((player_flags & 0x04u) != 0u) {
      DisplaySystemMessage(LocalizedString("CLEARED_DND"));
    }

    const std::string marked_format = LocalizedString("MARKED_AFK_MESSAGE");
    if (!marked_format.empty()) {
      DisplaySystemMessage(Localization::Get().FormatString(marked_format, {effective_message}));
    }

    local_afk_display_active_ = true;
  }

  SendAfkTogglePacket(effective_message);
}

void ChatSender::HandleDndSend(const Language language, const std::string& message) {
  const auto* active_player = map_runtime_ != nullptr
                                  ? map_runtime_->objects().GetActivePlayer()
                                  : nullptr;
  const std::uint32_t player_flags =
      active_player != nullptr ? active_player->GetPlayerFlags() : 0;
  const bool dnd_active = (player_flags & 0x04u) != 0u;

  std::string effective_message = message;
  if (!dnd_active && effective_message.empty()) {
    effective_message = LocalizedString("DEFAULT_DND_MESSAGE");
  }

  if (!effective_message.empty()) {
    const std::string marked_format = LocalizedString("MARKED_DND_MESSAGE");
    if (!marked_format.empty()) {
      DisplaySystemMessage(Localization::Get().FormatString(marked_format, {effective_message}));
    }
  } else {
    DisplaySystemMessage(LocalizedString("CLEARED_DND"));
  }

  SendPacket(ChatMsg::kDnd, language, "", effective_message);
}

bool ChatSender::IsRestrictedChatType(const ChatMsg type) const {
  switch (type) {
    case ChatMsg::kWhisper:
    case ChatMsg::kParty:
    case ChatMsg::kPartyLeader:
    case ChatMsg::kRaid:
    case ChatMsg::kRaidLeader:
    case ChatMsg::kRaidWarning:
    case ChatMsg::kGuild:
    case ChatMsg::kOfficer:
    case ChatMsg::kChannel:
    case ChatMsg::kDnd:
    case ChatMsg::kBattleground:
    case ChatMsg::kBattlegroundLeader:
      return false;
    default:
      return true;
  }
}

bool ChatSender::TimestampWindow::IsSaturated(const std::uint32_t now_ms) const {
  const auto oldest = slots[next_slot];
  return oldest != 0 && now_ms - oldest < kThrottleWindowMs;
}

void ChatSender::TimestampWindow::Record(const std::uint32_t now_ms) {
  slots[next_slot] = now_ms;
  next_slot = (next_slot + 1) % kBurstSize;
}

void ChatSender::TimestampWindow::Reset() {
  next_slot = 0;
  for (auto& slot : slots) {
    slot = 0;
  }
}

std::size_t ChatSender::CaseInsensitiveStringHash::operator()(
    const std::string& value) const {
  return static_cast<std::size_t>(openwow::core::SStrHashCI(value.c_str()));
}

bool ChatSender::CaseInsensitiveStringEqual::operator()(
    const std::string& left,
    const std::string& right) const {
  return openwow::core::SStrCmpNoCase(
             left.c_str(), right.c_str(), 0x7FFFFFFFu) == 0;
}

void ChatSender::SendChatMessage(std::uint32_t type, std::uint32_t language,
                                 const std::string& target,
                                 const std::string& message) {
  if (!send_packet_) return;

  const auto chat_type = static_cast<ChatMsg>(type);
  const auto chat_language = static_cast<Language>(language);
  const auto current_time_ms = client_time_ ? client_time_() : 0u;

  if (chat_type == ChatMsg::kAfk) {
    OnOutgoingChatActivity(current_time_ms);
    HandleAfkSend(message);
    return;
  }

  ClearLocalAfkIfNeeded(true);
  OnOutgoingChatActivity(current_time_ms);

  if (chat_type == ChatMsg::kDnd) {
    TutorialSystem::Instance().FlagTutorial(0x16u);
    HandleDndSend(chat_language, message);
    return;
  }

  if (chat_ != nullptr && chat_->chat_restricted() &&
      IsRestrictedChatType(chat_type)) {
    openwow::ui::game::DisplaySystemMessage(254);
    return;
  }

  TutorialSystem::Instance().FlagTutorial(0x16u);

  if (IsThrottleManagedType(chat_type, chat_language)) {
    ProcessPending(current_time_ms);

    if (UsesPerTargetThrottle(chat_type)) {
      if (IsPerTargetWindowSaturated(target, current_time_ms)) {
        pending_messages_.push_back(PendingMessage{
            .type = chat_type,
            .language = chat_language,
            .target = target,
            .message = message,
        });
        return;
      }
      RecordPerTargetTimestamp(target, current_time_ms);
    } else if (!IsWhisperThrottleExempt(target)) {
      if (shared_whisper_window_.IsSaturated(current_time_ms)) {
        pending_messages_.push_back(PendingMessage{
            .type = chat_type,
            .language = chat_language,
            .target = target,
            .message = message,
        });
        return;
      }
      shared_whisper_window_.Record(current_time_ms);
    }
  }

  SendPacket(chat_type, chat_language, target, message);
}

void ChatSender::SendPacket(const ChatMsg type, const Language language,
                            const std::string& target,
                            const std::string& message) {
  auto pkt = ChatManager::BuildChatMessage(type, language, message, target);
  (void)send_packet_(pkt);
}

void ChatSender::ProcessPending(const std::uint32_t current_time_ms) {
  for (auto it = pending_messages_.begin(); it != pending_messages_.end();) {
    bool can_send = true;

    if (UsesPerTargetThrottle(it->type)) {
      if (IsPerTargetWindowSaturated(it->target, current_time_ms)) {
        can_send = false;
      } else {
        RecordPerTargetTimestamp(it->target, current_time_ms);
      }
    } else if (!IsWhisperThrottleExempt(it->target)) {
      if (shared_whisper_window_.IsSaturated(current_time_ms)) {
        can_send = false;
      } else {
        shared_whisper_window_.Record(current_time_ms);
      }
    }

    if (!can_send) {
      ++it;
      continue;
    }

    SendPacket(it->type, it->language, it->target, it->message);
    it = pending_messages_.erase(it);
  }
}

bool ChatSender::IsThrottleManagedType(const ChatMsg type,
                                       const Language language) const {
  if (language == Language::kAddon) {
    return false;
  }

  return type == ChatMsg::kWhisper || type == ChatMsg::kChannel ||
         type == ChatMsg::kBattlenet;
}

bool ChatSender::UsesPerTargetThrottle(const ChatMsg type) const {
  return type == ChatMsg::kChannel;
}

bool ChatSender::IsWhisperThrottleExempt(const std::string& target) const {
  if (map_runtime_ == nullptr || target.empty()) {
    return false;
  }

  const auto whisper_guids = ChatSystem::Get().GetWhisperMruSnapshot();
  for (const auto raw_guid : whisper_guids) {
    if (raw_guid == 0) {
      continue;
    }

    const auto player_name =
        map_runtime_->objects().GetPlayerName(ObjectGuid(raw_guid));
    if (!player_name.empty() &&
        openwow::core::SStrCmpNoCase(
            target.c_str(), player_name.c_str(), 0x7FFFFFFFu) == 0) {
      return true;
    }
  }

  return false;
}

bool ChatSender::IsPerTargetWindowSaturated(
    const std::string& target,
    const std::uint32_t current_time_ms) const {
  const auto it = per_target_windows_.find(target);
  return it != per_target_windows_.end() &&
         it->second.IsSaturated(current_time_ms);
}

void ChatSender::RecordPerTargetTimestamp(const std::string& target,
                                          const std::uint32_t current_time_ms) {
  per_target_windows_[target].Record(current_time_ms);
}

void ChatSender::SendSay(const std::string& message, Language language) {
  SendTyped(ChatMsg::kSay, language, "", message);
}

void ChatSender::SendYell(const std::string& message, Language language) {
  SendTyped(ChatMsg::kYell, language, "", message);
}

void ChatSender::SendWhisper(const std::string& target,
                             const std::string& message, Language language) {
  SendTyped(ChatMsg::kWhisper, language, target, message);
}

void ChatSender::SendParty(const std::string& message, Language language) {
  SendTyped(ChatMsg::kParty, language, "", message);
}

void ChatSender::SendRaid(const std::string& message, Language language) {
  SendTyped(ChatMsg::kRaid, language, "", message);
}

void ChatSender::SendGuild(const std::string& message, Language language) {
  SendTyped(ChatMsg::kGuild, language, "", message);
}

void ChatSender::SendOfficer(const std::string& message, Language language) {
  SendTyped(ChatMsg::kOfficer, language, "", message);
}

void ChatSender::SendChannel(const std::string& channel,
                             const std::string& message, Language language) {
  SendTyped(ChatMsg::kChannel, language, channel, message);
}

void ChatSender::SendEmote(const std::string& emote_text) {
  SendTyped(ChatMsg::kEmote, Language::kUniversal, "", emote_text);
}

void ChatSender::SendRaidWarning(const std::string& message, Language language) {
  SendTyped(ChatMsg::kRaidWarning, language, "", message);
}

void ChatSender::SendAfk(const std::string& message, Language language) {
  SendTyped(ChatMsg::kAfk, language, "", message);
}

void ChatSender::SendDnd(const std::string& message, Language language) {
  SendTyped(ChatMsg::kDnd, language, "", message);
}

void ChatSender::SendBattleground(const std::string& message, Language language) {
  SendTyped(ChatMsg::kBattleground, language, "", message);
}

void ChatSender::SendBattlegroundLeader(const std::string& message, Language language) {
  SendTyped(ChatMsg::kBattlegroundLeader, language, "", message);
}

void ChatSender::SendRaidLeader(const std::string& message, Language language) {
  SendTyped(ChatMsg::kRaidLeader, language, "", message);
}

void ChatSender::SendPartyLeader(const std::string& message, Language language) {
  SendTyped(ChatMsg::kPartyLeader, language, "", message);
}

}

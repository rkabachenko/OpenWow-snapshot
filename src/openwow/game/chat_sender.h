
#pragma once

#include <cstdint>
#include <list>
#include <functional>
#include <string>
#include <unordered_map>

#include "openwow/game/chat_types.h"
#include "openwow/network/protocol/wotlk/world_packet.h"

namespace openwow::game {

class ChatManager;
class CharacterMapRuntime;

class ChatSender {
 public:
  using SendPacketFn =
      std::function<bool(const net::wotlk::WorldPacket&)>;
  using ClientTimeFn = std::function<std::uint32_t()>;

  void BindRuntime(SendPacketFn send_packet, CharacterMapRuntime& map_runtime,
                   ChatManager& chat, ClientTimeFn client_time);
  void Update(std::uint32_t current_time_ms);
  void Reset();
  void SetActivityCallback(std::function<void(std::uint32_t)> callback);
  void SendTyped(ChatMsg type, Language language, const std::string& target,
                 const std::string& message);

  [[nodiscard]] bool IsLocalAfkDisplayed() const;
  void SyncLocalAfkDisplayState(std::uint32_t player_flags);
  void ClearLocalAfkIfNeeded(bool force_clear);

  void SendSay(const std::string& message,
               Language language = Language::kCommon);
  void SendYell(const std::string& message,
                Language language = Language::kCommon);
  void SendWhisper(const std::string& target, const std::string& message,
                   Language language = Language::kCommon);
  void SendParty(const std::string& message,
                 Language language = Language::kCommon);
  void SendRaid(const std::string& message,
                Language language = Language::kCommon);
  void SendGuild(const std::string& message,
                 Language language = Language::kCommon);
  void SendOfficer(const std::string& message,
                   Language language = Language::kCommon);
  void SendChannel(const std::string& channel, const std::string& message,
                   Language language = Language::kCommon);
  void SendEmote(const std::string& emote_text);
  void SendRaidWarning(const std::string& message,
                       Language language = Language::kCommon);
  void SendAfk(const std::string& message,
               Language language = Language::kCommon);
  void SendDnd(const std::string& message,
               Language language = Language::kCommon);
  void SendBattleground(const std::string& message,
                        Language language = Language::kCommon);
  void SendBattlegroundLeader(const std::string& message,
                              Language language = Language::kCommon);
  void SendPartyLeader(const std::string& message,
                       Language language = Language::kCommon);
  void SendRaidLeader(const std::string& message,
                      Language language = Language::kCommon);

 private:
  struct TimestampWindow {
    static constexpr std::size_t kBurstSize = 10;

    bool IsSaturated(std::uint32_t now_ms) const;
    void Record(std::uint32_t now_ms);
    void Reset();

    std::uint32_t next_slot = 0;
    std::uint32_t slots[kBurstSize]{};
  };

  struct PendingMessage {
    ChatMsg type{ChatMsg::kSystem};
    Language language{Language::kUniversal};
    std::string target;
    std::string message;
  };

  struct CaseInsensitiveStringHash {
    std::size_t operator()(const std::string& value) const;
  };

  struct CaseInsensitiveStringEqual {
    bool operator()(const std::string& left, const std::string& right) const;
  };

  void SendChatMessage(std::uint32_t type, std::uint32_t language,
                       const std::string& target,
                       const std::string& message);
  void SendPacket(ChatMsg type, Language language, const std::string& target,
                  const std::string& message);
  void ProcessPending(std::uint32_t current_time_ms);
  void OnOutgoingChatActivity(std::uint32_t current_time_ms);
  void DisplaySystemMessage(const std::string& message) const;
  [[nodiscard]] std::string LocalizedString(const std::string& key,
                                            const std::string& fallback = {}) const;
  void HandleAfkSend(const std::string& message);
  void HandleDndSend(Language language, const std::string& message);
  void SendAfkTogglePacket(const std::string& message);
  [[nodiscard]] bool IsRestrictedChatType(ChatMsg type) const;
  bool IsThrottleManagedType(ChatMsg type, Language language) const;
  bool UsesPerTargetThrottle(ChatMsg type) const;
  bool IsWhisperThrottleExempt(const std::string& target) const;
  bool IsPerTargetWindowSaturated(const std::string& target,
                                  std::uint32_t current_time_ms) const;
  void RecordPerTargetTimestamp(const std::string& target,
                                std::uint32_t current_time_ms);

  SendPacketFn send_packet_;
  ClientTimeFn client_time_;
  CharacterMapRuntime* map_runtime_ = nullptr;
  ChatManager* chat_ = nullptr;
  TimestampWindow shared_whisper_window_;
  std::unordered_map<std::string, TimestampWindow, CaseInsensitiveStringHash,
                     CaseInsensitiveStringEqual>
      per_target_windows_;
  std::list<PendingMessage> pending_messages_;
  std::function<void(std::uint32_t)> activity_callback_;
  bool local_afk_display_active_{false};
};

}

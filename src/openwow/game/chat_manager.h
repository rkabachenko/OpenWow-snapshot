#pragma once

#include "openwow/game/chat_types.h"
#include "openwow/game/object_guid.h"
#include "openwow/game/packet_reader.h"
#include "openwow/network/protocol/wotlk/opcodes.h"
#include "openwow/network/protocol/wotlk/world_packet.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

struct ChatMessage {
  ChatMsg type{ChatMsg::kSystem};
  Language language{Language::kUniversal};
  ObjectGuid sender_guid;
  ObjectGuid receiver_guid;
  std::string sender_name;
  std::string secondary_name;
  std::string channel_name;
  std::string message;
  ChatTag chat_tag{ChatTag::kNone};
  std::uint32_t achievement_id{0};
  bool is_gm{false};
};

struct ChannelInfo {
  std::string name;
  std::uint32_t channel_id{0};
  std::uint8_t flags{0};
};

struct ChannelNotifyPacket {
  ChannelNotify type{ChannelNotify::kJoined};
  std::string channel_name;
  std::uint64_t actor_guid{0};
  std::uint64_t secondary_guid{0};
  std::uint32_t channel_id{0};
  std::uint32_t member_count{0};
  std::uint8_t channel_flags{0};
  std::uint8_t status_flag{0};
  std::uint8_t old_member_flags{0};
  std::uint8_t new_member_flags{0};
  std::string text;
};

struct ChannelMember {
  std::uint64_t player_guid{0};
  std::uint8_t member_flags{0};
};

struct ChannelListResponse {
  std::uint8_t channel_type{0};
  std::string channel_name;
  std::uint8_t channel_flags{0};
  std::vector<ChannelMember> members;
};

struct ChatServerMessage {
  std::uint32_t message_type{0};
  std::string message;
};

struct DefenseMessage {
  std::uint32_t zone_id{0};
  std::string message;
};

struct ChannelMemberCount {
  std::string channel_name;
  std::uint8_t flags{0};
  std::uint32_t member_count{0};
};

struct ComplainResult {
  std::uint8_t result{0};
  std::uint8_t unk{0};
};

struct UserlistEntry {
  std::uint64_t guid{0};
  std::uint8_t user_flags{0};
  std::uint8_t channel_flags{0};
  std::uint32_t num_players{0};
  std::string channel_name;
};

struct ChatComplaintRecord {
  std::uint32_t line_id{0};
  ObjectGuid sender_guid;
  std::string sender_name;
  std::string message;
};

class ChatManager {
 public:
  static constexpr std::size_t kMaxScrollback = 1000;
  static constexpr std::size_t kComplaintHistoryCapacity = 60;

  ChatManager() = default;

  static net::wotlk::WorldPacket BuildChatMessage(
      ChatMsg type, Language language,
      const std::string& message,
      const std::string& target = "");

  static net::wotlk::WorldPacket BuildJoinChannel(
      std::uint32_t channel_id,
      const std::string& channel_name,
      const std::string& password = "",
      bool has_voice = false,
      std::uint8_t join_flag = 0);

  static net::wotlk::WorldPacket BuildLeaveChannel(
      std::uint32_t channel_id,
      const std::string& channel_name);

  static bool ParseChatMessage(const std::uint8_t* data, std::size_t len,
                               bool is_gm_message, ChatMessage& out);

  static bool ParseChannelNotify(const std::uint8_t* data, std::size_t len,
                                 ChannelNotifyPacket& out);

  bool HandleChatPlayerNotFound(const std::uint8_t* data, std::size_t len);

  bool HandleChannelList(const std::uint8_t* data, std::size_t len);

  bool HandleChatWrongFaction();

  bool HandleChatServerMessage(const std::uint8_t* data, std::size_t len);

  bool HandleChatNotInParty(const std::uint8_t* data, std::size_t len);

  bool HandleChatRestricted(const std::uint8_t* data, std::size_t len);

  bool HandleDefenseMessage(const std::uint8_t* data, std::size_t len);

  bool HandleChatPlayerAmbiguous(const std::uint8_t* data, std::size_t len);

  bool HandleChannelMemberCount(const std::uint8_t* data, std::size_t len);

  bool HandleComplainResult(const std::uint8_t* data, std::size_t len);
  bool HandleUserlistAdd(const std::uint8_t* data, std::size_t len);
  bool HandleUserlistRemove(const std::uint8_t* data, std::size_t len);
  bool HandleUserlistUpdate(const std::uint8_t* data, std::size_t len);

  void AddMessage(ChatMessage msg);

  [[nodiscard]] const std::deque<ChatMessage>& messages() const { return messages_; }

  [[nodiscard]] std::vector<const ChatMessage*> GetMessagesByType(ChatMsg type) const;

  [[nodiscard]] const ChatComplaintRecord* FindComplaintRecordByLineId(
      std::uint32_t line_id) const;

  [[nodiscard]] const ChatComplaintRecord* FindLatestComplaintRecord(
      const std::string& sender_name, const char* message = nullptr) const;

  void Clear();

  [[nodiscard]] std::size_t message_count() const { return messages_.size(); }

  [[nodiscard]] const std::string& last_player_not_found() const {
    return last_player_not_found_;
  }

  [[nodiscard]] const std::optional<ChannelListResponse>& last_channel_list() const {
    return last_channel_list_;
  }

  [[nodiscard]] bool chat_wrong_faction() const { return chat_wrong_faction_; }

  [[nodiscard]] const std::optional<ChatServerMessage>& last_server_message() const {
    return last_server_message_;
  }

  [[nodiscard]] bool chat_not_in_party() const { return chat_not_in_party_; }

  [[nodiscard]] std::uint32_t last_chat_not_in_party_type() const {
    return last_chat_not_in_party_type_;
  }

  [[nodiscard]] bool chat_restricted() const { return chat_restricted_; }

  [[nodiscard]] std::uint8_t last_chat_restriction() const {
    return last_chat_restriction_;
  }

  [[nodiscard]] const std::optional<DefenseMessage>& last_defense_message() const {
    return last_defense_message_;
  }

  [[nodiscard]] const std::string& last_ambiguous_player() const {
    return last_ambiguous_player_;
  }

  [[nodiscard]] const std::optional<ChannelMemberCount>& last_channel_member_count() const {
    return last_channel_member_count_;
  }

  [[nodiscard]] const std::optional<ComplainResult>& last_complain_result() const {
    return last_complain_result_;
  }
  [[nodiscard]] const std::optional<UserlistEntry>& last_userlist_add() const {
    return last_userlist_add_;
  }
  [[nodiscard]] const std::optional<UserlistEntry>& last_userlist_remove() const {
    return last_userlist_remove_;
  }
  [[nodiscard]] const std::optional<UserlistEntry>& last_userlist_update() const {
    return last_userlist_update_;
  }

  void OnChannelJoined(const std::string& name, std::uint32_t id, std::uint8_t flags);
  void OnChannelLeft(const std::string& name);
  [[nodiscard]] const std::vector<ChannelInfo>& channels() const { return channels_; }

  using OnMessageFn = std::function<void(const ChatMessage&)>;
  void SetOnMessage(OnMessageFn fn) { on_message_ = std::move(fn); }

 private:
  void TrackComplaintRecord(const ChatMessage& msg);

  std::deque<ChatMessage> messages_;
  std::deque<ChatComplaintRecord> complaint_history_;
  std::vector<ChannelInfo> channels_;
  OnMessageFn on_message_;
  std::uint32_t next_complaint_line_id_ = 1;
  std::string last_player_not_found_;
  std::optional<ChannelListResponse> last_channel_list_;
  bool chat_wrong_faction_ = false;
  std::optional<ChatServerMessage> last_server_message_;
  bool chat_not_in_party_ = false;
  std::uint32_t last_chat_not_in_party_type_ = 0;
  bool chat_restricted_ = false;
  std::uint8_t last_chat_restriction_ = 0;
  std::optional<DefenseMessage> last_defense_message_;
  std::string last_ambiguous_player_;
  std::optional<ChannelMemberCount> last_channel_member_count_;

  std::optional<ComplainResult> last_complain_result_;
  std::optional<UserlistEntry> last_userlist_add_;
  std::optional<UserlistEntry> last_userlist_remove_;
  std::optional<UserlistEntry> last_userlist_update_;

};

}

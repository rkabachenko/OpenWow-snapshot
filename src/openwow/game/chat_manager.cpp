
#include "openwow/game/chat_manager.h"

#include "openwow/core/storm_string.h"

#include <algorithm>
#include <cstring>

namespace openwow::game {

namespace {

bool IsComplaintTrackableMessage(const ChatMessage& msg) {
  return !msg.sender_guid.IsEmpty() && !msg.sender_name.empty() && !msg.message.empty();
}

}

net::wotlk::WorldPacket ChatManager::BuildChatMessage(
    ChatMsg type, Language language,
    const std::string& message,
    const std::string& target) {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_MESSAGECHAT);

  pkt.AppendU32(static_cast<std::uint32_t>(type));
  pkt.AppendU32(static_cast<std::uint32_t>(language));

  if (ChatTypeNeedsTarget(type)) {

    pkt.AppendString(target.c_str());
  } else if (ChatTypeNeedsChannel(type)) {

    pkt.AppendString(target.c_str());
  }

  pkt.AppendString(message.c_str());

  return pkt;
}

net::wotlk::WorldPacket ChatManager::BuildJoinChannel(
    std::uint32_t channel_id,
    const std::string& channel_name,
    const std::string& password,
    const bool has_voice,
    const std::uint8_t join_flag) {

  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_JOIN_CHANNEL);

  pkt.AppendU32(channel_id);
  pkt.AppendU8(join_flag);
  pkt.AppendU8(has_voice ? 1 : 0);
  pkt.AppendString(channel_name.c_str());
  pkt.AppendString(password.c_str());

  return pkt;
}

net::wotlk::WorldPacket ChatManager::BuildLeaveChannel(
    std::uint32_t channel_id,
    const std::string& channel_name) {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_LEAVE_CHANNEL);

  pkt.AppendU32(channel_id);
  pkt.AppendString(channel_name.c_str());

  return pkt;
}

bool ChatManager::ParseChatMessage(const std::uint8_t* data, std::size_t len,
                                   bool is_gm_message, ChatMessage& out) {
  PacketReader reader(data, len);
  out = ChatMessage{};

  std::uint8_t chat_type_raw;
  if (!reader.ReadU8(chat_type_raw)) return false;
  out.type = static_cast<ChatMsg>(chat_type_raw);

  std::uint32_t language_raw;
  if (!reader.ReadU32(language_raw)) return false;
  out.language = static_cast<Language>(language_raw);

  std::uint64_t sender_guid_raw;
  if (!reader.ReadU64(sender_guid_raw)) return false;
  out.sender_guid = ObjectGuid(sender_guid_raw);

  std::uint32_t flags;
  if (!reader.ReadU32(flags)) return false;

  out.is_gm = is_gm_message;

  if (IsMonsterChatType(out.type)) {

    std::uint32_t sender_name_len;
    if (!reader.ReadU32(sender_name_len)) return false;
    if (!reader.ReadCString(out.sender_name)) return false;

    std::uint64_t receiver_guid_raw;
    if (!reader.ReadU64(receiver_guid_raw)) return false;
    out.receiver_guid = ObjectGuid(receiver_guid_raw);

    if (receiver_guid_raw != 0) {
      auto high = out.receiver_guid.GetHigh();
      if (high != HighGuid::kPlayer && high != HighGuid::kPet) {
        std::uint32_t recv_name_len;
        if (!reader.ReadU32(recv_name_len)) return false;
        if (!reader.ReadCString(out.secondary_name)) return false;
      }
    }
  } else if (out.type == ChatMsg::kWhisperForeign) {
    std::uint32_t sender_name_len;
    if (!reader.ReadU32(sender_name_len)) return false;
    if (!reader.ReadCString(out.sender_name)) return false;

    std::uint64_t receiver_guid_raw;
    if (!reader.ReadU64(receiver_guid_raw)) return false;
    out.receiver_guid = ObjectGuid(receiver_guid_raw);

  } else if (IsBgSystemMessage(out.type)) {
    std::uint64_t receiver_guid_raw;
    if (!reader.ReadU64(receiver_guid_raw)) return false;
    out.receiver_guid = ObjectGuid(receiver_guid_raw);

    if (receiver_guid_raw != 0) {
      auto high = out.receiver_guid.GetHigh();
      if (high != HighGuid::kPlayer) {
        std::uint32_t recv_name_len;
        if (!reader.ReadU32(recv_name_len)) return false;
        if (!reader.ReadCString(out.secondary_name)) return false;
      }
    }
  } else if (IsAchievementMessage(out.type)) {
    std::uint64_t receiver_guid_raw;
    if (!reader.ReadU64(receiver_guid_raw)) return false;
    out.receiver_guid = ObjectGuid(receiver_guid_raw);

  } else {

    if (is_gm_message) {

      std::uint32_t sender_name_len;
      if (!reader.ReadU32(sender_name_len)) return false;
      if (!reader.ReadCString(out.sender_name)) return false;
    }

    if (out.type == ChatMsg::kChannel) {
      if (!reader.ReadCString(out.channel_name)) return false;
    }

    std::uint64_t receiver_guid_raw;
    if (!reader.ReadU64(receiver_guid_raw)) return false;
    out.receiver_guid = ObjectGuid(receiver_guid_raw);
  }

  std::uint32_t message_len;
  if (!reader.ReadU32(message_len)) return false;
  if (!reader.ReadCString(out.message)) return false;

  std::uint8_t chat_tag_raw;
  if (!reader.ReadU8(chat_tag_raw)) return false;
  out.chat_tag = static_cast<ChatTag>(chat_tag_raw);

  if (IsAchievementMessage(out.type)) {
    if (!reader.ReadU32(out.achievement_id)) return false;
  }

  return true;
}

bool ChatManager::ParseChannelNotify(const std::uint8_t* data, std::size_t len,
                                     ChannelNotifyPacket& out) {
  PacketReader reader(data, len);

  std::uint8_t notify_type_raw;
  if (!reader.ReadU8(notify_type_raw)) return false;
  out.type = static_cast<ChannelNotify>(notify_type_raw);

  if (!reader.ReadCString(out.channel_name)) return false;

  switch (out.type) {
    case ChannelNotify::kJoined:
    case ChannelNotify::kLeft:
    case ChannelNotify::kPasswordChanged:
    case ChannelNotify::kOwnerChanged:
    case ChannelNotify::kAnnouncementsOn:
    case ChannelNotify::kAnnouncementsOff:
    case ChannelNotify::kModerationOn:
    case ChannelNotify::kModerationOff:
    case ChannelNotify::kPlayerAlreadyMember:
    case ChannelNotify::kVoiceOn:
    case ChannelNotify::kVoiceOff:
    case ChannelNotify::kVoiceOnSilent:
      return reader.ReadU64(out.actor_guid);

    case ChannelNotify::kYouJoined:
      if (!reader.ReadU8(out.channel_flags)) return false;
      if (!reader.ReadU32(out.channel_id)) return false;
      if (!reader.ReadU32(out.member_count)) return false;
      if (reader.Remaining() == 0) return true;
      return reader.ReadCString(out.text);

    case ChannelNotify::kYouLeft:
      if (!reader.ReadU32(out.channel_id)) return false;
      return reader.ReadU8(out.status_flag);

    case ChannelNotify::kPlayerNotFound:
    case ChannelNotify::kChannelOwner:
    case ChannelNotify::kPlayerNotBanned:
    case ChannelNotify::kPlayerInvited:
    case ChannelNotify::kPlayerInviteBanned:
      return reader.ReadCString(out.text);

    case ChannelNotify::kModeChange:
      if (!reader.ReadU64(out.actor_guid)) return false;
      if (!reader.ReadU8(out.old_member_flags)) return false;
      return reader.ReadU8(out.new_member_flags);

    case ChannelNotify::kPlayerKicked:
    case ChannelNotify::kPlayerBanned:
    case ChannelNotify::kPlayerUnbanned:
      if (!reader.ReadU64(out.actor_guid)) return false;
      return reader.ReadU64(out.secondary_guid);

    case ChannelNotify::kInvite:
      return reader.ReadU64(out.actor_guid);

    default:
      return true;
  }
}

void ChatManager::AddMessage(ChatMessage msg) {
  TrackComplaintRecord(msg);
  if (on_message_) {
    on_message_(msg);
  }
  messages_.push_back(std::move(msg));
  while (messages_.size() > kMaxScrollback) {
    messages_.pop_front();
  }
}

std::vector<const ChatMessage*> ChatManager::GetMessagesByType(ChatMsg type) const {
  std::vector<const ChatMessage*> result;
  for (const auto& msg : messages_) {
    if (msg.type == type) {
      result.push_back(&msg);
    }
  }
  return result;
}

const ChatComplaintRecord* ChatManager::FindComplaintRecordByLineId(
    const std::uint32_t line_id) const {
  if (line_id == 0) {
    return nullptr;
  }

  for (const auto& record : complaint_history_) {
    if (record.line_id == line_id) {
      return &record;
    }
  }

  return nullptr;
}

const ChatComplaintRecord* ChatManager::FindLatestComplaintRecord(const std::string& sender_name,
                                                                  const char* message) const {
  if (sender_name.empty()) {
    return nullptr;
  }

  for (const auto& record : complaint_history_) {
    if (openwow::core::SStrCmpNoCase(record.sender_name.c_str(), sender_name.c_str(),
                                     0x7FFFFFFFu) != 0) {
      continue;
    }
    if (message != nullptr &&
        openwow::core::SStrCmpNoCase(record.message.c_str(), message, 0x7FFFFFFFu) != 0) {
      continue;
    }
    return &record;
  }

  return nullptr;
}

void ChatManager::Clear() {
  messages_.clear();
  complaint_history_.clear();
  channels_.clear();
  next_complaint_line_id_ = 1;
  last_player_not_found_.clear();
  last_channel_list_.reset();
  chat_wrong_faction_ = false;
  last_server_message_.reset();
  chat_not_in_party_ = false;
  last_chat_not_in_party_type_ = 0;
  chat_restricted_ = false;
  last_chat_restriction_ = 0;
  last_defense_message_.reset();
  last_ambiguous_player_.clear();
  last_channel_member_count_.reset();

  last_complain_result_.reset();
  last_userlist_add_.reset();
  last_userlist_remove_.reset();
  last_userlist_update_.reset();
}

void ChatManager::TrackComplaintRecord(const ChatMessage& msg) {
  if (!IsComplaintTrackableMessage(msg)) {
    return;
  }

  if (next_complaint_line_id_ == 0) {
    next_complaint_line_id_ = 1;
  }

  ChatComplaintRecord record;
  record.line_id = next_complaint_line_id_++;
  record.sender_guid = msg.sender_guid;
  record.sender_name = msg.sender_name;
  record.message = msg.message;
  complaint_history_.push_front(std::move(record));
  while (complaint_history_.size() > kComplaintHistoryCapacity) {
    complaint_history_.pop_back();
  }
}

void ChatManager::OnChannelJoined(const std::string& name, std::uint32_t id,
                                  std::uint8_t flags) {

  OnChannelLeft(name);
  channels_.push_back({name, id, flags});
}

void ChatManager::OnChannelLeft(const std::string& name) {
  channels_.erase(
      std::remove_if(channels_.begin(), channels_.end(),
                     [&name](const ChannelInfo& ci) { return ci.name == name; }),
      channels_.end());
}

bool ChatManager::HandleChatPlayerNotFound(const std::uint8_t* data,
                                           std::size_t len) {
  PacketReader reader(data, len);
  std::string name;
  if (!reader.ReadCString(name)) return false;
  last_player_not_found_ = std::move(name);
  return true;
}

bool ChatManager::HandleChannelList(const std::uint8_t* data,
                                    std::size_t len) {
  PacketReader reader(data, len);

  ChannelListResponse resp;

  if (!reader.ReadU8(resp.channel_type)) return false;
  if (!reader.ReadCString(resp.channel_name)) return false;
  if (!reader.ReadU8(resp.channel_flags)) return false;

  std::uint32_t member_count;
  if (!reader.ReadU32(member_count)) return false;

  resp.members.reserve(member_count);
  for (std::uint32_t i = 0; i < member_count; ++i) {
    ChannelMember m;
    if (!reader.ReadU64(m.player_guid)) return false;
    if (!reader.ReadU8(m.member_flags)) return false;
    resp.members.push_back(m);
  }

  last_channel_list_ = std::move(resp);
  return true;
}

bool ChatManager::HandleChatWrongFaction() {
  chat_wrong_faction_ = true;
  return true;
}

bool ChatManager::HandleChatServerMessage(const std::uint8_t* data,
                                          std::size_t len) {
  PacketReader r(data, len);
  ChatServerMessage msg{};
  if (!r.ReadU32(msg.message_type)) return false;
  if (!r.ReadCString(msg.message)) return false;
  last_server_message_ = std::move(msg);
  return true;
}

bool ChatManager::HandleChatNotInParty(const std::uint8_t* data,
                                       std::size_t len) {
  PacketReader r(data, len);
  std::uint32_t chat_type;
  if (!r.ReadU32(chat_type)) return false;
  chat_not_in_party_ = true;
  last_chat_not_in_party_type_ = chat_type;
  return true;
}

bool ChatManager::HandleChatRestricted(const std::uint8_t* data,
                                       std::size_t len) {
  PacketReader r(data, len);
  std::uint8_t restriction;
  if (!r.ReadU8(restriction)) return false;
  chat_restricted_ = true;
  last_chat_restriction_ = restriction;
  return true;
}

bool ChatManager::HandleDefenseMessage(const std::uint8_t* data,
                                       std::size_t len) {
  PacketReader r(data, len);
  DefenseMessage dm{};
  if (!r.ReadU32(dm.zone_id)) return false;

  std::uint32_t msg_length;
  if (!r.ReadU32(msg_length)) return false;
  if (!r.ReadCString(dm.message)) return false;
  last_defense_message_ = std::move(dm);
  return true;
}

bool ChatManager::HandleChatPlayerAmbiguous(const std::uint8_t* data,
                                            std::size_t len) {
  PacketReader r(data, len);
  std::string name;
  if (!r.ReadCString(name)) return false;
  last_ambiguous_player_ = std::move(name);
  return true;
}

bool ChatManager::HandleChannelMemberCount(const std::uint8_t* data,
                                           std::size_t len) {
  PacketReader r(data, len);
  ChannelMemberCount cmc{};
  if (!r.ReadCString(cmc.channel_name)) return false;
  if (!r.ReadU8(cmc.flags)) return false;
  if (!r.ReadU32(cmc.member_count)) return false;
  last_channel_member_count_ = std::move(cmc);
  return true;
}

bool ChatManager::HandleComplainResult(const std::uint8_t* data,
                                       std::size_t len) {
  PacketReader r(data, len);
  ComplainResult res{};
  if (!r.ReadU8(res.result)) return false;
  if (!r.ReadU8(res.unk)) return false;
  last_complain_result_ = res;
  return true;
}

bool ChatManager::HandleUserlistAdd(const std::uint8_t* data,
                                    std::size_t len) {
  PacketReader r(data, len);
  UserlistEntry e{};
  if (!r.ReadU64(e.guid)) return false;
  if (!r.ReadU8(e.user_flags)) return false;
  if (!r.ReadU8(e.channel_flags)) return false;
  if (!r.ReadU32(e.num_players)) return false;
  if (!r.ReadCString(e.channel_name)) return false;
  last_userlist_add_ = std::move(e);
  return true;
}

bool ChatManager::HandleUserlistRemove(const std::uint8_t* data,
                                       std::size_t len) {
  PacketReader r(data, len);
  UserlistEntry e{};
  if (!r.ReadU64(e.guid)) return false;
  if (!r.ReadU8(e.channel_flags)) return false;
  if (!r.ReadU32(e.num_players)) return false;
  if (!r.ReadCString(e.channel_name)) return false;
  last_userlist_remove_ = std::move(e);
  return true;
}

bool ChatManager::HandleUserlistUpdate(const std::uint8_t* data,
                                       std::size_t len) {
  PacketReader r(data, len);
  UserlistEntry e{};
  if (!r.ReadU64(e.guid)) return false;
  if (!r.ReadU8(e.user_flags)) return false;
  if (!r.ReadU8(e.channel_flags)) return false;
  if (!r.ReadU32(e.num_players)) return false;
  if (!r.ReadCString(e.channel_name)) return false;
  last_userlist_update_ = std::move(e);
  return true;
}

}


#include "openwow/ui/game/chat_frame_manager.h"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace openwow::ui {

const std::vector<ChatFrameMessage> ChatFrameManager::kEmptyMessages{};

ChatFrameManager& ChatFrameManager::Get() {
  static ChatFrameManager instance;
  return instance;
}

bool ChatFrameManager::ValidFrame(std::uint32_t id) const {
  return id >= 1 && id <= kMaxFrames;
}

void ChatFrameManager::CreateFrame(std::uint32_t id,
                                   const ChatFrameConfig& config) {
  if (!ValidFrame(id)) return;
  std::lock_guard lock(mutex_);
  auto& state = frames_[id - 1];
  state.config = config;
  state.config.id = id;
  state.messages.clear();
  state.scroll_offset = 0;
  state.has_new_messages = false;
}

void ChatFrameManager::RemoveFrame(std::uint32_t id) {
  if (!ValidFrame(id)) return;
  std::lock_guard lock(mutex_);
  auto& state = frames_[id - 1];
  state.config = {};
  state.messages.clear();
  state.scroll_offset = 0;
  state.has_new_messages = false;
}

ChatFrameConfig& ChatFrameManager::GetFrameConfig(std::uint32_t id) {

  return frames_[std::clamp(id, 1u, kMaxFrames) - 1].config;
}

const ChatFrameConfig& ChatFrameManager::GetFrameConfig(
    std::uint32_t id) const {
  return frames_[std::clamp(id, 1u, kMaxFrames) - 1].config;
}

void ChatFrameManager::AddMessage(const ChatFrameMessage& msg) {
  std::lock_guard lock(mutex_);

  ChatFrameMessage m = msg;
  m.id = next_msg_id_++;

  for (auto& state : frames_) {
    if (state.config.id == 0) continue;

    if (!state.config.message_types.empty() &&
        state.config.message_types.find(m.type) ==
            state.config.message_types.end()) {
      continue;
    }

    state.messages.push_back(m);
    TrimMessages(state);

    if (state.scroll_offset > 0) {
      state.has_new_messages = true;
    }
  }
}

void ChatFrameManager::AddSystemMessage(const std::string& text) {
  ChatFrameMessage msg;
  msg.type = static_cast<std::uint32_t>(openwow::game::ChatMsg::kSystem);
  msg.text = text;
  msg.formatted_text = text;
  AddMessage(msg);
}

const std::vector<ChatFrameMessage>& ChatFrameManager::GetMessages(
    std::uint32_t frame_id) const {
  if (!ValidFrame(frame_id)) return kEmptyMessages;
  std::lock_guard lock(mutex_);
  return frames_[frame_id - 1].messages;
}

std::uint32_t ChatFrameManager::GetMessageCount(
    std::uint32_t frame_id) const {
  if (!ValidFrame(frame_id)) return 0;
  std::lock_guard lock(mutex_);
  return static_cast<std::uint32_t>(frames_[frame_id - 1].messages.size());
}

void ChatFrameManager::ScrollUp(std::uint32_t frame_id,
                                std::uint32_t lines) {
  if (!ValidFrame(frame_id)) return;
  std::lock_guard lock(mutex_);
  auto& state = frames_[frame_id - 1];
  auto msg_count = static_cast<std::uint32_t>(state.messages.size());
  state.scroll_offset = std::min(state.scroll_offset + lines, msg_count);
}

void ChatFrameManager::ScrollDown(std::uint32_t frame_id,
                                  std::uint32_t lines) {
  if (!ValidFrame(frame_id)) return;
  std::lock_guard lock(mutex_);
  auto& state = frames_[frame_id - 1];
  if (lines >= state.scroll_offset) {
    state.scroll_offset = 0;
  } else {
    state.scroll_offset -= lines;
  }
}

void ChatFrameManager::ScrollToBottom(std::uint32_t frame_id) {
  if (!ValidFrame(frame_id)) return;
  std::lock_guard lock(mutex_);
  frames_[frame_id - 1].scroll_offset = 0;
}

bool ChatFrameManager::IsAtBottom(std::uint32_t frame_id) const {
  if (!ValidFrame(frame_id)) return true;
  std::lock_guard lock(mutex_);
  return frames_[frame_id - 1].scroll_offset == 0;
}

std::uint32_t ChatFrameManager::GetScrollOffset(
    std::uint32_t frame_id) const {
  if (!ValidFrame(frame_id)) return 0;
  std::lock_guard lock(mutex_);
  return frames_[frame_id - 1].scroll_offset;
}

bool ChatFrameManager::HasNewMessages(std::uint32_t frame_id) const {
  if (!ValidFrame(frame_id)) return false;
  std::lock_guard lock(mutex_);
  return frames_[frame_id - 1].has_new_messages;
}

void ChatFrameManager::ClearNewMessageFlag(std::uint32_t frame_id) {
  if (!ValidFrame(frame_id)) return;
  std::lock_guard lock(mutex_);
  frames_[frame_id - 1].has_new_messages = false;
}

void ChatFrameManager::SetMessageTypeVisible(std::uint32_t frame_id,
                                             std::uint32_t message_type,
                                             bool visible) {
  if (!ValidFrame(frame_id)) return;
  std::lock_guard lock(mutex_);
  auto& types = frames_[frame_id - 1].config.message_types;
  if (visible) {
    types.insert(message_type);
  } else {
    types.erase(message_type);
  }
}

bool ChatFrameManager::IsMessageTypeVisible(std::uint32_t frame_id,
                                            std::uint32_t message_type) const {
  if (!ValidFrame(frame_id)) return false;
  std::lock_guard lock(mutex_);
  const auto& types = frames_[frame_id - 1].config.message_types;

  if (types.empty()) return true;
  return types.count(message_type) > 0;
}

void ChatFrameManager::SetMaxMessages(std::uint32_t max) {
  std::lock_guard lock(mutex_);
  max_messages_ = max;
}

std::uint32_t ChatFrameManager::GetMaxMessages() const {
  std::lock_guard lock(mutex_);
  return max_messages_;
}

std::string ChatFrameManager::GetChatTypeColor(std::uint32_t chat_type) {
  using CT = openwow::game::ChatMsg;
  auto ct = static_cast<CT>(static_cast<std::uint8_t>(chat_type));
  switch (ct) {
    case CT::kSay:                return "FFFFFF";
    case CT::kYell:               return "FF3F40";
    case CT::kWhisper:            return "FF80FF";
    case CT::kWhisperInform:      return "FF80FF";
    case CT::kParty:              return "ABABFF";
    case CT::kPartyLeader:        return "77C8FF";
    case CT::kRaid:               return "FF7F00";
    case CT::kRaidLeader:         return "FF4700";
    case CT::kRaidWarning:        return "FF4700";
    case CT::kGuild:              return "40FF40";
    case CT::kOfficer:            return "40BF40";
    case CT::kEmote:              return "FF7F3F";
    case CT::kTextEmote:          return "FF7F3F";
    case CT::kChannel:            return "FFC0C0";
    case CT::kSystem:             return "FFFF00";
    case CT::kLoot:               return "00AB00";
    case CT::kMoney:              return "FFFF00";
    case CT::kCombatXpGain:       return "9482CA";
    case CT::kCombatHonorGain:    return "E0D6A6";
    case CT::kSkill:              return "5555FF";
    case CT::kBgSystemNeutral:    return "FFB200";
    case CT::kBgSystemAlliance:   return "00AEF0";
    case CT::kBgSystemHorde:      return "FF0000";
    case CT::kAchievement:        return "FFFF00";
    case CT::kGuildAchievement:   return "40FF40";
    default:                      return "FFFFFF";
  }
}

std::string ChatFrameManager::FormatMessage(const ChatFrameMessage& msg,
                                            bool show_timestamp,
                                            const std::string& time_format) {
  std::ostringstream out;

  if (show_timestamp && msg.timestamp > 0) {
    auto t = static_cast<std::time_t>(msg.timestamp);
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    out << std::put_time(&tm_buf, time_format.c_str()) << " ";
  }

  std::string color = GetChatTypeColor(msg.type);

  auto ct = static_cast<openwow::game::ChatMsg>(
      static_cast<std::uint8_t>(msg.type));

  switch (ct) {
    case openwow::game::ChatMsg::kSay:
      out << "|cff" << color << "[" << msg.sender << "] says: " << msg.text
          << "|r";
      break;
    case openwow::game::ChatMsg::kYell:
      out << "|cff" << color << "[" << msg.sender << "] yells: " << msg.text
          << "|r";
      break;
    case openwow::game::ChatMsg::kWhisper:
      out << "|cff" << color << "[" << msg.sender << "] whispers: " << msg.text
          << "|r";
      break;
    case openwow::game::ChatMsg::kGuild:
      out << "|cff" << color << "[Guild][" << msg.sender << "]: " << msg.text
          << "|r";
      break;
    case openwow::game::ChatMsg::kParty:
      out << "|cff" << color << "[Party][" << msg.sender << "]: " << msg.text
          << "|r";
      break;
    case openwow::game::ChatMsg::kRaid:
      out << "|cff" << color << "[Raid][" << msg.sender << "]: " << msg.text
          << "|r";
      break;
    case openwow::game::ChatMsg::kChannel:
      out << "|cff" << color << "[" << msg.channel_name << "][" << msg.sender
          << "]: " << msg.text << "|r";
      break;
    case openwow::game::ChatMsg::kSystem:
      out << "|cff" << color << msg.text << "|r";
      break;
    default:
      if (!msg.sender.empty()) {
        out << "|cff" << color << "[" << msg.sender << "]: " << msg.text
            << "|r";
      } else {
        out << "|cff" << color << msg.text << "|r";
      }
      break;
  }
  return out.str();
}

void ChatFrameManager::Reset() {
  std::lock_guard lock(mutex_);
  for (auto& state : frames_) {
    state.config = {};
    state.messages.clear();
    state.scroll_offset = 0;
    state.has_new_messages = false;
  }
  max_messages_ = 500;
  next_msg_id_ = 1;
}

void ChatFrameManager::TrimMessages(FrameState& state) {
  if (state.messages.size() > max_messages_) {
    auto excess = state.messages.size() - max_messages_;
    state.messages.erase(
        state.messages.begin(),
        state.messages.begin() + static_cast<std::ptrdiff_t>(excess));
  }
}

}

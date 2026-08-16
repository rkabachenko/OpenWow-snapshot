
#pragma once

#include "openwow/game/chat_types.h"
#include "openwow/game/object_guid.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace openwow::ui {

inline constexpr std::uint32_t kDefaultChatFrame = 1;

struct ChatFrameConfig {
  std::uint32_t id = 0;
  std::string name = "General";
  bool locked = false;
  float x = 0, y = 0;
  float width = 430;
  float height = 120;
  float font_size = 14;
  float alpha = 1.0f;
  bool show_timestamp = false;
  std::string timestamp_format = "%H:%M";

  std::unordered_set<std::uint32_t> message_types;

  std::unordered_set<std::uint32_t> channels;
};

struct ChatFrameMessage {
  std::uint32_t id = 0;
  std::uint32_t type = 0;
  std::string sender;
  std::string text;
  std::string channel_name;
  std::uint32_t language = 0;
  openwow::game::ObjectGuid sender_guid;
  std::uint32_t timestamp = 0;

  std::string formatted_text;

  bool is_combat = false;
};

class ChatFrameManager {
 public:
  static ChatFrameManager& Get();

  static constexpr std::uint32_t kMaxFrames = 10;

  void CreateFrame(std::uint32_t id, const ChatFrameConfig& config);
  void RemoveFrame(std::uint32_t id);
  ChatFrameConfig& GetFrameConfig(std::uint32_t id);
  const ChatFrameConfig& GetFrameConfig(std::uint32_t id) const;

  void AddMessage(const ChatFrameMessage& msg);

  void AddSystemMessage(const std::string& text);

  const std::vector<ChatFrameMessage>& GetMessages(
      std::uint32_t frame_id) const;
  std::uint32_t GetMessageCount(std::uint32_t frame_id) const;

  void ScrollUp(std::uint32_t frame_id, std::uint32_t lines = 3);
  void ScrollDown(std::uint32_t frame_id, std::uint32_t lines = 3);
  void ScrollToBottom(std::uint32_t frame_id);
  bool IsAtBottom(std::uint32_t frame_id) const;
  std::uint32_t GetScrollOffset(std::uint32_t frame_id) const;

  bool HasNewMessages(std::uint32_t frame_id) const;
  void ClearNewMessageFlag(std::uint32_t frame_id);

  void SetMessageTypeVisible(std::uint32_t frame_id,
                             std::uint32_t message_type, bool visible);
  bool IsMessageTypeVisible(std::uint32_t frame_id,
                            std::uint32_t message_type) const;

  void SetMaxMessages(std::uint32_t max);
  std::uint32_t GetMaxMessages() const;

  static std::string FormatMessage(const ChatFrameMessage& msg,
                                   bool show_timestamp,
                                   const std::string& time_format);

  static std::string GetChatTypeColor(std::uint32_t chat_type);

  void Reset();

 private:
  ChatFrameManager() = default;

  struct FrameState {
    ChatFrameConfig config;
    std::vector<ChatFrameMessage> messages;
    std::uint32_t scroll_offset = 0;
    bool has_new_messages = false;
  };

  bool ValidFrame(std::uint32_t id) const;
  void TrimMessages(FrameState& state);

  std::array<FrameState, kMaxFrames> frames_{};
  std::uint32_t max_messages_ = 500;
  std::uint32_t next_msg_id_ = 1;
  static const std::vector<ChatFrameMessage> kEmptyMessages;
  mutable std::mutex mutex_;
};

}

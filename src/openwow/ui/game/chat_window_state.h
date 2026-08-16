
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::ui::game {

inline constexpr int kMaxChatWindows = 10;

struct ChatWindowChannel {
  std::string name;
  std::uint32_t number = 0;
};

struct ChatWindowSavedPosition {
  int point = 0;
  float x = 0.0f;
  float y = 0.0f;
  bool is_set = false;
};

struct ChatWindowSavedDimensions {
  float width = 430.0f;
  float height = 120.0f;
};

struct ChatWindowInfo {
  std::string name;
  float font_size = 14.0f;
  float r = 1.0f, g = 1.0f, b = 1.0f;
  float alpha = 1.0f;
  bool shown = true;
  bool locked = false;
  int dock_target = 0;
  bool uninteractable = false;
  bool saved_layout_is_set = false;
  ChatWindowSavedPosition saved_position;
  ChatWindowSavedDimensions saved_dimensions;
  std::vector<std::optional<ChatWindowChannel>> channels;
  std::vector<std::string> message_groups;
};

class ChatWindowState {
 public:
  static ChatWindowState& Get();

  void SetWindowName(int index, std::string_view name);
  void SetWindowFontSize(int index, int font_size);
  void SetWindowAlpha(int index, float alpha);
  void SetWindowColor(int index, float r, float g, float b);
  void SetWindowDockTarget(int index, int dock_target);
  void SetWindowLocked(int index, bool locked);
  void SetWindowShown(int index, bool shown);
  void SetWindowUninteractable(int index, bool uninteractable);
  [[nodiscard]] std::optional<ChatWindowInfo> TryGetWindow(int index) const;

  void AddChannel(int window, const std::string& channel, std::uint32_t channel_number = 0);
  void RemoveChannel(int window, const std::string& channel);
  void RemoveChannelFromAllWindows(const std::string& channel);
  [[nodiscard]] bool HasChannel(int window, const std::string& channel) const;
  [[nodiscard]] std::size_t GetChannelCount(int window) const;
  [[nodiscard]] std::vector<ChatWindowChannel> GetChannels(int window) const;

  void AddMessageGroup(int window, const std::string& group);
  void RemoveMessageGroup(int window, const std::string& group);
  [[nodiscard]] bool HasMessageGroup(int window, const std::string& group) const;
  [[nodiscard]] std::size_t GetMessageGroupCount(int window) const;
  [[nodiscard]] std::vector<std::string> GetMessageGroups(int window) const;
  [[nodiscard]] std::array<ChatWindowInfo, kMaxChatWindows> GetWindowsSnapshot() const;

  void ClearAllMessageGroups();

  void ApplyAddedVersionDefaultMessageGroups(int added_version);

  [[nodiscard]] std::optional<ChatWindowSavedPosition> GetSavedPosition(int window) const;
  void SetSavedPosition(int window, int point, float x, float y);
  [[nodiscard]] std::optional<ChatWindowSavedDimensions> GetSavedDimensions(int window) const;
  void SetSavedDimensions(int window, float width, float height);

  void Reset();

 private:
  ChatWindowState();
  bool ValidIndex(int index) const { return index >= 0 && index < kMaxChatWindows; }

  std::array<ChatWindowInfo, kMaxChatWindows> windows_;
  mutable std::mutex mutex_;
};

}

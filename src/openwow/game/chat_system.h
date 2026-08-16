
#pragma once

#include "openwow/game/chat_manager.h"
#include "openwow/game/chat_types.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace openwow::game {

struct ChatChannel {
  std::string name;
  std::string display_name;
  std::uint32_t id = 0;
  std::uint32_t lookup_id = 0;
  std::uint32_t instance_id = 0;
  std::uint8_t flags = 0;
  std::uint32_t member_count = 0;
  bool is_joined = false;
  bool show_in_display = false;
  bool voice_enabled = false;
  bool requested_voice = false;
  bool permanent = false;
  bool lua_hidden = false;
  std::vector<std::string> members;

  [[nodiscard]] const std::string& DisplayNameOrName() const {
    return display_name.empty() ? name : display_name;
  }
};

struct ChannelRosterMember {
  std::uint64_t guid = 0;
  std::uint8_t flags = 0;
  std::string name;
  bool name_query_pending = false;
  std::uint8_t raw_flags = 0;
};

enum class DisplayChannelKind : std::uint32_t {
  kJoinedChannel = 0,
  kSpecialSlot1 = 1,
  kSpecialSlot2 = 2,
  kSpecialSlot3 = 3,
  kInvalid = 4,
};

enum class DisplayChannelCategory : std::uint8_t {
  kGroup = 0,
  kWorld = 1,
  kCustom = 2,
};

struct DisplayChannelEntry {
  DisplayChannelKind kind = DisplayChannelKind::kInvalid;
  std::string channel_name;
  std::optional<DisplayChannelCategory> category_override;
};

struct DisplayChannelInfo {
  DisplayChannelKind kind = DisplayChannelKind::kInvalid;
  DisplayChannelCategory category = DisplayChannelCategory::kGroup;
  std::string channel_name;
  std::string display_name;
  std::uint32_t channel_number = 0;
  std::uint32_t member_count = 0;
  bool is_header = false;
  bool collapsed = false;
  bool active = false;
  bool voice_enabled = false;
  bool selected = false;
};

struct ResolvedDisplayChannel {
  DisplayChannelKind kind = DisplayChannelKind::kInvalid;
  std::optional<ChatChannel> channel;
};

struct ChatColor {
  float r = 1.0f, g = 1.0f, b = 1.0f;
};

class ChatSystem {
public:
  static ChatSystem &Get();

  static constexpr std::size_t kMaxHistory = 500;
  static constexpr std::size_t kMaxNumberedChannelSlots = 10;

  void AddMessage(const ChatMessage &msg);

  [[nodiscard]] std::size_t GetMessageCount(ChatMsg type) const;

  [[nodiscard]] const ChatMessage *GetMessage(ChatMsg type, std::size_t index) const;

  [[nodiscard]] const std::vector<ChatMessage> &GetAllMessages() const;

  void SetFilterEnabled(ChatMsg type, bool enabled);
  [[nodiscard]] bool IsFilterEnabled(ChatMsg type) const;

  void ResetChannelConfiguration();
  void JoinChannel(const ChatChannel &channel);

  [[nodiscard]] std::optional<ChatChannel> QueuePendingNumberedChannel(
      const std::string& name,
      std::uint32_t lookup_id = 0,
      std::string_view display_name = {},
      bool requested_voice = false,
      bool permanent = false,
      std::uint32_t preferred_slot = 0);
  void LeaveChannel(const std::string &name);
  void UpdateChannel(const std::string &name, const ChatChannel &channel);
  [[nodiscard]] std::size_t GetNumChannels() const;
  [[nodiscard]] std::size_t GetChannelSlotCount() const;
  [[nodiscard]] const ChatChannel *GetChannel(std::size_t index) const;
  [[nodiscard]] std::vector<ChatChannel> GetChannelsSnapshot() const;
  [[nodiscard]] const ChatChannel *GetChannelById(std::uint32_t id) const;
  [[nodiscard]] const ChatChannel *GetChannelByLookupId(
      std::uint32_t lookup_id) const;
  [[nodiscard]] const ChatChannel *GetChannelBySlot(std::size_t index) const;

  [[nodiscard]] const ChatChannel *GetLuaChannelBySlot(std::size_t index) const;
  [[nodiscard]] const ChatChannel *GetLuaChannel(std::size_t index) const;
  [[nodiscard]] const ChatChannel *GetChannelByName(const std::string &name) const;

  [[nodiscard]] std::uint32_t GetVisibleChannelSlotByName(
      const std::string &name) const;

  void SetDisplayChannels(std::vector<DisplayChannelEntry> display_channels);
  void ResetDisplayChannelsToJoinedChannels();
  [[nodiscard]] std::size_t GetNumDisplayChannels() const;
  [[nodiscard]] std::optional<DisplayChannelInfo> GetDisplayChannelInfo(std::size_t index) const;
  [[nodiscard]] std::optional<ResolvedDisplayChannel>
  ResolveDisplayChannel(std::size_t index) const;
  bool SelectDisplayChannel(DisplayChannelKind kind, std::string channel_name = {});
  void ClearSelectedDisplayChannel();
  [[nodiscard]] std::optional<std::size_t> GetSelectedDisplayChannelIndex() const;
  [[nodiscard]] bool MatchesSelectedDisplayChannel(DisplayChannelKind kind,
                                                   std::string_view channel_name = {}) const;
  [[nodiscard]] bool ExpandDisplayChannelHeader(std::size_t index);
  [[nodiscard]] bool CollapseDisplayChannelHeader(std::size_t index);
  void SetSelectedJoinedChannelSelfFlags(const std::string& channel_name,
                                         std::uint8_t flags);
  [[nodiscard]] std::uint8_t GetSelectedJoinedChannelSelfFlags() const;
  void SelectWatchedJoinedChannel(const std::string& channel_name);
  void ClearWatchedChannelSelection();
  [[nodiscard]] bool IsWatchingJoinedChannel(const std::string& channel_name) const;
  [[nodiscard]] std::string GetWatchedJoinedChannelName() const;
  void ReplaceWatchedChannelRoster(const std::string& channel_name,
                                   std::vector<ChannelRosterMember> members,
                                   std::uint32_t pending_name_queries);
  bool AddWatchedChannelRosterMember(const std::string& channel_name,
                                     ChannelRosterMember member);
  bool RemoveWatchedChannelRosterMember(const std::string& channel_name,
                                        std::uint64_t guid);
  bool UpdateWatchedChannelRosterMemberFlags(const std::string& channel_name,
                                             std::uint64_t guid,
                                             std::uint8_t raw_flags,
                                             std::uint8_t display_flags);
  bool ResolveWatchedChannelRosterMemberName(std::uint64_t guid,
                                             const std::string& resolved_name,
                                             bool unresolved);
  void SortWatchedChannelRoster();
  [[nodiscard]] std::size_t GetWatchedChannelRosterSize() const;
  [[nodiscard]] std::optional<ChannelRosterMember>
  GetWatchedChannelRosterMember(std::size_t index) const;
  [[nodiscard]] std::uint32_t GetWatchedChannelRosterPendingQueries() const;

  void SetLastWhisperTarget(const std::string &name);
  [[nodiscard]] const std::string &GetLastWhisperTarget() const;

  static constexpr std::size_t kWhisperMruSize = 16;
  void RecordWhisperGuid(std::uint64_t guid);
  [[nodiscard]] bool IsInWhisperMru(std::uint64_t guid) const;
  [[nodiscard]] std::array<std::uint64_t, kWhisperMruSize> GetWhisperMruSnapshot() const;

  static constexpr std::size_t kRecentChatMruSize = 128;
  void RecordRecentChatGuid(std::uint64_t guid);
  [[nodiscard]] bool IsInRecentChatMru(std::uint64_t guid) const;

  [[nodiscard]] std::uint32_t GetDefaultLanguage() const;
  void SetDefaultLanguage(std::uint32_t language);

  static ChatColor GetDefaultColor(ChatMsg type);

  void Reset();

private:
  ChatSystem() = default;

  void TrimHistory();
  void InvalidateDisplayChannelCacheLocked();
  void RebuildDisplayChannelCacheLocked() const;
  [[nodiscard]] bool SetDisplayChannelHeaderCollapsedLocked(std::size_t index,
                                                            bool collapsed);
  void UpsertChannelLocked(const std::string& lookup_name, const ChatChannel& channel);
  [[nodiscard]] const ChatChannel* FindChannelByIdLocked(std::uint32_t id) const;
  [[nodiscard]] const ChatChannel* FindChannelByLookupIdLocked(
      std::uint32_t lookup_id) const;
  [[nodiscard]] const ChatChannel *FindChannelByNameLocked(const std::string &name) const;
  [[nodiscard]] ChatChannel *FindChannelByNameLocked(const std::string& name);
  [[nodiscard]] const ChatChannel *FindChannelBySlotLocked(std::size_t index) const;
  [[nodiscard]] std::size_t FindFirstFreeChannelSlotLocked() const;
  void SortWatchedChannelRosterLocked();

  std::vector<ChatMessage> all_messages_;
  std::unordered_map<std::uint8_t, std::vector<ChatMessage>> typed_messages_;
  std::unordered_map<std::uint8_t, bool> filters_;
  std::vector<ChatChannel> channels_;
  std::size_t channel_slot_count_ = 0;
  std::vector<DisplayChannelEntry> explicit_display_channels_;
  bool has_explicit_display_channels_ = false;
  std::array<bool, 3> collapsed_display_channel_headers_{};
  mutable std::vector<DisplayChannelInfo> display_channel_cache_;
  mutable bool display_channel_cache_valid_ = false;
  DisplayChannelKind selected_display_channel_kind_ = DisplayChannelKind::kInvalid;
  std::string selected_display_channel_name_;
  std::uint8_t selected_joined_channel_self_flags_ = 0;
  std::string watched_joined_channel_name_;
  std::vector<ChannelRosterMember> watched_channel_roster_;
  std::uint32_t watched_channel_roster_pending_queries_ = 0;
  std::string last_whisper_target_;
  std::uint32_t default_language_ = static_cast<std::uint32_t>(Language::kCommon);

  std::uint64_t whisper_mru_[kWhisperMruSize]{};
  std::uint64_t recent_chat_mru_[kRecentChatMruSize]{};

  mutable std::mutex mutex_;
};

}

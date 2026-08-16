
#include "openwow/game/chat_system.h"

#include "openwow/core/storm_string.h"
#include "openwow/game/chat_color_defaults.h"
#include "openwow/game/group_system.h"
#include "openwow/game/voice_chat.h"

#include <algorithm>
#include <cstring>

namespace openwow::game {

namespace {

constexpr std::size_t kDisplayChannelHeaderCount = 3;
constexpr std::array<DisplayChannelKind, 3> kAutomaticSpecialSlotOrder = {
    DisplayChannelKind::kSpecialSlot2,
    DisplayChannelKind::kSpecialSlot3,
    DisplayChannelKind::kSpecialSlot1,
};

bool ChannelNameEquals(const std::string& lhs, const std::string& rhs) {
  return openwow::core::SStrCmpUTF8NoCase(lhs.c_str(), rhs.c_str(), 0x7FFFFFFF) == 0;
}

bool ShouldIncludeDisplayChannel(const ChatChannel& channel) {
  return !channel.lua_hidden && (channel.is_joined || channel.show_in_display);
}

bool IsSelectedDisplayChannel(const DisplayChannelKind selected_kind,
                              const std::string& selected_name,
                              const DisplayChannelKind candidate_kind,
                              const std::string& candidate_name) {
  if (selected_kind != candidate_kind) {
    return false;
  }

  if (selected_kind != DisplayChannelKind::kJoinedChannel) {
    return selected_kind != DisplayChannelKind::kInvalid;
  }

  return !candidate_name.empty() && ChannelNameEquals(selected_name, candidate_name);
}

DisplayChannelCategory ClassifyDisplayChannelCategory(const ChatChannel& channel) {
  return channel.lookup_id != 0 ? DisplayChannelCategory::kWorld
                                : DisplayChannelCategory::kCustom;
}

std::uint32_t GetDisplayMemberCount(const ChatChannel& channel) {
  if (channel.member_count != 0) {
    return channel.member_count;
  }

  return static_cast<std::uint32_t>(channel.members.size());
}

int CompareWatchedChannelRosterMembersByResolvedName(
    const ChannelRosterMember& left, const ChannelRosterMember& right) {
  const bool left_missing_name = left.name.empty();
  const bool right_missing_name = right.name.empty();
  if (left_missing_name || right_missing_name) {
    if (left_missing_name == right_missing_name) {
      return 0;
    }
    return left_missing_name ? -1 : 1;
  }

  return openwow::core::SStrCmpNoCaseCollate(left.name.c_str(), right.name.c_str(),
                                             0x7FFFFFFF);
}

bool IsSpecialDisplayChannelKind(const DisplayChannelKind kind) {
  return kind == DisplayChannelKind::kSpecialSlot1 ||
         kind == DisplayChannelKind::kSpecialSlot2 ||
         kind == DisplayChannelKind::kSpecialSlot3;
}

VoiceChatChannelType ResolveSpecialSlotVoiceType(const DisplayChannelKind kind) {
  switch (kind) {
  case DisplayChannelKind::kSpecialSlot1:
    return VoiceChatChannelType::kBattleground;
  case DisplayChannelKind::kSpecialSlot2:
    return VoiceChatChannelType::kParty;
  case DisplayChannelKind::kSpecialSlot3:
    return VoiceChatChannelType::kRaid;
  case DisplayChannelKind::kJoinedChannel:
  case DisplayChannelKind::kInvalid:
    return VoiceChatChannelType::kCustom;
  }

  return VoiceChatChannelType::kCustom;
}

bool SpecialDisplayChannelVoiceEnabled(const DisplayChannelKind kind) {
  return VoiceChat::Get().HasChannelType(ResolveSpecialSlotVoiceType(kind));
}

std::uint32_t SpecialDisplayChannelMemberCount(const DisplayChannelKind kind) {
  auto& group_system = GroupSystem::Get();
  switch (kind) {
  case DisplayChannelKind::kSpecialSlot1:
  case DisplayChannelKind::kSpecialSlot3:
    return group_system.GetRealRaidMemberCount();
  case DisplayChannelKind::kSpecialSlot2: {
    const auto tracked_party_members = group_system.GetTrackedPartyMemberCount();
    return tracked_party_members != 0 || SpecialDisplayChannelVoiceEnabled(kind)
               ? tracked_party_members + 1
               : 0;
  }
  case DisplayChannelKind::kJoinedChannel:
  case DisplayChannelKind::kInvalid:
    return 0;
  }

  return 0;
}

bool ShouldShowSpecialDisplayChannel(const DisplayChannelKind kind) {
  auto& group_system = GroupSystem::Get();
  switch (kind) {
  case DisplayChannelKind::kSpecialSlot1:
    return group_system.IsBattlegroundGroup() && group_system.GetRealRaidMemberCount() != 0;
  case DisplayChannelKind::kSpecialSlot2:
    return group_system.GetTrackedPartyMemberCount() != 0 ||
           SpecialDisplayChannelVoiceEnabled(kind);
  case DisplayChannelKind::kSpecialSlot3:
    return group_system.GetRealRaidMemberCount() != 0 ||
           SpecialDisplayChannelVoiceEnabled(kind);
  case DisplayChannelKind::kJoinedChannel:
  case DisplayChannelKind::kInvalid:
    return false;
  }

  return false;
}

}

ChatSystem &ChatSystem::Get() {
  static ChatSystem instance;
  return instance;
}

void ChatSystem::AddMessage(const ChatMessage &msg) {
  std::lock_guard<std::mutex> lock(mutex_);

  all_messages_.push_back(msg);

  auto key = static_cast<std::uint8_t>(msg.type);
  typed_messages_[key].push_back(msg);

  if (msg.type == ChatMsg::kWhisper && !msg.sender_name.empty()) {
    last_whisper_target_ = msg.sender_name;
  }

  TrimHistory();
}

std::size_t ChatSystem::GetMessageCount(ChatMsg type) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto key = static_cast<std::uint8_t>(type);
  auto it = typed_messages_.find(key);
  if (it == typed_messages_.end())
    return 0;
  return it->second.size();
}

const ChatMessage *ChatSystem::GetMessage(ChatMsg type, std::size_t index) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto key = static_cast<std::uint8_t>(type);
  auto it = typed_messages_.find(key);
  if (it == typed_messages_.end())
    return nullptr;
  if (index >= it->second.size())
    return nullptr;
  return &it->second[index];
}

const std::vector<ChatMessage> &ChatSystem::GetAllMessages() const {

  return all_messages_;
}

void ChatSystem::SetFilterEnabled(ChatMsg type, bool enabled) {
  std::lock_guard<std::mutex> lock(mutex_);
  filters_[static_cast<std::uint8_t>(type)] = enabled;
}

bool ChatSystem::IsFilterEnabled(ChatMsg type) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = filters_.find(static_cast<std::uint8_t>(type));
  if (it == filters_.end())
    return false;
  return it->second;
}

void ChatSystem::ResetChannelConfiguration() {
  std::lock_guard<std::mutex> lock(mutex_);
  channels_.clear();
  channel_slot_count_ = 0;
  explicit_display_channels_.clear();
  has_explicit_display_channels_ = false;
  collapsed_display_channel_headers_.fill(false);
  display_channel_cache_.clear();
  display_channel_cache_valid_ = false;
  selected_display_channel_kind_ = DisplayChannelKind::kInvalid;
  selected_display_channel_name_.clear();
  selected_joined_channel_self_flags_ = 0;
  watched_joined_channel_name_.clear();
  watched_channel_roster_.clear();
  watched_channel_roster_pending_queries_ = 0;
}

void ChatSystem::JoinChannel(const ChatChannel &channel) {
  std::lock_guard<std::mutex> lock(mutex_);
  UpsertChannelLocked(channel.name, channel);
  InvalidateDisplayChannelCacheLocked();
}

std::optional<ChatChannel> ChatSystem::QueuePendingNumberedChannel(
    const std::string& name,
    const std::uint32_t lookup_id,
    const std::string_view display_name,
    const bool requested_voice,
    const bool permanent,
    const std::uint32_t preferred_slot) {
  std::lock_guard<std::mutex> lock(mutex_);

  std::size_t slot_index = kMaxNumberedChannelSlots;
  if (preferred_slot >= 1 && preferred_slot <= kMaxNumberedChannelSlots) {
    const auto preferred_index = static_cast<std::size_t>(preferred_slot - 1);
    if (FindChannelBySlotLocked(preferred_index) == nullptr) {
      slot_index = preferred_index;
    }
  }

  if (slot_index >= kMaxNumberedChannelSlots) {
    slot_index = FindFirstFreeChannelSlotLocked();
    if (slot_index >= kMaxNumberedChannelSlots) {
      return std::nullopt;
    }
  }

  ChatChannel channel;
  channel.name = name;
  channel.id = static_cast<std::uint32_t>(slot_index + 1);
  channel.lookup_id = lookup_id;
  if (!display_name.empty()) {
    channel.display_name.assign(display_name);
  }
  channel.requested_voice = requested_voice;
  channel.permanent = permanent;
  channel.lua_hidden = true;
  channels_.push_back(channel);
  channel_slot_count_ = std::max(channel_slot_count_, slot_index + 1);
  InvalidateDisplayChannelCacheLocked();

  return channels_.back();
}

void ChatSystem::LeaveChannel(const std::string &name) {
  std::lock_guard<std::mutex> lock(mutex_);
  channels_.erase(std::remove_if(channels_.begin(), channels_.end(),
                                 [&](const ChatChannel &ch) {
                                   return ChannelNameEquals(ch.name, name);
                                 }),
                  channels_.end());
  if (selected_display_channel_kind_ == DisplayChannelKind::kJoinedChannel &&
      ChannelNameEquals(selected_display_channel_name_, name)) {
    selected_joined_channel_self_flags_ = 0;
  }
  if (ChannelNameEquals(watched_joined_channel_name_, name)) {
    watched_joined_channel_name_.clear();
    watched_channel_roster_.clear();
    watched_channel_roster_pending_queries_ = 0;
  }
  InvalidateDisplayChannelCacheLocked();
}

void ChatSystem::UpdateChannel(const std::string &name, const ChatChannel &channel) {
  std::lock_guard<std::mutex> lock(mutex_);
  UpsertChannelLocked(name, channel);
  InvalidateDisplayChannelCacheLocked();
}

std::size_t ChatSystem::GetNumChannels() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return channels_.size();
}

std::size_t ChatSystem::GetChannelSlotCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return channel_slot_count_;
}

const ChatChannel *ChatSystem::GetChannel(std::size_t index) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (index >= channels_.size())
    return nullptr;
  return &channels_[index];
}

std::vector<ChatChannel> ChatSystem::GetChannelsSnapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return channels_;
}

const ChatChannel* ChatSystem::GetChannelById(const std::uint32_t id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return FindChannelByIdLocked(id);
}

const ChatChannel* ChatSystem::GetChannelByLookupId(
    const std::uint32_t lookup_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return FindChannelByLookupIdLocked(lookup_id);
}

const ChatChannel* ChatSystem::GetChannelBySlot(std::size_t index) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return FindChannelBySlotLocked(index);
}

const ChatChannel* ChatSystem::GetLuaChannelBySlot(std::size_t index) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto* channel = FindChannelBySlotLocked(index);
  if (channel == nullptr || channel->lua_hidden) {
    return nullptr;
  }

  return channel;
}

const ChatChannel* ChatSystem::GetLuaChannel(std::size_t index) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (index >= channels_.size()) {
    return nullptr;
  }

  const auto* channel = &channels_[index];
  if (channel->lua_hidden) {
    return nullptr;
  }

  return channel;
}

const ChatChannel *ChatSystem::GetChannelByName(const std::string &name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return FindChannelByNameLocked(name);
}

std::uint32_t ChatSystem::GetVisibleChannelSlotByName(
    const std::string &name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto &ch : channels_) {
    if (ChannelNameEquals(ch.name, name) && !ch.lua_hidden) {
      return ch.id;
    }
  }
  return 0;
}

void ChatSystem::SetDisplayChannels(std::vector<DisplayChannelEntry> display_channels) {
  std::lock_guard<std::mutex> lock(mutex_);
  explicit_display_channels_ = std::move(display_channels);
  has_explicit_display_channels_ = true;
  InvalidateDisplayChannelCacheLocked();
}

void ChatSystem::ResetDisplayChannelsToJoinedChannels() {
  std::lock_guard<std::mutex> lock(mutex_);
  has_explicit_display_channels_ = false;
  explicit_display_channels_.clear();
  InvalidateDisplayChannelCacheLocked();
}

std::size_t ChatSystem::GetNumDisplayChannels() const {
  std::lock_guard<std::mutex> lock(mutex_);
  RebuildDisplayChannelCacheLocked();
  return display_channel_cache_.size();
}

std::optional<DisplayChannelInfo> ChatSystem::GetDisplayChannelInfo(const std::size_t index) const {
  std::lock_guard<std::mutex> lock(mutex_);
  RebuildDisplayChannelCacheLocked();
  if (index >= display_channel_cache_.size()) {
    return std::nullopt;
  }

  return display_channel_cache_[index];
}

std::optional<ResolvedDisplayChannel> ChatSystem::ResolveDisplayChannel(std::size_t index) const {
  std::lock_guard<std::mutex> lock(mutex_);
  RebuildDisplayChannelCacheLocked();
  if (index >= display_channel_cache_.size()) {
    return std::nullopt;
  }

  const auto &display_channel = display_channel_cache_[index];
  if (display_channel.is_header || !display_channel.active ||
      display_channel.kind == DisplayChannelKind::kInvalid) {
    return std::nullopt;
  }

  ResolvedDisplayChannel resolved;
  resolved.kind = display_channel.kind;
  if (display_channel.kind != DisplayChannelKind::kJoinedChannel) {
    return resolved;
  }

  const auto *channel = FindChannelByNameLocked(display_channel.channel_name);
  if (!channel) {
    return std::nullopt;
  }

  resolved.channel = *channel;
  return resolved;
}

bool ChatSystem::SelectDisplayChannel(const DisplayChannelKind kind, std::string channel_name) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (kind != DisplayChannelKind::kJoinedChannel) {
    channel_name.clear();
  }

  const bool same_selection =
      selected_display_channel_kind_ == kind &&
      (kind != DisplayChannelKind::kJoinedChannel ||
       ChannelNameEquals(selected_display_channel_name_, channel_name));
  if (same_selection) {
    return false;
  }

  selected_joined_channel_self_flags_ = 0;
  selected_display_channel_kind_ = kind;
  selected_display_channel_name_ = std::move(channel_name);
  InvalidateDisplayChannelCacheLocked();
  return true;
}

void ChatSystem::ClearSelectedDisplayChannel() {
  std::lock_guard<std::mutex> lock(mutex_);
  selected_display_channel_kind_ = DisplayChannelKind::kInvalid;
  selected_display_channel_name_.clear();
  selected_joined_channel_self_flags_ = 0;
  InvalidateDisplayChannelCacheLocked();
}

std::optional<std::size_t> ChatSystem::GetSelectedDisplayChannelIndex() const {
  std::lock_guard<std::mutex> lock(mutex_);
  RebuildDisplayChannelCacheLocked();
  if (selected_display_channel_kind_ == DisplayChannelKind::kInvalid) {
    return std::nullopt;
  }

  for (std::size_t index = 0; index < display_channel_cache_.size(); ++index) {
    const auto& display_channel = display_channel_cache_[index];
    if (display_channel.is_header || !display_channel.active ||
        display_channel.kind != selected_display_channel_kind_) {
      continue;
    }

    if (selected_display_channel_kind_ != DisplayChannelKind::kJoinedChannel) {
      return index;
    }

    const auto* channel = FindChannelByNameLocked(display_channel.channel_name);
    if (channel != nullptr &&
        ChannelNameEquals(channel->name, selected_display_channel_name_)) {
      return index;
    }
  }

  return std::nullopt;
}

bool ChatSystem::MatchesSelectedDisplayChannel(const DisplayChannelKind kind,
                                               const std::string_view channel_name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (selected_display_channel_kind_ != kind) {
    return false;
  }

  if (kind != DisplayChannelKind::kJoinedChannel) {
    return true;
  }

  return ChannelNameEquals(selected_display_channel_name_, std::string(channel_name));
}

bool ChatSystem::ExpandDisplayChannelHeader(const std::size_t index) {
  std::lock_guard<std::mutex> lock(mutex_);
  return SetDisplayChannelHeaderCollapsedLocked(index, false);
}

bool ChatSystem::CollapseDisplayChannelHeader(const std::size_t index) {
  std::lock_guard<std::mutex> lock(mutex_);
  return SetDisplayChannelHeaderCollapsedLocked(index, true);
}

void ChatSystem::SetSelectedJoinedChannelSelfFlags(const std::string& channel_name,
                                                   const std::uint8_t flags) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (selected_display_channel_kind_ != DisplayChannelKind::kJoinedChannel ||
      !ChannelNameEquals(selected_display_channel_name_, channel_name)) {
    return;
  }

  selected_joined_channel_self_flags_ = flags;
}

std::uint8_t ChatSystem::GetSelectedJoinedChannelSelfFlags() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (selected_display_channel_kind_ != DisplayChannelKind::kJoinedChannel) {
    return 0;
  }

  return selected_joined_channel_self_flags_;
}

void ChatSystem::SelectWatchedJoinedChannel(const std::string& channel_name) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (ChannelNameEquals(watched_joined_channel_name_, channel_name)) {
    return;
  }

  watched_joined_channel_name_ = channel_name;
  watched_channel_roster_.clear();
  watched_channel_roster_pending_queries_ = 0;
}

void ChatSystem::ClearWatchedChannelSelection() {
  std::lock_guard<std::mutex> lock(mutex_);
  watched_joined_channel_name_.clear();
  watched_channel_roster_.clear();
  watched_channel_roster_pending_queries_ = 0;
}

bool ChatSystem::IsWatchingJoinedChannel(const std::string& channel_name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return ChannelNameEquals(watched_joined_channel_name_, channel_name);
}

std::string ChatSystem::GetWatchedJoinedChannelName() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return watched_joined_channel_name_;
}

void ChatSystem::ReplaceWatchedChannelRoster(const std::string& channel_name,
                                             std::vector<ChannelRosterMember> members,
                                             const std::uint32_t pending_name_queries) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (watched_joined_channel_name_ != channel_name) {
    return;
  }

  watched_channel_roster_ = std::move(members);
  watched_channel_roster_pending_queries_ = pending_name_queries;
  if (watched_channel_roster_pending_queries_ == 0) {
    SortWatchedChannelRosterLocked();
  }
}

bool ChatSystem::AddWatchedChannelRosterMember(const std::string& channel_name,
                                               ChannelRosterMember member) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (watched_joined_channel_name_ != channel_name) {
    return false;
  }

  if (member.name_query_pending) {
    ++watched_channel_roster_pending_queries_;
  }
  watched_channel_roster_.push_back(std::move(member));
  return true;
}

bool ChatSystem::RemoveWatchedChannelRosterMember(const std::string& channel_name,
                                                  const std::uint64_t guid) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (watched_joined_channel_name_ != channel_name) {
    return false;
  }

  const auto it = std::find_if(
      watched_channel_roster_.begin(), watched_channel_roster_.end(),
      [guid](const ChannelRosterMember& member) { return member.guid == guid; });
  if (it == watched_channel_roster_.end()) {
    return false;
  }

  if (it->name_query_pending && watched_channel_roster_pending_queries_ != 0) {
    --watched_channel_roster_pending_queries_;
  }

  watched_channel_roster_.erase(it);
  return true;
}

bool ChatSystem::UpdateWatchedChannelRosterMemberFlags(const std::string& channel_name,
                                                       const std::uint64_t guid,
                                                       const std::uint8_t raw_flags,
                                                       const std::uint8_t display_flags) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (watched_joined_channel_name_ != channel_name) {
    return false;
  }

  for (auto& member : watched_channel_roster_) {
    if (member.guid != guid) {
      continue;
    }

    member.raw_flags = raw_flags;
    member.flags = display_flags;
    return true;
  }

  return false;
}

bool ChatSystem::ResolveWatchedChannelRosterMemberName(const std::uint64_t guid,
                                                       const std::string& resolved_name,
                                                       const bool unresolved) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& member : watched_channel_roster_) {
    if (member.guid != guid || !member.name_query_pending) {
      continue;
    }

    member.name_query_pending = false;
    member.name = unresolved ? std::string() : resolved_name;
    if (watched_channel_roster_pending_queries_ != 0) {
      --watched_channel_roster_pending_queries_;
    }
    if (watched_channel_roster_pending_queries_ == 0) {
      SortWatchedChannelRosterLocked();
    }
    return true;
  }

  return false;
}

void ChatSystem::SortWatchedChannelRoster() {
  std::lock_guard<std::mutex> lock(mutex_);
  SortWatchedChannelRosterLocked();
}

std::size_t ChatSystem::GetWatchedChannelRosterSize() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return watched_channel_roster_.size();
}

std::optional<ChannelRosterMember>
ChatSystem::GetWatchedChannelRosterMember(const std::size_t index) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (index >= watched_channel_roster_.size()) {
    return std::nullopt;
  }

  return watched_channel_roster_[index];
}

std::uint32_t ChatSystem::GetWatchedChannelRosterPendingQueries() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return watched_channel_roster_pending_queries_;
}

void ChatSystem::SetLastWhisperTarget(const std::string &name) {
  std::lock_guard<std::mutex> lock(mutex_);
  last_whisper_target_ = name;
}

const std::string &ChatSystem::GetLastWhisperTarget() const {

  return last_whisper_target_;
}

void ChatSystem::RecordWhisperGuid(std::uint64_t guid) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (guid == 0)
    return;

  std::size_t pos = 0;
  while (pos < kWhisperMruSize && whisper_mru_[pos] != guid)
    ++pos;
  if (pos >= kWhisperMruSize)
    pos = kWhisperMruSize - 1;

  if (pos > 0)
    std::memmove(&whisper_mru_[1], &whisper_mru_[0], pos * sizeof(std::uint64_t));
  whisper_mru_[0] = guid;
}

bool ChatSystem::IsInWhisperMru(std::uint64_t guid) const {
  std::lock_guard<std::mutex> lock(mutex_);
  for (std::size_t i = 0; i < kWhisperMruSize; ++i) {
    if (whisper_mru_[i] == guid)
      return true;
  }
  return false;
}

std::array<std::uint64_t, ChatSystem::kWhisperMruSize> ChatSystem::GetWhisperMruSnapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::array<std::uint64_t, kWhisperMruSize> snapshot{};
  std::copy(std::begin(whisper_mru_), std::end(whisper_mru_), snapshot.begin());
  return snapshot;
}

void ChatSystem::RecordRecentChatGuid(std::uint64_t guid) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (guid == 0)
    return;

  std::size_t pos = 0;
  while (pos < kRecentChatMruSize && recent_chat_mru_[pos] != guid)
    ++pos;
  if (pos >= kRecentChatMruSize)
    pos = kRecentChatMruSize - 1;

  if (pos > 0)
    std::memmove(&recent_chat_mru_[1], &recent_chat_mru_[0], pos * sizeof(std::uint64_t));
  recent_chat_mru_[0] = guid;
}

bool ChatSystem::IsInRecentChatMru(std::uint64_t guid) const {
  std::lock_guard<std::mutex> lock(mutex_);
  for (std::size_t i = 0; i < kRecentChatMruSize; ++i) {
    if (recent_chat_mru_[i] == guid)
      return true;
  }
  return false;
}

std::uint32_t ChatSystem::GetDefaultLanguage() const {
  return default_language_;
}

void ChatSystem::SetDefaultLanguage(std::uint32_t language) {
  default_language_ = language;
}

ChatColor ChatSystem::GetDefaultColor(ChatMsg type) {
  if (const auto* entry = GetBuiltinChatColorDefault(type)) {
    return {DecodeChatColorByte(entry->r), DecodeChatColorByte(entry->g),
            DecodeChatColorByte(entry->b)};
  }

  return {1.0f, 1.0f, 1.0f};
}

void ChatSystem::Reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  all_messages_.clear();
  typed_messages_.clear();
  filters_.clear();
  channels_.clear();
  channel_slot_count_ = 0;
  explicit_display_channels_.clear();
  has_explicit_display_channels_ = false;
  collapsed_display_channel_headers_.fill(false);
  display_channel_cache_.clear();
  display_channel_cache_valid_ = false;
  selected_display_channel_kind_ = DisplayChannelKind::kInvalid;
  selected_display_channel_name_.clear();
  selected_joined_channel_self_flags_ = 0;
  watched_joined_channel_name_.clear();
  watched_channel_roster_.clear();
  watched_channel_roster_pending_queries_ = 0;
  last_whisper_target_.clear();
  default_language_ = static_cast<std::uint32_t>(Language::kCommon);
  std::fill(std::begin(whisper_mru_), std::end(whisper_mru_), 0ULL);
  std::fill(std::begin(recent_chat_mru_), std::end(recent_chat_mru_), 0ULL);
}

void ChatSystem::TrimHistory() {

  if (all_messages_.size() > kMaxHistory) {
    auto excess = all_messages_.size() - kMaxHistory;
    all_messages_.erase(all_messages_.begin(),
                        all_messages_.begin() + static_cast<std::ptrdiff_t>(excess));
  }

  for (auto &[key, msgs] : typed_messages_) {
    if (msgs.size() > kMaxHistory) {
      auto excess = msgs.size() - kMaxHistory;
      msgs.erase(msgs.begin(), msgs.begin() + static_cast<std::ptrdiff_t>(excess));
    }
  }
}

void ChatSystem::InvalidateDisplayChannelCacheLocked() {
  display_channel_cache_valid_ = false;
}

bool ChatSystem::SetDisplayChannelHeaderCollapsedLocked(const std::size_t index,
                                                        const bool collapsed) {
  RebuildDisplayChannelCacheLocked();
  if (index >= display_channel_cache_.size()) {
    return false;
  }

  const auto& display_channel = display_channel_cache_[index];
  if (!display_channel.is_header) {
    return false;
  }

  const auto category_index = static_cast<std::size_t>(display_channel.category);
  if (category_index >= collapsed_display_channel_headers_.size()) {
    return false;
  }

  collapsed_display_channel_headers_[category_index] = collapsed;
  InvalidateDisplayChannelCacheLocked();
  return true;
}

void ChatSystem::RebuildDisplayChannelCacheLocked() const {
  if (display_channel_cache_valid_) {
    return;
  }

  display_channel_cache_.clear();

  auto append_joined_channel = [this](const ChatChannel& channel,
                                      const DisplayChannelCategory category) {
    DisplayChannelInfo info;
    info.kind = DisplayChannelKind::kJoinedChannel;
    info.category = category;
    info.channel_name = channel.name;
    info.display_name = channel.DisplayNameOrName();
    info.channel_number = channel.id;
    info.member_count = GetDisplayMemberCount(channel);
    info.active = channel.is_joined;
    info.voice_enabled = channel.voice_enabled;
    info.selected = IsSelectedDisplayChannel(selected_display_channel_kind_,
                                             selected_display_channel_name_,
                                             DisplayChannelKind::kJoinedChannel, channel.name);
    display_channel_cache_.push_back(std::move(info));
  };

  auto append_header = [this](const DisplayChannelCategory category, const std::uint32_t count) {
    DisplayChannelInfo info;
    info.is_header = true;
    info.category = category;
    info.collapsed = collapsed_display_channel_headers_[static_cast<std::size_t>(category)];
    info.member_count = count;
    display_channel_cache_.push_back(std::move(info));
  };

  auto build_special_channel_info = [this](const DisplayChannelKind kind) {
    DisplayChannelInfo info;
    info.kind = kind;
    info.category = DisplayChannelCategory::kGroup;
    info.member_count = SpecialDisplayChannelMemberCount(kind);
    info.active = ShouldShowSpecialDisplayChannel(kind);
    info.voice_enabled = SpecialDisplayChannelVoiceEnabled(kind);
    info.selected = IsSelectedDisplayChannel(selected_display_channel_kind_,
                                             selected_display_channel_name_, kind, {});
    return info;
  };

  if (has_explicit_display_channels_) {
    display_channel_cache_.reserve(explicit_display_channels_.size());
    for (const auto& display_channel : explicit_display_channels_) {
      if (IsSpecialDisplayChannelKind(display_channel.kind)) {
        display_channel_cache_.push_back(build_special_channel_info(display_channel.kind));
        continue;
      }

      DisplayChannelInfo info;
      info.kind = display_channel.kind;
      if (display_channel.kind == DisplayChannelKind::kJoinedChannel) {
        info.channel_name = display_channel.channel_name;
      }

      if (display_channel.kind == DisplayChannelKind::kJoinedChannel) {
        if (const auto* channel = FindChannelByNameLocked(display_channel.channel_name);
            channel != nullptr) {
          info.category = display_channel.category_override.value_or(
              ClassifyDisplayChannelCategory(*channel));
          info.display_name = channel->DisplayNameOrName();
          info.channel_number = channel->id;
          info.member_count = GetDisplayMemberCount(*channel);
          info.active = channel->is_joined;
          info.voice_enabled = channel->voice_enabled;
          info.selected = IsSelectedDisplayChannel(selected_display_channel_kind_,
                                                   selected_display_channel_name_,
                                                   DisplayChannelKind::kJoinedChannel,
                                                   channel->name);
        } else {
          info.category = display_channel.category_override.value_or(
              DisplayChannelCategory::kCustom);
          info.display_name = display_channel.channel_name;
        }
      } else {
        info.selected = IsSelectedDisplayChannel(selected_display_channel_kind_,
                                                 selected_display_channel_name_,
                                                 display_channel.kind, {});
      }

      display_channel_cache_.push_back(std::move(info));
    }

    display_channel_cache_valid_ = true;
    return;
  }

  std::array<std::vector<const ChatChannel*>, kDisplayChannelHeaderCount> grouped_channels{};
  for (const auto& channel : channels_) {
    if (!ShouldIncludeDisplayChannel(channel)) {
      continue;
    }

    const auto category = static_cast<std::size_t>(ClassifyDisplayChannelCategory(channel));
    grouped_channels[category].push_back(&channel);
  }

  std::uint32_t visible_special_channel_count = 0;
  for (const auto special_kind : kAutomaticSpecialSlotOrder) {
    if (ShouldShowSpecialDisplayChannel(special_kind)) {
      ++visible_special_channel_count;
    }
  }

  append_header(DisplayChannelCategory::kGroup, visible_special_channel_count);
  if (!collapsed_display_channel_headers_[static_cast<std::size_t>(DisplayChannelCategory::kGroup)]) {
    for (const auto special_kind : kAutomaticSpecialSlotOrder) {
      if (!ShouldShowSpecialDisplayChannel(special_kind)) {
        continue;
      }

      display_channel_cache_.push_back(build_special_channel_info(special_kind));
    }
  }

  for (std::size_t category_index = 1; category_index < grouped_channels.size(); ++category_index) {
    const auto category = static_cast<DisplayChannelCategory>(category_index);
    append_header(category, static_cast<std::uint32_t>(grouped_channels[category_index].size()));
    if (collapsed_display_channel_headers_[category_index]) {
      continue;
    }

    for (const auto* channel : grouped_channels[category_index]) {
      append_joined_channel(*channel, category);
    }
  }

  display_channel_cache_valid_ = true;
}

void ChatSystem::UpsertChannelLocked(const std::string& lookup_name, const ChatChannel& channel) {
  auto matches_channel = [&](const ChatChannel& existing) {
    if (channel.id != 0 && existing.id == channel.id) {
      return true;
    }
    if (!lookup_name.empty() && ChannelNameEquals(existing.name, lookup_name)) {
      return true;
    }
    return !channel.name.empty() && ChannelNameEquals(existing.name, channel.name);
  };

  std::size_t replacement_index = channels_.size();
  for (std::size_t index = 0; index < channels_.size(); ++index) {
    if (!matches_channel(channels_[index])) {
      continue;
    }

    if (replacement_index == channels_.size()) {
      replacement_index = index;
      channels_[index] = channel;
      continue;
    }

    channels_.erase(channels_.begin() + index);
    --index;
  }

  if (replacement_index == channels_.size()) {
    channels_.push_back(channel);
  }

  if (channel.id != 0) {
    channel_slot_count_ = std::max(
        channel_slot_count_, static_cast<std::size_t>(channel.id));
  }
}

const ChatChannel* ChatSystem::FindChannelByIdLocked(const std::uint32_t id) const {
  if (id == 0) {
    return nullptr;
  }

  for (const auto& channel : channels_) {
    if (channel.id == id) {
      return &channel;
    }
  }

  return nullptr;
}

const ChatChannel* ChatSystem::FindChannelByLookupIdLocked(
    const std::uint32_t lookup_id) const {
  if (lookup_id == 0) {
    return nullptr;
  }

  for (const auto& channel : channels_) {
    if (channel.lookup_id == lookup_id) {
      return &channel;
    }
  }

  return nullptr;
}

const ChatChannel *ChatSystem::FindChannelByNameLocked(const std::string &name) const {
  for (const auto &ch : channels_) {
    if (ChannelNameEquals(ch.name, name)) {
      return &ch;
    }
  }
  return nullptr;
}

ChatChannel* ChatSystem::FindChannelByNameLocked(const std::string& name) {
  for (auto& ch : channels_) {
    if (ChannelNameEquals(ch.name, name)) {
      return &ch;
    }
  }
  return nullptr;
}

const ChatChannel* ChatSystem::FindChannelBySlotLocked(const std::size_t index) const {
  if (index >= channel_slot_count_) {
    return nullptr;
  }

  const auto slot_id = static_cast<std::uint32_t>(index + 1);
  for (const auto& channel : channels_) {
    if (channel.id == slot_id) {
      return &channel;
    }
  }

  return nullptr;
}

std::size_t ChatSystem::FindFirstFreeChannelSlotLocked() const {
  for (std::size_t index = 0; index < channel_slot_count_; ++index) {
    if (FindChannelBySlotLocked(index) == nullptr) {
      return index;
    }
  }

  return channel_slot_count_;
}

void ChatSystem::SortWatchedChannelRosterLocked() {
  std::stable_sort(
      watched_channel_roster_.begin(), watched_channel_roster_.end(),
      [](const ChannelRosterMember& left, const ChannelRosterMember& right) {
        return CompareWatchedChannelRosterMembersByResolvedName(left, right) < 0;
      });
}

}

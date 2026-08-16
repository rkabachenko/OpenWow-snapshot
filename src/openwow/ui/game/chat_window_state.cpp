
#include "openwow/ui/game/chat_window_state.h"

#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/foundation/text/ascii.h"

#include <array>
#include <string>
#include <string_view>

namespace openwow::ui::game {

namespace {

struct ChatWindowMessageGroupDefinition {
  std::string_view token;
  bool default_enabled;
  int introduced_added_version;
};

constexpr std::size_t kDefaultChatWindowChannelCapacity = 10;
constexpr float kDefaultChatWindowBackgroundAlpha = 40.0f / 255.0f;
constexpr int kDefaultChatWindowAnchorPoint = 6;
constexpr std::size_t kChatWindowNameStorageBytes = 128;

constexpr std::size_t kPrimaryChatWindowDefaultGroupCount = 40;

constexpr std::array<ChatWindowMessageGroupDefinition, 46> kChatWindowMessageGroups{{
    {"SYSTEM", true, 0},
    {"SYSTEM_NOMENU", true, 5},
    {"SAY", true, 0},
    {"EMOTE", true, 7},
    {"YELL", true, 0},
    {"WHISPER", true, 0},
    {"PARTY", true, 0},
    {"PARTY_LEADER", true, 10},
    {"RAID", true, 7},
    {"RAID_LEADER", true, 7},
    {"RAID_WARNING", true, 7},
    {"BATTLEGROUND", true, 7},
    {"BATTLEGROUND_LEADER", true, 7},
    {"GUILD", true, 0},
    {"OFFICER", true, 7},
    {"MONSTER_SAY", true, 7},
    {"MONSTER_YELL", true, 7},
    {"MONSTER_EMOTE", true, 7},
    {"MONSTER_WHISPER", true, 7},
    {"MONSTER_BOSS_EMOTE", true, 7},
    {"MONSTER_BOSS_WHISPER", true, 7},
    {"ERRORS", true, 7},
    {"AFK", true, 7},
    {"DND", true, 7},
    {"IGNORED", true, 7},
    {"BG_HORDE", true, 7},
    {"BG_ALLIANCE", true, 7},
    {"BG_NEUTRAL", true, 7},
    {"COMBAT_FACTION_CHANGE", true, 1},
    {"SKILL", true, 0},
    {"LOOT", true, 0},
    {"MONEY", true, 2},
    {"CHANNEL", true, 0},
    {"ACHIEVEMENT", true, 8},
    {"GUILD_ACHIEVEMENT", true, 8},
    {"TARGETICONS", false, 11},
    {"BN_WHISPER", true, 12},
    {"BN_WHISPER_INFORM", true, 12},
    {"BN_CONVERSATION", true, 12},
    {"BN_INLINE_TOAST_ALERT", true, 13},
    {"OPENING", true, 6},
    {"TRADESKILLS", true, 6},
    {"PET_INFO", true, 6},
    {"COMBAT_XP_GAIN", true, 0},
    {"COMBAT_HONOR_GAIN", true, 0},
    {"COMBAT_MISC_INFO", true, 0},
}};

std::optional<std::size_t> FindMessageGroupDefinition(std::string_view group) {
  for (std::size_t index = 0; index < kChatWindowMessageGroups.size(); ++index) {
    if (openwow::text::EqualsIgnoreCaseAscii(kChatWindowMessageGroups[index].token,
                                             group)) {
      return index;
    }
  }
  return std::nullopt;
}

void AssignDefaultMessageGroupRange(ChatWindowInfo& window,
                                    const std::size_t first_index,
                                    const std::size_t past_last_index,
                                    const bool require_default_enabled) {
  window.message_groups.clear();
  window.message_groups.reserve(past_last_index - first_index);
  for (std::size_t index = first_index; index < past_last_index; ++index) {
    const auto& group = kChatWindowMessageGroups[index];
    if (!require_default_enabled || group.default_enabled) {
      window.message_groups.emplace_back(group.token);
    }
  }
}

void ResetWindowToIdaDefaults(ChatWindowInfo& window) {
  window.name.clear();
  window.font_size = 0.0f;
  window.r = 0.0f;
  window.g = 0.0f;
  window.b = 0.0f;
  window.alpha = kDefaultChatWindowBackgroundAlpha;
  window.shown = false;
  window.locked = true;
  window.dock_target = 0;
  window.uninteractable = false;
  window.saved_layout_is_set = false;
  window.saved_position.point = kDefaultChatWindowAnchorPoint;
  window.saved_position.x = 0.0f;
  window.saved_position.y = 0.0f;
  window.saved_position.is_set = false;
  window.saved_dimensions.width = 430.0f;
  window.saved_dimensions.height = 120.0f;
  window.channels.clear();
  window.channels.reserve(kDefaultChatWindowChannelCapacity);
  window.message_groups.clear();
}

void ApplyResetChatWindowsDefaults(std::array<ChatWindowInfo, kMaxChatWindows>& windows) {
  for (auto& window : windows) {
    ResetWindowToIdaDefaults(window);
  }

  windows[0].shown = true;
  windows[0].dock_target = 1;
  AssignDefaultMessageGroupRange(windows[0], 0, kPrimaryChatWindowDefaultGroupCount, true);

  windows[1].shown = true;
  windows[1].dock_target = 2;
  AssignDefaultMessageGroupRange(windows[1], kPrimaryChatWindowDefaultGroupCount,
                                 kChatWindowMessageGroups.size(), false);
}

void InsertMessageGroupInCanonicalOrder(std::vector<std::string>& groups,
                                        const std::size_t canonical_index) {
  const auto canonical_name = kChatWindowMessageGroups[canonical_index].token;
  for (const auto& existing : groups) {
    if (openwow::text::EqualsIgnoreCaseAscii(existing, canonical_name)) {
      return;
    }
  }

  const auto insertion = std::find_if(
      groups.begin(), groups.end(), [canonical_index](const std::string& existing) {
        const auto existing_index = FindMessageGroupDefinition(existing);
        return existing_index.has_value() && *existing_index > canonical_index;
      });
  groups.insert(insertion, std::string(canonical_name));
}

}

ChatWindowState& ChatWindowState::Get() {
  static ChatWindowState instance;
  return instance;
}

ChatWindowState::ChatWindowState() { ApplyResetChatWindowsDefaults(windows_); }

void ChatWindowState::SetWindowName(int index, std::string_view name) {
  std::lock_guard lock(mutex_);
  if (!ValidIndex(index)) {
    return;
  }

  auto& window = windows_[static_cast<std::size_t>(index)];
  if (name.empty()) {
    window.name.clear();
    return;
  }

  const auto max_name_size = kChatWindowNameStorageBytes - 1;
  window.name.assign(name.data(), std::min(name.size(), max_name_size));
}

void ChatWindowState::SetWindowFontSize(int index, int font_size) {
  std::lock_guard lock(mutex_);
  if (!ValidIndex(index) || font_size <= 0) {
    return;
  }

  windows_[static_cast<std::size_t>(index)].font_size = static_cast<float>(font_size);
}

void ChatWindowState::SetWindowAlpha(int index, float alpha) {
  std::lock_guard lock(mutex_);
  if (!ValidIndex(index)) return;
  windows_[static_cast<std::size_t>(index)].alpha = alpha;
}

void ChatWindowState::SetWindowColor(int index, float r, float g, float b) {
  std::lock_guard lock(mutex_);
  if (!ValidIndex(index)) return;
  auto& w = windows_[static_cast<std::size_t>(index)];
  w.r = r;
  w.g = g;
  w.b = b;
}

void ChatWindowState::SetWindowDockTarget(int index, int dock_target) {
  std::lock_guard lock(mutex_);
  if (!ValidIndex(index)) return;
  windows_[static_cast<std::size_t>(index)].dock_target = dock_target;
}

void ChatWindowState::SetWindowLocked(int index, bool locked) {
  std::lock_guard lock(mutex_);
  if (!ValidIndex(index)) return;
  windows_[static_cast<std::size_t>(index)].locked = locked;
}

void ChatWindowState::SetWindowShown(int index, bool shown) {
  std::lock_guard lock(mutex_);
  if (!ValidIndex(index)) return;
  windows_[static_cast<std::size_t>(index)].shown = shown;
}

void ChatWindowState::SetWindowUninteractable(int index, bool uninteractable) {
  std::lock_guard lock(mutex_);
  if (!ValidIndex(index)) return;
  windows_[static_cast<std::size_t>(index)].uninteractable = uninteractable;
}

std::optional<ChatWindowInfo> ChatWindowState::TryGetWindow(int index) const {
  std::lock_guard lock(mutex_);
  if (!ValidIndex(index)) return std::nullopt;
  return windows_[static_cast<std::size_t>(index)];
}

void ChatWindowState::AddChannel(int window, const std::string& channel,
                                 std::uint32_t channel_number) {
  std::lock_guard lock(mutex_);
  if (!ValidIndex(window) || channel.empty()) return;
  auto& channel_slots = windows_[static_cast<std::size_t>(window)].channels;

  for (const auto& slot : channel_slots) {
    if (slot.has_value() &&
        detail::GameUiLookupMatches(slot->name, channel, true)) {
      return;
    }
  }

  const auto first_empty_slot =
      std::find_if(channel_slots.begin(), channel_slots.end(),
                   [](const std::optional<ChatWindowChannel>& slot) {
                     return !slot.has_value();
                   });
  if (first_empty_slot != channel_slots.end()) {
    *first_empty_slot = ChatWindowChannel{channel, channel_number};
  } else {
    channel_slots.emplace_back(ChatWindowChannel{channel, channel_number});
  }
}

void ChatWindowState::RemoveChannel(int window, const std::string& channel) {
  std::lock_guard lock(mutex_);
  if (!ValidIndex(window) || channel.empty()) return;
  auto& channel_slots = windows_[static_cast<std::size_t>(window)].channels;
  for (auto& slot : channel_slots) {
    if (slot.has_value() &&
        detail::GameUiLookupMatches(slot->name, channel, true)) {
      slot.reset();
      return;
    }
  }
}

void ChatWindowState::RemoveChannelFromAllWindows(const std::string& channel) {
  std::lock_guard lock(mutex_);
  if (channel.empty()) return;

  for (auto& window : windows_) {
    for (auto& slot : window.channels) {
      if (slot.has_value() &&
          detail::GameUiLookupMatches(slot->name, channel, true)) {
        slot.reset();
      }
    }
  }
}

bool ChatWindowState::HasChannel(int window, const std::string& channel) const {
  std::lock_guard lock(mutex_);
  if (!ValidIndex(window) || channel.empty()) return false;
  const auto& channel_slots = windows_[static_cast<std::size_t>(window)].channels;
  return std::find_if(channel_slots.begin(), channel_slots.end(),
                      [&](const std::optional<ChatWindowChannel>& slot) {
                        return slot.has_value() &&
                               detail::GameUiLookupMatches(slot->name, channel, true);
                      }) != channel_slots.end();
}

std::size_t ChatWindowState::GetChannelCount(int window) const {
  std::lock_guard lock(mutex_);
  if (!ValidIndex(window)) return 0;
  const auto& channel_slots = windows_[static_cast<std::size_t>(window)].channels;
  return static_cast<std::size_t>(std::count_if(
      channel_slots.begin(), channel_slots.end(),
      [](const std::optional<ChatWindowChannel>& slot) { return slot.has_value(); }));
}

std::vector<ChatWindowChannel> ChatWindowState::GetChannels(int window) const {
  std::lock_guard lock(mutex_);
  if (!ValidIndex(window)) return {};

  const auto& channel_slots = windows_[static_cast<std::size_t>(window)].channels;
  std::vector<ChatWindowChannel> channels;
  channels.reserve(channel_slots.size());
  for (const auto& slot : channel_slots) {
    if (slot.has_value()) {
      channels.push_back(*slot);
    }
  }
  return channels;
}

void ChatWindowState::AddMessageGroup(int window, const std::string& group) {
  std::lock_guard lock(mutex_);
  if (!ValidIndex(window) || group.empty()) return;
  const auto message_group = FindMessageGroupDefinition(group);
  if (!message_group.has_value()) {
    return;
  }
  auto& groups = windows_[static_cast<std::size_t>(window)].message_groups;
  InsertMessageGroupInCanonicalOrder(groups, *message_group);
}

void ChatWindowState::RemoveMessageGroup(int window, const std::string& group) {
  std::lock_guard lock(mutex_);
  if (!ValidIndex(window) || group.empty()) return;
  const auto message_group = FindMessageGroupDefinition(group);
  if (!message_group.has_value()) {
    return;
  }
  auto& groups = windows_[static_cast<std::size_t>(window)].message_groups;
  const auto canonical_name = kChatWindowMessageGroups[*message_group].token;
  groups.erase(std::remove_if(groups.begin(), groups.end(),
                              [canonical_name](const std::string& existing) {
                                return openwow::text::EqualsIgnoreCaseAscii(existing,
                                                                            canonical_name);
                              }),
               groups.end());
}

bool ChatWindowState::HasMessageGroup(int window, const std::string& group) const {
  std::lock_guard lock(mutex_);
  if (!ValidIndex(window) || group.empty()) return false;
  const auto message_group = FindMessageGroupDefinition(group);
  if (!message_group.has_value()) {
    return false;
  }
  const auto& groups = windows_[static_cast<std::size_t>(window)].message_groups;
  const auto canonical_name = kChatWindowMessageGroups[*message_group].token;
  return std::find_if(groups.begin(), groups.end(),
                      [canonical_name](const std::string& existing) {
                        return openwow::text::EqualsIgnoreCaseAscii(existing, canonical_name);
                      }) != groups.end();
}

std::size_t ChatWindowState::GetMessageGroupCount(int window) const {
  std::lock_guard lock(mutex_);
  if (!ValidIndex(window)) return 0;
  return windows_[static_cast<std::size_t>(window)].message_groups.size();
}

std::vector<std::string> ChatWindowState::GetMessageGroups(int window) const {
  std::lock_guard lock(mutex_);
  if (!ValidIndex(window)) return {};
  return windows_[static_cast<std::size_t>(window)].message_groups;
}

std::array<ChatWindowInfo, kMaxChatWindows> ChatWindowState::GetWindowsSnapshot() const {
  std::lock_guard lock(mutex_);
  return windows_;
}

std::optional<ChatWindowSavedPosition> ChatWindowState::GetSavedPosition(int window) const {
  std::lock_guard lock(mutex_);
  if (!ValidIndex(window)) return std::nullopt;
  const auto& saved_position = windows_[static_cast<std::size_t>(window)].saved_position;
  if (!saved_position.is_set) return std::nullopt;
  return saved_position;
}

void ChatWindowState::SetSavedPosition(int window, int point, float x, float y) {
  std::lock_guard lock(mutex_);
  if (!ValidIndex(window)) return;
  auto& info = windows_[static_cast<std::size_t>(window)];
  auto& saved_position = info.saved_position;
  saved_position.point = point;
  saved_position.x = x;
  saved_position.y = y;
  saved_position.is_set = true;
  info.saved_layout_is_set = true;
}

std::optional<ChatWindowSavedDimensions> ChatWindowState::GetSavedDimensions(int window) const {
  std::lock_guard lock(mutex_);
  if (!ValidIndex(window)) return std::nullopt;
  return windows_[static_cast<std::size_t>(window)].saved_dimensions;
}

void ChatWindowState::SetSavedDimensions(int window, float width, float height) {
  std::lock_guard lock(mutex_);
  if (!ValidIndex(window)) return;
  auto& info = windows_[static_cast<std::size_t>(window)];
  auto& saved_dimensions = info.saved_dimensions;
  saved_dimensions.width = width;
  saved_dimensions.height = height;
  info.saved_layout_is_set = true;
}

void ChatWindowState::Reset() {
  std::lock_guard lock(mutex_);
  ApplyResetChatWindowsDefaults(windows_);
}

void ChatWindowState::ClearAllMessageGroups() {
  std::lock_guard lock(mutex_);
  for (auto& win : windows_) {
    win.message_groups.clear();
  }
}

void ChatWindowState::ApplyAddedVersionDefaultMessageGroups(const int added_version) {
  std::lock_guard lock(mutex_);

  for (std::size_t index = 0; index < kChatWindowMessageGroups.size(); ++index) {
    const auto& group = kChatWindowMessageGroups[index];
    if (!group.default_enabled || added_version >= group.introduced_added_version) {
      continue;
    }

    const std::size_t window_index =
        index < kPrimaryChatWindowDefaultGroupCount ? 0u : 1u;
    InsertMessageGroupInCanonicalOrder(windows_[window_index].message_groups, index);
  }
}

}

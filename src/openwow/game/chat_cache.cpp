
#include "openwow/game/chat_cache.h"

#include "openwow/core/storm_string.h"
#include "openwow/data/formats/dbc/dbc_enums.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/chat_channel_location.h"
#include "openwow/game/chat_lua_bridge.h"
#include "openwow/game/chat_system.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/chat_window_state.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/ui_enum_helpers.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::game {

namespace {

constexpr std::uint32_t kChatCacheVersion = 5;
constexpr std::uint32_t kChatCacheAddedVersion = 13;
constexpr std::uint32_t kDefenseChannelFlag = 0x10000u;
constexpr std::uint32_t kAreaScopedDefenseChannelFlag = 0x2u;
constexpr std::uint32_t kCityOnlyChannelFlag = 0x10u;
constexpr std::uint32_t kAutoJoinChannelFlag = 0x1u;
constexpr std::uint32_t kStandardRecruitmentModeFlag = 0x20000u;
constexpr std::uint32_t kMaxChannelMaskIndex = 32;
constexpr std::string_view kGuildRecruitmentChannelCVar = "guildRecruitmentChannel";
constexpr std::string_view kGuildRecruitmentChannelConfigToken =
    "OPTION_GUILD_RECRUITMENT_CHANNEL";
constexpr std::uint8_t kJoinChannelFlag = 1u;

struct ChatCacheAddedZoneChannelDefault {
  std::uint32_t channel_id;
  std::uint32_t introduced_in_added_version;
};

constexpr std::array<ChatCacheAddedZoneChannelDefault, 1>
    kChatCacheAddedZoneChannelDefaults{{
        {26u, 4u},
    }};

struct PendingPersistentChannel {
  std::string name;
  bool requested_voice = false;
  std::uint32_t preferred_slot = 0;
};

struct ChatCacheRuntimeState {
  bool loaded = false;
  bool joined_loaded_channels = false;
  bool use_default_zone_channels = false;
  bool suppress_recruitment_leave_on_disable = false;
  std::uint32_t global_zone_mask = 0;
  std::array<std::uint32_t, ui::game::kMaxChatWindows> window_zone_masks{};
  std::vector<PendingPersistentChannel> pending_channels;
};

ChatCacheRuntimeState& GetChatCacheRuntimeState() {
  static ChatCacheRuntimeState state;
  return state;
}

std::uint32_t ChannelMaskBitFromId(const std::uint32_t channel_id) {
  if (channel_id == 0 || channel_id > kMaxChannelMaskIndex) {
    return 0;
  }

  return 1u << (channel_id - 1u);
}

std::uint8_t QuantizeColorByte(const float value) {
  return static_cast<std::uint8_t>(static_cast<int>(value * 255.0f));
}

std::string FormatFloat(const float value) {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << std::fixed << std::setprecision(6) << value;
  return out.str();
}

std::uint32_t NormalizeDefenseAreaId(const data::dbc::DbcLoader* dbc,
                                     std::uint32_t area_id) {
  if (dbc == nullptr) {
    return area_id;
  }

  const auto& area_table = dbc->area_table();
  std::uint32_t current = area_id;
  for (std::size_t depth = 0; depth < 32; ++depth) {
    const auto* area = area_table.LookupEntry(current);
    if (area == nullptr || area->parent_area == 0) {
      return current;
    }
    current = area->parent_area;
  }

  return current;
}

const data::dbc::ChatChannelsEntry* LookupChannelDefinition(
    const data::dbc::DbcLoader* dbc, const std::string& channel_name) {
  if (dbc == nullptr || channel_name.empty()) {
    return nullptr;
  }

  if (const auto* exact = dbc->chat_channels().LookupByNameCaseInsensitive(channel_name);
      exact != nullptr) {
    return exact;
  }

  for (const auto& entry : dbc->chat_channels().entries()) {
    if (entry.pattern.empty()) {
      continue;
    }

    const std::string pattern(entry.pattern);
    const auto placeholder = pattern.find("%s");
    if (placeholder == std::string::npos) {
      continue;
    }

    const std::string prefix = pattern.substr(0, placeholder);
    const std::string suffix = pattern.substr(placeholder + 2);
    if (channel_name.size() < prefix.size() + suffix.size()) {
      continue;
    }

    if (core::SStrCmpUTF8NoCase(channel_name.c_str(), prefix.c_str(),
                                static_cast<std::uint32_t>(prefix.size())) != 0) {
      continue;
    }

    if (!suffix.empty()) {
      const char* channel_suffix =
          channel_name.c_str() + (channel_name.size() - suffix.size());
      if (core::SStrCmpUTF8NoCase(channel_suffix, suffix.c_str(),
                                  static_cast<std::uint32_t>(suffix.size())) != 0) {
        continue;
      }
    }

    return &entry;
  }

  return nullptr;
}

std::uint32_t ResolveZoneChannelMaskBit(const data::dbc::DbcLoader* dbc,
                                        const std::uint32_t current_zone_id,
                                        const ChatChannel& channel) {
  const auto* definition = LookupChannelDefinition(dbc, channel.name);
  if (definition == nullptr || (definition->flags & kDefenseChannelFlag) == 0) {
    return 0;
  }

  if ((definition->flags & kAreaScopedDefenseChannelFlag) == 0) {
    return ChannelMaskBitFromId(definition->id);
  }

  const auto normalized_zone = NormalizeDefenseAreaId(dbc, current_zone_id);
  if (definition->id == normalized_zone) {
    return ChannelMaskBitFromId(definition->id);
  }

  return NormalizeDefenseAreaId(dbc, definition->id) == normalized_zone
             ? ChannelMaskBitFromId(definition->id)
             : 0;
}

std::string TrimTrailingCarriageReturn(std::string line) {
  if (!line.empty() && line.back() == '\r') {
    line.pop_back();
  }
  return line;
}

std::optional<int> ParseIntToken(const std::string& token) {
  if (token.empty()) {
    return std::nullopt;
  }

  char* end = nullptr;
  const long value = std::strtol(token.c_str(), &end, 10);
  if (end == nullptr || *end != '\0') {
    return std::nullopt;
  }

  return static_cast<int>(value);
}

std::optional<float> ParseFloatToken(const std::string& token) {
  if (token.empty()) {
    return std::nullopt;
  }

  char* end = nullptr;
  const float value = std::strtof(token.c_str(), &end);
  if (end == nullptr || *end != '\0') {
    return std::nullopt;
  }

  return value;
}

std::uint32_t ParseGuildRecruitmentChannelMode(std::string_view value) {
  if (value.empty()) {
    return 0;
  }

  std::size_t index = 0;
  const bool negative = value[index] == '-';
  if (negative) {
    ++index;
  }

  if (index >= value.size() || value[index] < '0' || value[index] > '9') {
    return 0;
  }

  std::uint32_t parsed = static_cast<std::uint32_t>(value[index] - '0');
  ++index;
  while (index < value.size()) {
    const char ch = value[index];
    if (ch < '0' || ch > '9') {
      break;
    }

    parsed = parsed * 10u + static_cast<std::uint32_t>(ch - '0');
    ++index;
  }

  if (!negative) {
    return parsed;
  }

  return 0u - parsed;
}

bool IsGuildRecruitmentAutoJoinEnabled() {
  const std::uint32_t mode = ParseGuildRecruitmentChannelMode(
      ui::game::CVarSystem::Instance().GetCVar(std::string(kGuildRecruitmentChannelCVar)));
  return mode == 1u;
}

bool IsCurrentAreaCityRestricted(const data::dbc::DbcLoader* dbc,
                                 const std::uint32_t current_area_id) {
  if (dbc == nullptr || current_area_id == 0) {
    return false;
  }

  const auto* area = dbc->area_table().LookupEntry(current_area_id);
  return area != nullptr &&
         (area->flags & openwow::data::dbc::kAreaFlagSlaveCapital) != 0;
}

bool IsAvailableZoneChannelDefinition(const data::dbc::DbcLoader* dbc,
                                      const std::uint32_t current_area_id,
                                      const data::dbc::ChatChannelsEntry& definition) {
  if (definition.id == 0 || definition.id > kMaxChannelMaskIndex ||
      definition.name.empty() || definition.pattern.empty()) {
    return false;
  }

  if ((definition.flags & kDefenseChannelFlag) != 0) {
    if ((definition.flags & kAreaScopedDefenseChannelFlag) == 0) {
      return true;
    }

    const auto normalized_zone = NormalizeDefenseAreaId(dbc, current_area_id);
    return normalized_zone != 0 &&
           (definition.id == normalized_zone ||
            NormalizeDefenseAreaId(dbc, definition.id) == normalized_zone);
  }

  if ((definition.flags & kCityOnlyChannelFlag) != 0) {
    return IsCurrentAreaCityRestricted(dbc, current_area_id);
  }

  return true;
}

void ApplyAddedVersionZoneChannelDefaults(
    const data::dbc::DbcLoader* dbc,
    const std::uint32_t added_version,
    ChatCacheRuntimeState& runtime_state) {
  if (dbc == nullptr) {
    return;
  }

  for (const auto& default_entry : kChatCacheAddedZoneChannelDefaults) {
    if (default_entry.introduced_in_added_version <= added_version) {
      continue;
    }

    const auto* channel_definition =
        dbc->chat_channels().LookupEntry(default_entry.channel_id);
    if (channel_definition == nullptr ||
        (channel_definition->flags & kAutoJoinChannelFlag) == 0) {
      continue;
    }

    const auto bit = ChannelMaskBitFromId(default_entry.channel_id);
    if (bit == 0 || (runtime_state.global_zone_mask & bit) != 0) {
      continue;
    }

    runtime_state.global_zone_mask |= bit;
    runtime_state.window_zone_masks[0] |= bit;
  }
}

bool MatchesWindowChannelAssignment(const ChatChannel& channel,
                                    const ui::game::ChatWindowChannel& assignment) {
  if (assignment.number != 0) {
    if (channel.lookup_id == assignment.number || channel.id == assignment.number) {
      return true;
    }
  }

  if (core::SStrCmpUTF8NoCase(channel.name.c_str(), assignment.name.c_str(),
                              0x7FFFFFFFu) == 0) {
    return true;
  }

  return !channel.display_name.empty() &&
         core::SStrCmpUTF8NoCase(channel.display_name.c_str(), assignment.name.c_str(),
                                 0x7FFFFFFFu) == 0;
}

const data::dbc::ChatChannelsEntry* FindGuildRecruitmentChannelDefinition(
    const data::dbc::DbcLoader* dbc) {
  if (dbc == nullptr) {
    return nullptr;
  }

  for (const auto& entry : dbc->chat_channels().entries()) {
    if ((entry.flags & kStandardRecruitmentModeFlag) == 0) {
      continue;
    }
    return &entry;
  }

  return nullptr;
}

bool QueueAndSendBuiltinChannelJoin(WorldSession& session,
                                    const data::dbc::ChatChannelsEntry& definition) {
  const auto channel_name = ResolveBuiltinChatChannelName(session, definition);
  if (!channel_name.has_value()) {
    return false;
  }

  auto& chat_system = ChatSystem::Get();
  if (chat_system.GetChannelByLookupId(definition.id) != nullptr) {
    return false;
  }

  if (chat_system.GetChannelByName(*channel_name) == nullptr) {
    const auto queued = chat_system.QueuePendingNumberedChannel(
        *channel_name, definition.id, definition.name, false, true);
    if (!queued.has_value()) {
      return false;
    }
  }

  session.interaction().SendJoinChannel(definition.id, *channel_name, {}, false,
                                        kJoinChannelFlag);
  return true;
}

bool QueueAndSendPersistentChannelJoin(WorldSession& session,
                                       const PendingPersistentChannel& pending_channel) {
  auto& chat_system = ChatSystem::Get();
  if (chat_system.GetChannelByName(pending_channel.name) != nullptr) {
    return false;
  }

  const auto queued = chat_system.QueuePendingNumberedChannel(
      pending_channel.name, 0, {}, pending_channel.requested_voice, true,
      pending_channel.preferred_slot);
  if (!queued.has_value()) {
    return false;
  }

  session.interaction().SendJoinChannel(0, pending_channel.name, {},
                                        pending_channel.requested_voice, kJoinChannelFlag);
  return true;
}

bool LeaveJoinedChannel(WorldSession& session, const ChatChannel& channel) {
  session.interaction().SendLeaveChannel(channel.id, channel.name);
  ChatSystem::Get().LeaveChannel(channel.name);
  return true;
}

void ApplyWindowZoneChannels(const data::dbc::DbcLoader* dbc,
                             const std::uint32_t current_area_id,
                             const std::uint32_t global_zone_mask,
                             const std::array<std::uint32_t, ui::game::kMaxChatWindows>&
                                 window_zone_masks) {
  if (dbc == nullptr) {
    return;
  }

  auto& windows = ui::game::ChatWindowState::Get();
  for (std::size_t window_index = 0; window_index < window_zone_masks.size(); ++window_index) {
    const auto effective_mask = window_zone_masks[window_index] & global_zone_mask;
    if (effective_mask == 0) {
      continue;
    }

    for (const auto& entry : dbc->chat_channels().entries()) {
      const auto bit = ChannelMaskBitFromId(entry.id);
      if (bit == 0 || (effective_mask & bit) == 0) {
        continue;
      }
      if (!IsAvailableZoneChannelDefinition(dbc, current_area_id, entry)) {
        continue;
      }

      windows.AddChannel(static_cast<int>(window_index), std::string(entry.name), entry.id);
    }
  }
}

void PopulateDefaultZoneChannels(const data::dbc::DbcLoader* dbc,
                                 const std::uint32_t current_area_id,
                                 std::uint32_t* const out_global_zone_mask) {
  if (dbc == nullptr || out_global_zone_mask == nullptr) {
    return;
  }

  *out_global_zone_mask = 0;
  auto& windows = ui::game::ChatWindowState::Get();
  for (const auto& entry : dbc->chat_channels().entries()) {
    const auto bit = ChannelMaskBitFromId(entry.id);
    if (bit == 0 || (entry.flags & kAutoJoinChannelFlag) == 0) {
      continue;
    }
    if (!IsAvailableZoneChannelDefinition(dbc, current_area_id, entry)) {
      continue;
    }

    *out_global_zone_mask |= bit;
    windows.AddChannel(0, std::string(entry.name), entry.id);
  }
}

void SyncGuildRecruitmentChannel(WorldSession& session) {
  auto& state = GetChatCacheRuntimeState();
  if (!state.loaded || !state.joined_loaded_channels) {
    return;
  }

  const auto* definition = FindGuildRecruitmentChannelDefinition(session.GetDbcLoader());
  if (definition == nullptr) {
    return;
  }

  auto& chat_system = ChatSystem::Get();
  const auto* existing = chat_system.GetChannelByLookupId(definition->id);
  const bool auto_join = IsGuildRecruitmentAutoJoinEnabled();
  const auto* local_player = session.objects().GetLocalPlayerTyped();
  if (local_player == nullptr) {
    return;
  }

  const bool in_guild = local_player->GetGuildID() != 0;

  if (!auto_join) {
    if (existing != nullptr && !state.suppress_recruitment_leave_on_disable) {
      LeaveJoinedChannel(session, *existing);
      ui::game::ScriptEventDispatch::Get().FireChannelUiUpdate();
    }
    state.suppress_recruitment_leave_on_disable = false;
    return;
  }

  state.suppress_recruitment_leave_on_disable = false;
  if (in_guild) {
    if (existing != nullptr) {
      LeaveJoinedChannel(session, *existing);
      ui::game::ScriptEventDispatch::Get().FireChannelUiUpdate();
    }
    return;
  }

  if (existing == nullptr && QueueAndSendBuiltinChannelJoin(session, *definition)) {
    ui::game::ScriptEventDispatch::Get().FireChannelUiUpdate();
  }
}

}

void ResetChatCacheRuntimeState() {
  auto& state = GetChatCacheRuntimeState();
  state = {};
}

void SetGuildRecruitmentChannelAutoJoin(WorldSession& session,
                                        const bool enabled,
                                        const bool suppress_leave_on_disable) {
  auto& state = GetChatCacheRuntimeState();
  state.suppress_recruitment_leave_on_disable =
      suppress_leave_on_disable && !enabled;
  auto& cvars = ui::game::CVarSystem::Instance();
  const std::string desired_value = enabled ? "1" : "0";
  const bool value_changed =
      cvars.GetCVar(std::string(kGuildRecruitmentChannelCVar)) != desired_value;
  cvars.SetCVar(std::string(kGuildRecruitmentChannelCVar), desired_value, true);

  if (!value_changed) {
    SyncGuildRecruitmentChannel(session);
  }
}

void ApplyChatCachePayload(WorldSession& session,
                           const std::string_view data) {
  auto& runtime_state = GetChatCacheRuntimeState();
  runtime_state = {};

  auto& chat_system = ChatSystem::Get();
  auto& chat_windows = ui::game::ChatWindowState::Get();
  auto& cvars = ui::game::CVarSystem::Instance();
  auto& bridge = ChatLuaBridge::Get();

  chat_system.ResetChannelConfiguration();
  chat_windows.Reset();
  bridge.ResetChatTypeVisuals();

  static std::uint32_t guild_recruitment_callback_handle = 0;
  if (guild_recruitment_callback_handle != 0) {
    cvars.RemoveCallback(std::string(kGuildRecruitmentChannelCVar),
                         guild_recruitment_callback_handle);
  }
  guild_recruitment_callback_handle = cvars.AddCallback(
      std::string(kGuildRecruitmentChannelCVar),
      [&session](const std::string&, const std::string&) {
        SyncGuildRecruitmentChannel(session);
      });

  cvars.SetCVar(std::string(kGuildRecruitmentChannelCVar), "1", true);

  std::uint32_t cache_version = 0;
  std::uint32_t added_version = 0;
  bool parsed_header = false;
  bool saw_version_header = false;
  bool expecting_added_version = false;

  if (!data.empty()) {
    chat_windows.ClearAllMessageGroups();
  }

  enum class ParseSection {
    kNone,
    kTopLevelChannels,
    kColors,
    kWindowMessages,
    kWindowChannels,
  };

  ParseSection section = ParseSection::kNone;
  int current_window = -1;
  std::istringstream lines{std::string(data)};
  std::string line;
  while (std::getline(lines, line)) {
    line = TrimTrailingCarriageReturn(std::move(line));
    if (line.empty()) {
      continue;
    }

    if (!parsed_header) {
      if (line.rfind("VERSION", 0) == 0) {
        if (const auto parsed = ParseIntToken(line.substr(7)); parsed.has_value()) {
          cache_version = static_cast<std::uint32_t>(std::max(*parsed, 0));
        }
        saw_version_header = true;
        parsed_header = cache_version < 2;
        expecting_added_version = !parsed_header;
        continue;
      }

      parsed_header = true;
    } else if (expecting_added_version) {
      expecting_added_version = false;
      parsed_header = true;
      if (line.rfind("ADDEDVERSION", 0) == 0) {
        if (const auto parsed = ParseIntToken(line.substr(12)); parsed.has_value()) {
          added_version = static_cast<std::uint32_t>(std::max(*parsed, 0));
        }
        continue;
      }
    }

    if (line == "END") {
      if (section == ParseSection::kWindowMessages ||
          section == ParseSection::kWindowChannels) {
        section = ParseSection::kNone;
      } else if (current_window != -1) {
        current_window = -1;
        section = ParseSection::kNone;
      } else {
        section = ParseSection::kNone;
      }
      continue;
    }

    if (section == ParseSection::kTopLevelChannels) {
      if (saw_version_header && cache_version < 3) {
        continue;
      }

      std::istringstream entry(line);
      std::string channel_name;
      int requested_voice = 0;
      int preferred_slot = 0;
      if (entry >> channel_name >> requested_voice >> preferred_slot) {
        runtime_state.pending_channels.push_back(
            {channel_name, requested_voice != 0,
             static_cast<std::uint32_t>(std::max(preferred_slot, 0))});
      }
      continue;
    }

    if (section == ParseSection::kColors) {
      std::istringstream entry(line);
      std::string token;
      int r = 0;
      int g = 0;
      int b = 0;
      char name_by_class = 'N';
      if (entry >> token >> r >> g >> b >> name_by_class) {
        (void)bridge.SetChatTypeColor(token, static_cast<float>(r) / 255.0f,
                                      static_cast<float>(g) / 255.0f,
                                      static_cast<float>(b) / 255.0f);
        (void)bridge.SetChatColorNameByClass(token, name_by_class == 'Y');
      }
      continue;
    }

    if (current_window != -1 && section == ParseSection::kWindowMessages) {
      chat_windows.AddMessageGroup(current_window, line);
      continue;
    }

    if (current_window != -1 && section == ParseSection::kWindowChannels) {
      chat_windows.AddChannel(current_window, line);
      continue;
    }

    if (line == "CHANNELS") {
      section = current_window == -1 ? ParseSection::kTopLevelChannels
                                     : ParseSection::kWindowChannels;
      continue;
    }

    if (line == "COLORS") {
      section = ParseSection::kColors;
      continue;
    }

    if (line == "MESSAGES") {
      if (current_window != -1) {
        section = ParseSection::kWindowMessages;
      }
      continue;
    }

    if (line.rfind("WINDOW ", 0) == 0) {
      const auto window_index = ParseIntToken(line.substr(7));
      if (window_index.has_value() &&
          *window_index >= 1 && *window_index <= ui::game::kMaxChatWindows) {
        current_window = *window_index - 1;
      } else {
        current_window = -1;
      }
      section = ParseSection::kNone;
      continue;
    }

    if (line.rfind(std::string(kGuildRecruitmentChannelConfigToken) + ' ', 0) == 0) {
      const std::string mode =
          line.substr(std::string(kGuildRecruitmentChannelConfigToken).size() + 1);
      SetGuildRecruitmentChannelAutoJoin(
          session,
          openwow::core::SStrCmpNoCase(mode.c_str(), "STANDARD", 0x7FFFFFFF) != 0);
      continue;
    }

    if (line.rfind("ZONECHANNELS ", 0) == 0) {
      const auto mask = ParseIntToken(line.substr(13));
      if (!mask.has_value()) {
        continue;
      }

      if (current_window == -1) {
        runtime_state.global_zone_mask = static_cast<std::uint32_t>(*mask);
      } else if (current_window >= 0 &&
                 current_window < static_cast<int>(runtime_state.window_zone_masks.size())) {
        runtime_state.window_zone_masks[static_cast<std::size_t>(current_window)] =
            static_cast<std::uint32_t>(*mask);
      }
      continue;
    }

    if (current_window == -1) {
      continue;
    }

    if (line.rfind("NAME ", 0) == 0) {
      chat_windows.SetWindowName(current_window, line.substr(5));
      continue;
    }
    if (line.rfind("SIZE ", 0) == 0) {
      if (const auto size = ParseIntToken(line.substr(5)); size.has_value()) {
        chat_windows.SetWindowFontSize(current_window, *size);
      }
      continue;
    }
    if (line.rfind("COLOR ", 0) == 0) {
      std::istringstream entry(line.substr(6));
      int r = 0;
      int g = 0;
      int b = 0;
      int a = 0;
      if (entry >> r >> g >> b >> a) {
        chat_windows.SetWindowColor(current_window,
                                    static_cast<float>(r) / 255.0f,
                                    static_cast<float>(g) / 255.0f,
                                    static_cast<float>(b) / 255.0f);
        chat_windows.SetWindowAlpha(current_window,
                                    static_cast<float>(a) / 255.0f);
      }
      continue;
    }
    if (line.rfind("LOCKED ", 0) == 0) {
      if (const auto locked = ParseIntToken(line.substr(7)); locked.has_value()) {
        chat_windows.SetWindowLocked(current_window, *locked != 0);
      }
      continue;
    }
    if (line.rfind("UNINTERACTABLE ", 0) == 0) {
      if (const auto uninteractable = ParseIntToken(line.substr(15));
          uninteractable.has_value()) {
        chat_windows.SetWindowUninteractable(current_window,
                                             *uninteractable != 0);
      }
      continue;
    }
    if (line.rfind("DOCKED ", 0) == 0) {
      if (const auto docked = ParseIntToken(line.substr(7)); docked.has_value()) {
        chat_windows.SetWindowDockTarget(current_window, *docked);
      }
      continue;
    }
    if (line.rfind("SHOWN ", 0) == 0) {
      if (const auto shown = ParseIntToken(line.substr(6)); shown.has_value()) {
        chat_windows.SetWindowShown(current_window, *shown != 0);
      }
      continue;
    }
    if (line.rfind("POSITION ", 0) == 0) {
      std::istringstream entry(line.substr(9));
      std::string point_name;
      std::string x_token;
      std::string y_token;
      if (!(entry >> point_name >> x_token >> y_token)) {
        continue;
      }

      int frame_point = 0;
      const auto x = ParseFloatToken(x_token);
      const auto y = ParseFloatToken(y_token);
      if (openwow::ui::StringToFramePoint(point_name.c_str(), &frame_point) != 0 &&
          x.has_value() && y.has_value()) {
        chat_windows.SetSavedPosition(current_window, frame_point, *x, *y);
      }
      continue;
    }
    if (line.rfind("DIMENSIONS ", 0) == 0) {
      std::istringstream entry(line.substr(11));
      std::string width_token;
      std::string height_token;
      if (!(entry >> width_token >> height_token)) {
        continue;
      }

      const auto width = ParseFloatToken(width_token);
      const auto height = ParseFloatToken(height_token);
      if (width.has_value() && height.has_value()) {
        chat_windows.SetSavedDimensions(current_window, *width, *height);
      }
      continue;
    }
  }

  if (added_version < kChatCacheAddedVersion) {
    ApplyAddedVersionZoneChannelDefaults(session.GetDbcLoader(), added_version,
                                         runtime_state);
    chat_windows.ApplyAddedVersionDefaultMessageGroups(added_version);
  }

  runtime_state.loaded = true;
  runtime_state.use_default_zone_channels = data.empty();

  auto& dispatch = ui::game::ScriptEventDispatch::Get();

  dispatch.FireEvent(ui::game::events::UPDATE_CHAT_WINDOWS);
  for (const auto& state : bridge.GetChatTypeVisualStates()) {
    dispatch.FireEventArgs(
        ui::game::events::UPDATE_CHAT_COLOR,
        {state.token, static_cast<double>(state.r),
         static_cast<double>(state.g), static_cast<double>(state.b)});
    dispatch.FireEventArgs(
        ui::game::events::UPDATE_CHAT_COLOR_NAME_BY_CLASS,
        {state.token, state.colorNameByClass});
  }

  TrySyncLoadedChatChannels(session);
}

void TrySyncLoadedChatChannels(WorldSession& session) {
  auto& runtime_state = GetChatCacheRuntimeState();
  if (!runtime_state.loaded || runtime_state.joined_loaded_channels) {
    SyncGuildRecruitmentChannel(session);
    return;
  }

  const auto* dbc = session.GetDbcLoader();
  const auto current_area_id = session.objects().GetAreaId();
  if (dbc == nullptr || current_area_id == 0 ||
      session.scene_state().GetRealZoneText().empty()) {
    return;
  }

  if (runtime_state.use_default_zone_channels) {
    PopulateDefaultZoneChannels(dbc, current_area_id, &runtime_state.global_zone_mask);
    runtime_state.use_default_zone_channels = false;
  }

  ApplyWindowZoneChannels(dbc, current_area_id, runtime_state.global_zone_mask,
                          runtime_state.window_zone_masks);

  bool changed = false;
  for (const auto& entry : dbc->chat_channels().entries()) {
    const auto bit = ChannelMaskBitFromId(entry.id);
    if (bit == 0 || (runtime_state.global_zone_mask & bit) == 0) {
      continue;
    }
    if (!IsAvailableZoneChannelDefinition(dbc, current_area_id, entry)) {
      continue;
    }
    changed = QueueAndSendBuiltinChannelJoin(session, entry) || changed;
  }

  for (const auto& pending_channel : runtime_state.pending_channels) {
    changed = QueueAndSendPersistentChannelJoin(session, pending_channel) || changed;
  }

  runtime_state.joined_loaded_channels = true;
  if (changed) {
    ui::game::ScriptEventDispatch::Get().FireChannelUiUpdate();
  }

  SyncGuildRecruitmentChannel(session);
}

std::string SerializeChatCache(const data::dbc::DbcLoader* dbc,
                               const std::uint32_t current_zone_id) {
  const auto channels = ChatSystem::Get().GetChannelsSnapshot();
  const auto windows = ui::game::ChatWindowState::Get().GetWindowsSnapshot();
  const auto color_states = ChatLuaBridge::Get().GetChatTypeCacheStates();
  const bool auto_guild_recruitment = IsGuildRecruitmentAutoJoinEnabled();

  std::vector<ChatChannel> persistent_channels;
  persistent_channels.reserve(channels.size());

  std::uint32_t global_zone_mask = 0;
  for (const auto& channel : channels) {
    if (channel.is_joined && !channel.lua_hidden) {
      global_zone_mask |= ResolveZoneChannelMaskBit(dbc, current_zone_id, channel);
    }

    if (!channel.is_joined || channel.lua_hidden || channel.id == 0 || channel.lookup_id != 0 ||
        !channel.permanent) {
      continue;
    }

    persistent_channels.push_back(channel);
  }

  std::sort(persistent_channels.begin(), persistent_channels.end(),
            [](const ChatChannel& lhs, const ChatChannel& rhs) {
              if (lhs.id != rhs.id) {
                return lhs.id < rhs.id;
              }
              return lhs.name < rhs.name;
            });

  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << "VERSION " << kChatCacheVersion << "\n\n";
  out << "ADDEDVERSION " << kChatCacheAddedVersion << "\n\n";
  out << kGuildRecruitmentChannelConfigToken << ' '
      << (auto_guild_recruitment ? "AUTO" : "STANDARD") << "\n\n";

  out << "CHANNELS\n";
  for (const auto& channel : persistent_channels) {
    out << channel.name << ' ' << (channel.requested_voice ? 1 : 0) << ' '
        << channel.id << "\n";
  }
  out << "END\n\n";

  out << "ZONECHANNELS " << global_zone_mask << "\n\n";

  out << "COLORS\n";
  for (const auto& state : color_states) {
    if (state.token.empty()) {
      continue;
    }

    out << state.token << ' ' << static_cast<unsigned int>(state.r)
        << ' ' << static_cast<unsigned int>(state.g) << ' '
        << static_cast<unsigned int>(state.b) << ' '
        << (state.colorNameByClass ? 'Y' : 'N') << "\n";
  }
  out << "END\n\n";

  for (std::size_t window_index = 0; window_index < windows.size(); ++window_index) {
    const auto& window = windows[window_index];
    out << "WINDOW " << (window_index + 1) << "\n";
    if (!window.name.empty()) {
      out << "NAME " << window.name << "\n";
    }
    out << "SIZE " << static_cast<int>(window.font_size) << "\n";
    out << "COLOR " << static_cast<unsigned int>(QuantizeColorByte(window.r)) << ' '
        << static_cast<unsigned int>(QuantizeColorByte(window.g)) << ' '
        << static_cast<unsigned int>(QuantizeColorByte(window.b)) << ' '
        << static_cast<unsigned int>(QuantizeColorByte(window.alpha)) << "\n";
    out << "LOCKED " << (window.locked ? 1 : 0) << "\n";
    out << "UNINTERACTABLE " << (window.uninteractable ? 1 : 0) << "\n";
    out << "DOCKED " << window.dock_target << "\n";
    out << "SHOWN " << (window.shown ? 1 : 0) << "\n";
    if (window.saved_layout_is_set) {
      out << "POSITION " << ui::FramePointToString(window.saved_position.point) << ' '
          << FormatFloat(window.saved_position.x) << ' '
          << FormatFloat(window.saved_position.y) << "\n";
      out << "DIMENSIONS " << FormatFloat(window.saved_dimensions.width) << ' '
          << FormatFloat(window.saved_dimensions.height) << "\n\n";
    }

    out << "MESSAGES\n";
    for (const auto& group : window.message_groups) {
      out << group << "\n";
    }
    out << "END\n\n";

    out << "CHANNELS\n";
    std::uint32_t window_zone_mask = 0;
    for (const auto& slot : window.channels) {
      if (!slot.has_value()) {
        continue;
      }

      const auto& assignment = *slot;
      const auto it = std::find_if(
          channels.begin(), channels.end(), [&](const ChatChannel& channel) {
            return channel.is_joined && !channel.lua_hidden &&
                   MatchesWindowChannelAssignment(channel, assignment);
          });
      if (it == channels.end()) {
        out << assignment.name << "\n";
        continue;
      }

      const auto zone_bit = ResolveZoneChannelMaskBit(dbc, current_zone_id, *it);
      if ((global_zone_mask & zone_bit) != 0) {
        window_zone_mask |= zone_bit;
        continue;
      }

      out << assignment.name << "\n";
    }
    out << "END\n\n";

    out << "ZONECHANNELS " << (window_zone_mask & global_zone_mask) << "\n\n";
    out << "END\n\n";
  }

  return out.str();
}

}

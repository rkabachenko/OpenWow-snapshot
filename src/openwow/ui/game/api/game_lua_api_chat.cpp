
#include "openwow/ui/game/api/game_lua_api_chat.h"
#include "openwow/core/console.h"
#include "openwow/core/storm_string.h"
#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/game/chat_display.h"
#include "openwow/game/chat_lua_bridge.h"
#include "openwow/game/chat_manager.h"
#include "openwow/game/chat_system.h"
#include "openwow/game/client_text_log_files.h"
#include "openwow/game/activities/dance/application/dance_studio.h"
#include "openwow/game/gm_ticket_chat_log.h"
#include "openwow/game/group_system.h"
#include "openwow/game/localization.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/voice_chat.h"
#include "openwow/network/protocol/wotlk/opcodes.h"
#include "openwow/ui/game/chat_window_state.h"
#include "openwow/ui/game/api/game_lua_api_chatmsg.h"
#include "openwow/ui/game/event_dispatcher.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/runtime/world_ui_runtime_context.h"
#include "openwow/ui/lua_numeric.h"
#include "openwow/ui/lua_result_capacity.h"
#include "openwow/ui/ui_enum_helpers.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>

namespace openwow::ui::game::detail {

namespace {
openwow::audio::SoundRuntime& SoundRuntimeForLua(lua_State* state) {
  auto* context = openwow::ui::game::runtime::WorldUiRuntimeContext::FromLua(state);
  if (context == nullptr) {
    luaL_error(state, "world sound runtime is unavailable");
    std::abort();
  }
  return context->sound_runtime();
}
}

namespace {

constexpr std::size_t kChannelCommandTargetNameMaxLength = 0x30;
constexpr const char *kGameUiMgrKey = "openwow.world_ui_runtime_context";
constexpr std::uint32_t kEnumerateServerChannelRestrictedAreaMask = 0x10u;
constexpr const char *kPartyChannelName = "Party";
constexpr const char *kRaidChannelName = "Raid";
constexpr const char *kBattlegroundChannelName = "Battleground";

bool MatchesRestrictedVoiceChannelName(std::string_view channel_name, const bool include_localized) {
  if (GameUiLookupMatches(channel_name, kPartyChannelName, true) ||
      GameUiLookupMatches(channel_name, kRaidChannelName, true) ||
      GameUiLookupMatches(channel_name, kBattlegroundChannelName, true)) {
    return true;
  }

  if (!include_localized) {
    return false;
  }

  auto &localization = openwow::game::Localization::Get();
  return GameUiLookupMatches(channel_name, localization.GetString("PARTY", kPartyChannelName), true) ||
         GameUiLookupMatches(channel_name, localization.GetString("RAID", kRaidChannelName), true) ||
         GameUiLookupMatches(channel_name,
                             localization.GetString("BATTLEFIELDS", kBattlegroundChannelName),
                             true);
}

bool CanHandleRestrictedVoiceSilenceTarget(const openwow::game::WorldSession &session) {
  const auto *active_player = session.objects().GetActivePlayer();
  if (active_player == nullptr) {
    return false;
  }

  const auto active_player_guid = active_player->GetGuid();
  if (session.group().leader_guid() == active_player_guid) {
    return true;
  }

  return session.group().IsRaid() &&
         (session.group().my_flags() &
          static_cast<std::uint8_t>(openwow::game::GroupMemberFlag::kAssistant)) != 0;
}

void HandleRestrictedVoiceSilenceTarget(lua_State *L, const int target_arg_index,
                                        const bool silence) {
  auto *session = GetWorldSession(L);
  if (!session || !session->group().IsInGroup() || lua_isstring(L, target_arg_index) == 0) {
    return;
  }

  const char *target_token_or_name = lua_tostring(L, target_arg_index);
  const auto target_guid =
      ResolveGameUiLookup(session, target_token_or_name ? target_token_or_name : "",
                          openwow::game::kTypeMaskPlayer, session->group().IsRaid() ? 6 : 5,
                          false, false);
  if (target_guid.IsEmpty()) {
    DisplaySystemMessage(81, target_token_or_name ? target_token_or_name : "");
    return;
  }

  if (!CanHandleRestrictedVoiceSilenceTarget(*session)) {
    return;
  }

  session->interaction().SendGroupVoiceSilence(target_guid.GetRawValue(), silence,
                                               session->group().IsBattlegroundGroup());
}

std::uint8_t EncodeChatColorComponent(lua_Number value) {
  return static_cast<std::uint8_t>(static_cast<int>(value * 255.0));
}

double DecodeChatColorComponent(std::uint8_t value) {
  return static_cast<double>(value) / 255.0;
}

runtime::WorldUiRuntimeContext *GetRegisteredGameUiManager(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, kGameUiMgrKey);
  auto *manager = static_cast<runtime::WorldUiRuntimeContext *>(lua_touserdata(L, -1));
  lua_pop(L, 1);
  return manager;
}

std::optional<openwow::game::ChatChannel> FindJoinedChannelByName(std::string_view name) {
  if (name.empty()) {
    return std::nullopt;
  }

  const auto &chat_system = openwow::game::ChatSystem::Get();
  const auto channel_count = chat_system.GetNumChannels();
  for (std::size_t i = 0; i < channel_count; ++i) {
    const auto *channel = chat_system.GetChannel(i);
    if (!channel || !channel->is_joined) {
      continue;
    }
    if (GameUiLookupMatches(channel->name, name, true)) {
      return *channel;
    }
  }

  return std::nullopt;
}

std::string ResolvePartyDisplayRosterMemberName(openwow::game::WorldSession *session,
                                                const std::uint64_t guid,
                                                const std::string_view tracked_name) {
  if (session == nullptr || guid == 0) {
    return tracked_name.empty() ? std::string{} : std::string(tracked_name);
  }

  const openwow::game::ObjectGuid object_guid(guid);
  if (const auto *player = session->objects().GetPlayer(object_guid); player != nullptr) {
    const std::string player_name = player->GetPlayerName();
    if (!player_name.empty()) {
      return player_name;
    }
  }

  const std::string cached_name = session->objects().GetPlayerName(object_guid);
  if (!cached_name.empty()) {
    return cached_name;
  }

  return tracked_name.empty() ? std::string{} : std::string(tracked_name);
}

std::vector<openwow::game::ChannelRosterMember>
BuildPartyDisplayChannelRoster(openwow::game::WorldSession *session) {
  std::vector<openwow::game::ChannelRosterMember> members;
  if (session == nullptr) {
    return members;
  }

  const auto *active_player = session->objects().GetActivePlayer();
  if (active_player == nullptr) {
    return members;
  }

  auto &group_system = openwow::game::GroupSystem::Get();
  if (group_system.GetTrackedPartyMemberCount() == 0) {
    return members;
  }

  const std::uint64_t leader_guid = group_system.GetLeaderGuid();
  const auto append_member = [&](const std::uint64_t guid, const std::string_view tracked_name) {
    if (guid == 0) {
      return;
    }

    openwow::game::ChannelRosterMember member;
    member.guid = guid;
    const std::uint8_t base_flags = guid == leader_guid ? 0x05u : 0x04u;
    member.raw_flags = openwow::game::VoiceChat::Get().DecorateChannelRosterMemberFlags(
        openwow::game::VoiceChatChannelType::kParty, {}, openwow::game::ObjectGuid(guid),
        base_flags, true);
    member.flags = member.raw_flags;
    member.name = ResolvePartyDisplayRosterMemberName(session, guid, tracked_name);
    members.push_back(std::move(member));
  };

  const std::string self_name = active_player->GetPlayerName();
  append_member(active_player->GetGuid().GetRawValue(), self_name);

  for (std::uint32_t slot = 0; slot < 4; ++slot) {
    const std::uint64_t member_guid = group_system.GetTrackedPartyMemberGuid(slot);
    if (member_guid == 0) {
      continue;
    }

    std::string tracked_name;
    if (const auto *tracked_member = group_system.GetMemberByGuid(member_guid);
        tracked_member != nullptr) {
      tracked_name = tracked_member->name;
    }
    append_member(member_guid, tracked_name);
  }

  std::sort(members.begin(), members.end(),
            [](const openwow::game::ChannelRosterMember &left,
               const openwow::game::ChannelRosterMember &right) {
              const bool left_missing_name = left.name.empty();
              const bool right_missing_name = right.name.empty();
              if (left_missing_name || right_missing_name) {
                if (left_missing_name == right_missing_name) {
                  return false;
                }
                return left_missing_name;
              }

              return openwow::core::SStrCmpNoCaseCollate(
                         left.name.c_str(), right.name.c_str(), 0x7FFFFFFF) < 0;
            });
  return members;
}

openwow::game::ObjectGuid ResolveWatchedChannelRosterLookupGuid(
    openwow::game::WorldSession &session, const std::string_view token_or_name) {
  if (token_or_name.empty()) {
    return {};
  }

  if (const auto guid = session.objects().FindPlayerGuidByName(token_or_name); guid.has_value()) {
    return *guid;
  }

  return ResolveUnitId(&session, std::string(token_or_name));
}

std::optional<openwow::game::ChannelRosterMember> FindWatchedChannelRosterMemberByGuid(
    const std::uint64_t guid) {
  if (guid == 0) {
    return std::nullopt;
  }

  auto &chat_system = openwow::game::ChatSystem::Get();
  const auto roster_size = chat_system.GetWatchedChannelRosterSize();
  for (std::size_t index = 0; index < roster_size; ++index) {
    const auto member = chat_system.GetWatchedChannelRosterMember(index);
    if (!member.has_value() || member->guid != guid) {
      continue;
    }

    return member;
  }

  return std::nullopt;
}

void PushChannelRosterInfo(lua_State *L, const openwow::game::ChannelRosterMember &member) {
  lua_pushstring(L, member.name.empty() ? "UNKNOWN" : member.name.c_str());
  lua_pushwowbool(L, (member.flags & 0x01u) != 0);
  lua_pushwowbool(L, (member.flags & 0x02u) != 0);
  lua_pushwowbool(L, (member.flags & 0x20u) != 0);
  lua_pushwowbool(L, (member.flags & 0x40u) != 0);
  lua_pushwowbool(L, (member.flags & 0x80u) != 0);
}

std::optional<std::size_t> GetLuaWrappedZeroBasedIndex(lua_State *L, const int arg_index) {
  if (!lua_isnumber(L, arg_index)) {
    return std::nullopt;
  }

  const auto one_based = openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, arg_index));
  return static_cast<std::size_t>(one_based - 1u);
}

std::uint8_t QuantizeChatWindowColorComponent(lua_Number value) {
  const auto truncated = TruncateLuaNumberToSseI32(value * 255.0);
  return static_cast<std::uint8_t>(truncated);
}

float DecodeChatWindowColorComponent(const std::uint8_t value) {
  return static_cast<float>(value) / 255.0f;
}

std::optional<openwow::game::ComplaintableChatRecord> FindChatComplaintRecord(
    lua_State* L, openwow::game::WorldSession& session, const char* usage) {
  (void)session;
  if (lua_isnumber(L, 1)) {
    const auto line_id = openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1));
    return openwow::game::GMTicketChatLog::Get().FindComplaintRecordByLineId(line_id);
  }

  if (!lua_isstring(L, 1)) {
    luaL_error(L, usage);
    return std::nullopt;
  }

  const char* sender_name = lua_tostring(L, 1);
  const char* message = lua_isstring(L, 2) ? lua_tostring(L, 2) : nullptr;
  if (sender_name == nullptr) {
    return std::nullopt;
  }

  return openwow::game::GMTicketChatLog::Get().FindComplaintRecordBySenderAndText(
      sender_name, message);
}

bool CanComplainAboutChatRecord(const openwow::game::WorldSession& session,
                                const openwow::game::ComplaintableChatRecord& record) {
  const auto* local_player = session.objects().GetLocalPlayer();
  if (local_player == nullptr) {
    return false;
  }

  if (session.feature_status().complaint_status == 0) {
    return false;
  }

  if (record.sender_guid == local_player->GetGuid().GetRawValue()) {
    return false;
  }

  return !session.social().HasContact(openwow::game::ObjectGuid(record.sender_guid));
}

std::optional<int> GetLuaChatWindowLayoutIndex(lua_State *L, const int arg_index) {
  const auto zero_based_index = GetLuaWrappedZeroBasedIndex(L, arg_index);
  if (!zero_based_index ||
      *zero_based_index >= static_cast<std::size_t>(openwow::ui::game::kMaxChatWindows)) {
    return std::nullopt;
  }

  return static_cast<int>(*zero_based_index);
}

std::optional<openwow::game::ResolvedDisplayChannel> ResolveLuaDisplayChannel(lua_State *L,
                                                                              const int arg_index) {
  const auto zero_based_index = GetLuaWrappedZeroBasedIndex(L, arg_index);
  if (!zero_based_index) {
    return std::nullopt;
  }

  return openwow::game::ChatSystem::Get().ResolveDisplayChannel(*zero_based_index);
}

int LuaChannelCommandWithTarget(lua_State *L, const char *function_name,
                                openwow::net::wotlk::Opcode opcode) {
  if (!lua_isstring(L, 1) || !lua_isstring(L, 2)) {
    return luaL_error(L, "Usage: %s(\"channel\", \"name\")", function_name);
  }

  const char *raw_channel = lua_tostring(L, 1);
  const auto channel_name = ResolveChannelNameOrIndex(raw_channel);
  if (!channel_name) {
    return 0;
  }

  const char *target = lua_tostring(L, 2);
  if (target && std::strlen(target) > kChannelCommandTargetNameMaxLength) {
    return luaL_error(L, "Name too long");
  }

  if (auto *session = GetWorldSession(L)) {
    session->interaction().SendChannelTargetCommand(static_cast<std::uint16_t>(opcode),
                                                    *channel_name, target ? target : "");
  }
  return 0;
}

int LuaDisplayChannelVoiceCommand(lua_State *L, const char *function_name,
                                  openwow::net::wotlk::Opcode opcode) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: %s(\"displayIndex\")", function_name);
  }

  const auto resolved = ResolveLuaDisplayChannel(L, 1);
  if (!resolved || resolved->kind != openwow::game::DisplayChannelKind::kJoinedChannel ||
      !resolved->channel) {
    return 0;
  }

  if (auto *session = GetWorldSession(L)) {
    session->interaction().SendChannelCommand(static_cast<std::uint16_t>(opcode),
                                              resolved->channel->name);
  }
  return 0;
}

}

int LuaEnumerateServerChannels(lua_State *L) {
  const auto *dbc = GetDbcLoader(L);
  const auto* session = GetWorldSession(L);
  if (dbc == nullptr || session == nullptr) {
    return 0;
  }

  const auto current_area_id = session->objects().GetAreaId();
  if (current_area_id == 0) {
    return 0;
  }

  const auto *area = dbc->area_table().LookupEntry(current_area_id);
  if (area == nullptr) {
    return 0;
  }

  const bool include_restricted_channels =
      (area->flags & openwow::data::dbc::kAreaFlagSlaveCapital) != 0;
  int pushed = 0;
  for (const auto &entry : dbc->chat_channels().entries()) {
    if ((entry.flags & kEnumerateServerChannelRestrictedAreaMask) != 0 &&
        !include_restricted_channels) {
      continue;
    }

    const std::string_view channel_name = entry.name.empty() ? entry.pattern : entry.name;
    lua_pushlstring(L, channel_name.data(), channel_name.size());
    ++pushed;
  }

  return pushed;
}

int LuaGetChannelList(lua_State *L) {
  const auto &cs = ::openwow::game::ChatSystem::Get();
  auto num_channels = cs.GetNumChannels();
  int pushed = 0;
  for (std::size_t i = 0; i < num_channels; ++i) {
    const auto *chan = cs.GetChannel(i);
    if (!chan || chan->id == 0 || chan->lua_hidden || !chan->show_in_display) {
      continue;
    }

    lua_pushnumber(L, chan->id);
    lua_pushstring(L, chan->DisplayNameOrName().c_str());
    pushed += 2;
  }
  return pushed;
}

int LuaChannelToggleAnnouncements(lua_State *L) {
  return LuaSingleChannelCommand(
      L, "ChannelToggleAnnouncements",
      static_cast<std::uint16_t>(openwow::net::wotlk::Opcode::CMSG_CHANNEL_ANNOUNCEMENTS));
}

int LuaChannelVoiceOn(lua_State *L) {
  return LuaSingleChannelCommand(
      L, "ChannelVoiceOn",
      static_cast<std::uint16_t>(openwow::net::wotlk::Opcode::CMSG_CHANNEL_VOICE_ON));
}

int LuaChannelVoiceOff(lua_State *L) {
  return LuaSingleChannelCommand(
      L, "ChannelVoiceOff",
      static_cast<std::uint16_t>(openwow::net::wotlk::Opcode::CMSG_CHANNEL_VOICE_OFF));
}

int LuaDisplayChannelVoiceOn(lua_State *L) {
  return LuaDisplayChannelVoiceCommand(L, "DisplayChannelVoiceOn",
                                       openwow::net::wotlk::Opcode::CMSG_CHANNEL_VOICE_ON);
}

int LuaDisplayChannelVoiceOff(lua_State *L) {
  return LuaDisplayChannelVoiceCommand(L, "DisplayChannelVoiceOff",
                                       openwow::net::wotlk::Opcode::CMSG_CHANNEL_VOICE_OFF);
}

int LuaChannelModerator(lua_State *L) {
  return LuaChannelCommandWithTarget(L, "ChannelModerator",
                                     openwow::net::wotlk::Opcode::CMSG_CHANNEL_MODERATOR);
}

int LuaChannelUnmoderator(lua_State *L) {
  return LuaChannelCommandWithTarget(L, "ChannelUnmoderator",
                                     openwow::net::wotlk::Opcode::CMSG_CHANNEL_UNMODERATOR);
}

int LuaChannelKick(lua_State *L) {
  return LuaChannelCommandWithTarget(L, "ChannelKick",
                                     openwow::net::wotlk::Opcode::CMSG_CHANNEL_KICK);
}

int LuaChannelBan(lua_State *L) {
  return LuaChannelCommandWithTarget(L, "ChannelBan",
                                     openwow::net::wotlk::Opcode::CMSG_CHANNEL_BAN);
}

int LuaChannelUnban(lua_State *L) {
  return LuaChannelCommandWithTarget(L, "ChannelUnban",
                                     openwow::net::wotlk::Opcode::CMSG_CHANNEL_UNBAN);
}

int LuaChannelInvite(lua_State *L) {
  return LuaChannelCommandWithTarget(L, "ChannelInvite",
                                     openwow::net::wotlk::Opcode::CMSG_CHANNEL_INVITE);
}

int LuaListChannels(lua_State *L) {
  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }
  static constexpr std::size_t kBufSize = 3000;
  static constexpr std::size_t kTmpSize = 138;

  auto &cs = ::openwow::game::ChatSystem::Get();
  std::size_t count = cs.GetNumChannels();

  std::string buf;
  buf.reserve(kBufSize);

  for (std::size_t i = 0; i < count; ++i) {
    const auto *ch = cs.GetChannel(i);
    if (!ch || ch->id == 0 || ch->lua_hidden)
      continue;

    char tmp[kTmpSize + 2];
    std::snprintf(tmp, sizeof(tmp), "[%u. %s] ", ch->id, ch->name.c_str());

    std::size_t tmp_len = std::strlen(tmp);
    if (buf.size() + tmp_len >= kBufSize) {
      openwow::game::ChatFrame_DisplayMessage(
          session->objects(), buf.c_str(), openwow::game::ChatDisplayType::kChannelList,
          nullptr, 0, nullptr, nullptr, nullptr, 0, 0, 0, 0, 0,
          nullptr);
      buf.clear();
    }
    buf.append(tmp, tmp_len);
  }

  openwow::game::ChatFrame_DisplayMessage(
      session->objects(), buf.c_str(), openwow::game::ChatDisplayType::kChannelList,
      nullptr, 0, nullptr, nullptr, nullptr, 0, 0, 0, 0, 0,
      nullptr);
  return 0;
}

int LuaGetSelectedDisplayChannel(lua_State *L) {
  const auto selected_index = ::openwow::game::ChatSystem::Get().GetSelectedDisplayChannelIndex();
  if (!selected_index.has_value()) {
    lua_pushnil(L);
    return 1;
  }

  lua_pushnumber(L, static_cast<lua_Number>(*selected_index + 1u));
  return 1;
}

int PushSelectedDisplayChannelPrivilege(lua_State *L, const std::uint8_t flag_mask) {
  auto &chat_system = ::openwow::game::ChatSystem::Get();
  const auto selected_index = chat_system.GetSelectedDisplayChannelIndex();
  if (!selected_index.has_value()) {
    lua_pushnil(L);
    return 1;
  }

  const auto resolved = chat_system.ResolveDisplayChannel(*selected_index);
  if (!resolved.has_value() ||
      resolved->kind != ::openwow::game::DisplayChannelKind::kJoinedChannel ||
      !resolved->channel.has_value()) {
    lua_pushnil(L);
    return 1;
  }

  if ((chat_system.GetSelectedJoinedChannelSelfFlags() & flag_mask) == 0) {
    lua_pushnil(L);
    return 1;
  }

  lua_pushnumber(L, 1.0);
  return 1;
}

int LuaSetSelectedDisplayChannel(lua_State *L) {
  const auto zero_based_index = GetLuaWrappedZeroBasedIndex(L, 1);
  const auto resolved = ResolveLuaDisplayChannel(L, 1);
  if (!resolved || resolved->kind == ::openwow::game::DisplayChannelKind::kInvalid) {
    return 0;
  }

  if (zero_based_index.has_value()) {
    SoundRuntimeForLua(L).SetBackgroundSoundState(
        static_cast<std::uint32_t>(*zero_based_index));
  }

  auto &chat_system = ::openwow::game::ChatSystem::Get();
  const std::string channel_name =
      (resolved->kind == ::openwow::game::DisplayChannelKind::kJoinedChannel && resolved->channel)
          ? resolved->channel->name
          : std::string();
  if (!chat_system.SelectDisplayChannel(resolved->kind, channel_name)) {
    return 0;
  }

  if (resolved->kind != ::openwow::game::DisplayChannelKind::kJoinedChannel || !resolved->channel) {
    chat_system.ClearWatchedChannelSelection();
    return 0;
  }

  chat_system.SelectWatchedJoinedChannel(resolved->channel->name);

  if (auto* session = GetWorldSession(L)) {
    session->interaction().SendChannelCommand(
        static_cast<std::uint16_t>(openwow::net::wotlk::Opcode::CMSG_SET_CHANNEL_WATCH),
        resolved->channel->name);
    session->interaction().SendChannelCommand(
        static_cast<std::uint16_t>(openwow::net::wotlk::Opcode::CMSG_CHANNEL_DISPLAY_LIST),
        resolved->channel->name);
  }
  return 0;
}

int LuaIsDisplayChannelOwner(lua_State *L) {
  return PushSelectedDisplayChannelPrivilege(L, 0x01u);
}

int LuaIsDisplayChannelModerator(lua_State *L) {
  return PushSelectedDisplayChannelPrivilege(L, 0x03u);
}

int LuaIsSilenced(lua_State *L) {
  if (lua_isstring(L, 1) == 0) {
    return 0;
  }

  auto *session = GetWorldSession(L);
  if (session == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  const char *token_or_name = lua_tostring(L, 1);
  const auto guid =
      ResolveWatchedChannelRosterLookupGuid(*session, token_or_name ? token_or_name : "");
  if (guid.IsEmpty()) {
    lua_pushnil(L);
    return 1;
  }

  const auto member = FindWatchedChannelRosterMemberByGuid(guid.GetRawValue());
  lua_pushwowbool(L, member.has_value() && (member->raw_flags & 0x10u) == 0);
  return 1;
}

int LuaGetChannelRosterInfo(lua_State *L) {
  const auto display_index = GetLuaWrappedZeroBasedIndex(L, 1);
  const auto resolved = ResolveLuaDisplayChannel(L, 1);
  const auto member_index = GetLuaWrappedZeroBasedIndex(L, 2);
  if (!display_index || !resolved || !member_index) {
    return 0;
  }

  auto &chat_system = ::openwow::game::ChatSystem::Get();
  if (resolved->kind == ::openwow::game::DisplayChannelKind::kSpecialSlot2) {
    const auto selected_index = chat_system.GetSelectedDisplayChannelIndex();
    if (!selected_index.has_value() || *selected_index != *display_index) {
      chat_system.ClearWatchedChannelSelection();
      chat_system.SelectDisplayChannel(resolved->kind);
      return 0;
    }

    const auto roster = BuildPartyDisplayChannelRoster(GetWorldSession(L));
    if (*member_index >= roster.size()) {
      return 0;
    }

    PushChannelRosterInfo(L, roster[*member_index]);
    return 6;
  }

  if (resolved->kind != ::openwow::game::DisplayChannelKind::kJoinedChannel || !resolved->channel) {
    return 0;
  }

  if (!chat_system.IsWatchingJoinedChannel(resolved->channel->name)) {
    SendWatchedChannelSelection(L, resolved->channel->name, true);
    return 0;
  }

  if (chat_system.GetWatchedChannelRosterPendingQueries() != 0) {
    return 0;
  }

  const auto member = chat_system.GetWatchedChannelRosterMember(*member_index);
  if (!member.has_value()) {
    return 0;
  }

  PushChannelRosterInfo(L, *member);
  return 6;
}

int LuaChannelMute(lua_State *L) {
  return LuaChannelCommandWithTarget(L, "ChannelMute",
                                     openwow::net::wotlk::Opcode::CMSG_CHANNEL_MUTE);
}

int LuaChannelUnmute(lua_State *L) {
  return LuaChannelCommandWithTarget(L, "ChannelUnmute",
                                     openwow::net::wotlk::Opcode::CMSG_CHANNEL_UNMUTE);
}

int LuaChannelSilenceAll(lua_State *L) {
  const char *raw_channel = lua_tostring(L, 1);
  const auto channel_name = ResolveChannelNameOrIndex(raw_channel);
  if (!channel_name) {
    return 0;
  }

  if (MatchesRestrictedVoiceChannelName(*channel_name, false)) {
    HandleRestrictedVoiceSilenceTarget(L, 2, true);
    return 0;
  }

  return LuaChannelCommandWithTarget(L, "ChannelSilenceAll",
                                     openwow::net::wotlk::Opcode::CMSG_CHANNEL_SILENCE_ALL);
}

int LuaChannelSilenceVoice(lua_State *L) {
  const char *raw_channel = lua_tostring(L, 1);
  const auto channel_name = ResolveChannelNameOrIndex(raw_channel);
  if (!channel_name) {
    return 0;
  }

  if (MatchesRestrictedVoiceChannelName(*channel_name, true)) {
    HandleRestrictedVoiceSilenceTarget(L, 2, true);
    return 0;
  }

  return LuaChannelCommandWithTarget(L, "ChannelSilenceVoice",
                                     openwow::net::wotlk::Opcode::CMSG_CHANNEL_SILENCE_VOICE);
}

int LuaChannelUnSilenceAll(lua_State *L) {
  const char *raw_channel = lua_tostring(L, 1);
  const auto channel_name = ResolveChannelNameOrIndex(raw_channel);
  if (!channel_name) {
    return 0;
  }

  if (MatchesRestrictedVoiceChannelName(*channel_name, false)) {
    HandleRestrictedVoiceSilenceTarget(L, 2, false);
    return 0;
  }

  return LuaChannelCommandWithTarget(L, "ChannelUnSilenceAll",
                                     openwow::net::wotlk::Opcode::CMSG_CHANNEL_UNSILENCE_ALL);
}

int LuaChannelUnSilenceVoice(lua_State *L) {
  const char *raw_channel = lua_tostring(L, 1);
  const auto channel_name = ResolveChannelNameOrIndex(raw_channel);
  if (!channel_name) {
    return 0;
  }

  if (MatchesRestrictedVoiceChannelName(*channel_name, true)) {
    HandleRestrictedVoiceSilenceTarget(L, 2, false);
    return 0;
  }

  return LuaChannelCommandWithTarget(L, "ChannelUnSilenceVoice",
                                     openwow::net::wotlk::Opcode::CMSG_CHANNEL_UNSILENCE_VOICE);
}

int LuaDisplayChannelOwner(lua_State *L) {
  return LuaSingleChannelCommand(
      L, "DisplayChannelOwner",
      static_cast<std::uint16_t>(openwow::net::wotlk::Opcode::CMSG_CHANNEL_OWNER));
}

int LuaSilenceMember(lua_State *L) {
  HandleRestrictedVoiceSilenceTarget(L, 1, true);
  return 0;
}

int LuaUnSilenceMember(lua_State *L) {
  HandleRestrictedVoiceSilenceTarget(L, 1, false);
  return 0;
}

namespace {

int LuaSetClientTextLogEnabled(lua_State* L,
                               const openwow::game::ClientTextLogKind kind) {
  if (lua_gettop(L) >= 1) {
    openwow::game::SetClientTextLogEnabled(
        kind, ScriptReadBoolArgOrDefault(L, 1, true));
  }

  if (openwow::game::IsClientTextLogEnabled(kind)) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

}

int LuaLoggingChat(lua_State *L) {
  return LuaSetClientTextLogEnabled(L, openwow::game::ClientTextLogKind::Chat);
}

int LuaLoggingCombat(lua_State *L) {
  return LuaSetClientTextLogEnabled(L,
                                    openwow::game::ClientTextLogKind::Combat);
}

int LuaAddChatWindowChannel(lua_State *L) {
  if (!lua_isnumber(L, 1) || !lua_isstring(L, 2)) {
    return luaL_error(L, "Usage: AddChatWindowChannel(index, \"channel\")");
  }

  const auto window = GetLuaChatWindowLayoutIndex(L, 1);
  if (!window) {
    return 0;
  }

  const char *raw_channel = lua_tostring(L, 2);
  const auto channel = FindJoinedChannelByName(raw_channel ? raw_channel : "");
  if (channel) {
    openwow::ui::game::ChatWindowState::Get().AddChannel(*window, channel->name, channel->id);
    lua_pushnumber(L, static_cast<lua_Number>(channel->id));
    return 1;
  }

  const std::string channel_name = raw_channel ? raw_channel : "";
  openwow::ui::game::ChatWindowState::Get().AddChannel(*window, channel_name, 0);
  lua_pushnumber(L, 0.0);
  return 1;
}

int LuaAddChatWindowMessages(lua_State *L) {
  if (!lua_isnumber(L, 1) || !lua_isstring(L, 2)) {
    return luaL_error(L, "Usage: AddChatWindowMessages(index, \"messageGroup\")");
  }

  const auto window = GetLuaChatWindowLayoutIndex(L, 1);
  if (!window) {
    return 0;
  }

  const char* group = lua_tostring(L, 2);
  if (group != nullptr) {
    openwow::ui::game::ChatWindowState::Get().AddMessageGroup(*window, group);
  }
  return 0;
}

int LuaChangeChatColor(lua_State *L) {
  if (!lua_isstring(L, 1) || !lua_isnumber(L, 2) || !lua_isnumber(L, 3) || !lua_isnumber(L, 4)) {
    return luaL_error(L, "Usage: ChangeChatColor(chatType, r, g, b)");
  }
  auto chat_type = SafeLuaString(L, 1);
  const auto r = lua_tonumber(L, 2);
  const auto g = lua_tonumber(L, 3);
  const auto b = lua_tonumber(L, 4);

  if (!openwow::game::ChatLuaBridge::Get().SetChatTypeColor(
          chat_type, static_cast<float>(r), static_cast<float>(g), static_cast<float>(b))) {
    return 0;
  }

  if (auto *mgr = GetRegisteredGameUiManager(L); mgr) {
    mgr->frame_events().dispatcher().FireEventArgs(events::UPDATE_CHAT_COLOR,
                                {chat_type, DecodeChatColorComponent(EncodeChatColorComponent(r)),
                                 DecodeChatColorComponent(EncodeChatColorComponent(g)),
                                 DecodeChatColorComponent(EncodeChatColorComponent(b))});
  }
  return 0;
}

int LuaResetChatColors(lua_State *L) {
  (void)L;
  openwow::game::ChatLuaBridge::Get().ResetChatColors();
  return 0;
}

int LuaResetChatWindows(lua_State *L) {
  auto& chat_window_state = openwow::ui::game::ChatWindowState::Get();
  chat_window_state.Reset();

  auto& chat_types = openwow::game::ChatLuaBridge::Get();
  chat_types.ResetChatTypeVisuals();

  const auto& chat_system = openwow::game::ChatSystem::Get();
  const auto channel_count = chat_system.GetNumChannels();
  for (std::size_t i = 0; i < channel_count; ++i) {
    const auto* channel = chat_system.GetChannel(i);
    if (!channel || !channel->is_joined) {
      continue;
    }

    chat_window_state.AddChannel(0, channel->name, channel->id);
  }

  if (auto* manager = GetRegisteredGameUiManager(L); manager) {
    manager->frame_events().dispatcher().FireEvent(events::UPDATE_CHAT_WINDOWS);
    for (const auto& state : chat_types.GetChatTypeVisualStates()) {
      manager->frame_events().dispatcher().FireEventArgs(
          events::UPDATE_CHAT_COLOR,
                                      {state.token, static_cast<double>(state.r),
                                       static_cast<double>(state.g),
                                       static_cast<double>(state.b)});
      manager->frame_events().dispatcher().FireEventArgs(
          events::UPDATE_CHAT_COLOR_NAME_BY_CLASS,
                                      {state.token, state.colorNameByClass});
    }
  }

  return 0;
}

int LuaJoinPermanentChannel(lua_State *L) {
  return LuaJoinChannelImpl(L, true, "JoinPermanentChannel");
}

int LuaJoinTemporaryChannel(lua_State *L) {
  auto *session = GetWorldSession(L);
  auto name = SafeLuaString(L, 1);
  auto password = SafeLuaString(L, 2);
  if (name.empty())
    return 0;
  if (!::openwow::game::ChatSystem::Get().QueuePendingNumberedChannel(name).has_value()) {
    return 0;
  }
  if (session) {
    session->interaction().SendJoinChannel(0, name, password);
  }
  return 0;
}

int LuaRemoveChatWindowChannel(lua_State *L) {
  if (!lua_isnumber(L, 1) || !lua_isstring(L, 2)) {
    return luaL_error(L, "Usage: RemoveChatWindowChannel(index, \"channel\")");
  }

  const auto window = GetLuaChatWindowLayoutIndex(L, 1);
  if (!window) {
    return 0;
  }

  const char *raw_channel = lua_tostring(L, 2);
  if (!raw_channel || raw_channel[0] == '\0') {
    return 0;
  }

  if (const auto resolved = ResolveChannelNameOrIndex(raw_channel)) {
    openwow::ui::game::ChatWindowState::Get().RemoveChannel(*window, *resolved);
  } else {
    openwow::ui::game::ChatWindowState::Get().RemoveChannel(*window, raw_channel);
  }
  return 0;
}

int LuaRemoveChatWindowMessages(lua_State *L) {
  if (!lua_isnumber(L, 1) || !lua_isstring(L, 2)) {
    return luaL_error(L, "Usage: RemoveChatWindowMessages(index, \"messageGroup\")");
  }

  const auto window = GetLuaChatWindowLayoutIndex(L, 1);
  if (!window) {
    return 0;
  }

  const char* group = lua_tostring(L, 2);
  if (group != nullptr) {
    openwow::ui::game::ChatWindowState::Get().RemoveMessageGroup(*window, group);
  }
  return 0;
}

int LuaSetChannelOwner(lua_State *L) {
  return LuaChannelCommandWithTarget(L, "SetChannelOwner",
                                     openwow::net::wotlk::Opcode::CMSG_CHANNEL_SET_OWNER);
}

int LuaSetChannelPassword(lua_State *L) {
  if (!lua_isstring(L, 1) || !lua_isstring(L, 2)) {
    return luaL_error(L, "Usage: SetChannelPassword(\"name\", \"password\")");
  }

  const char *raw_channel = lua_tostring(L, 1);
  const auto channel_name = ResolveChannelNameOrIndex(raw_channel);
  if (!channel_name) {
    return 0;
  }

  const char *password = lua_tostring(L, 2);
  if (auto *session = GetWorldSession(L)) {
    session->interaction().SendChannelStringPairCommand(
        static_cast<std::uint16_t>(openwow::net::wotlk::Opcode::CMSG_CHANNEL_PASSWORD),
        *channel_name, password ? password : "");
  }
  return 0;
}

int LuaSetChatWindowAlpha(lua_State *L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: SetChatWindowAlpha(index, alpha)");
  }

  const auto window = GetLuaChatWindowLayoutIndex(L, 1);
  if (!window) {
    return 0;
  }

  const auto alpha = DecodeChatWindowColorComponent(
      QuantizeChatWindowColorComponent(lua_tonumber(L, 2)));
  openwow::ui::game::ChatWindowState::Get().SetWindowAlpha(*window, alpha);
  return 0;
}

int LuaSetChatWindowColor(lua_State *L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2) || !lua_isnumber(L, 3) ||
      !lua_isnumber(L, 4)) {
    return luaL_error(L, "Usage: SetChatWindowColor(index, r, g, b)");
  }

  const auto window = GetLuaChatWindowLayoutIndex(L, 1);
  if (!window) {
    return 0;
  }

  openwow::ui::game::ChatWindowState::Get().SetWindowColor(
      *window, DecodeChatWindowColorComponent(
                   QuantizeChatWindowColorComponent(lua_tonumber(L, 2))),
      DecodeChatWindowColorComponent(
          QuantizeChatWindowColorComponent(lua_tonumber(L, 3))),
      DecodeChatWindowColorComponent(
          QuantizeChatWindowColorComponent(lua_tonumber(L, 4))));
  return 0;
}

int LuaSetChatWindowDocked(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: SetChatWindowDocked(index, docked)");
  }

  const auto window = GetLuaChatWindowLayoutIndex(L, 1);
  if (!window) {
    return 0;
  }

  int dock_target = 0;
  if (lua_isnumber(L, 2)) {
    dock_target = TruncateLuaNumberToSseI32(lua_tonumber(L, 2));
  }

  openwow::ui::game::ChatWindowState::Get().SetWindowDockTarget(*window, dock_target);
  return 0;
}

int LuaSetChatWindowLocked(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: SetChatWindowLocked(index, locked)");
  }

  const auto window = GetLuaChatWindowLayoutIndex(L, 1);
  if (!window) {
    return 0;
  }

  openwow::ui::game::ChatWindowState::Get().SetWindowLocked(
      *window, ScriptReadBoolArgOrDefault(L, 2, false));
  return 0;
}

int LuaSetChatWindowName(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: SetChatWindowName(index, \"name\")");
  }

  const auto window = GetLuaChatWindowLayoutIndex(L, 1);
  if (!window) {
    return 0;
  }

  const char* name = "";
  if (lua_isstring(L, 2)) {
    name = lua_tostring(L, 2);
  }

  openwow::ui::game::ChatWindowState::Get().SetWindowName(*window, name);
  return 0;
}

int LuaSetChatWindowShown(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: SetChatWindowShown(index, shown)");
  }

  const auto window = GetLuaChatWindowLayoutIndex(L, 1);
  if (!window) {
    return 0;
  }

  openwow::ui::game::ChatWindowState::Get().SetWindowShown(
      *window, ScriptReadBoolArgOrDefault(L, 2, true));
  return 0;
}

int LuaSetChatWindowSize(lua_State *L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: SetChatWindowSize(index, size)");
  }

  const auto window = GetLuaChatWindowLayoutIndex(L, 1);
  if (!window) {
    return 0;
  }

  const auto font_size = TruncateLuaNumberToSseI32(lua_tonumber(L, 2));
  openwow::ui::game::ChatWindowState::Get().SetWindowFontSize(*window, font_size);
  return 0;
}

int LuaSetChatWindowUninteractable(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: SetChatWindowUninteractable(index, uninteractable)");
  }

  const auto window = GetLuaChatWindowLayoutIndex(L, 1);
  if (!window) {
    return 0;
  }

  openwow::ui::game::ChatWindowState::Get().SetWindowUninteractable(
      *window, ScriptReadBoolArgOrDefault(L, 2, false));
  return 0;
}

int LuaCanComplainChat(lua_State *L) {
  auto* session = GetWorldSession(L);
  if (!session) {
    FrameScript_PushNil(L);
    return 1;
  }

  constexpr const char* kUsage =
      "Usage: CanComplainChat(lineID) or CanComplainChat(name) or CanComplainChat(name, text)";
  const auto record = FindChatComplaintRecord(L, *session, kUsage);
  if (record.has_value() && CanComplainAboutChatRecord(*session, *record)) {
    FrameScript_PushNumber(L, 1.0);
  } else {
    FrameScript_PushNil(L);
  }
  return 1;
}

int LuaGetChatTypeIndex(lua_State *L) {
  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "Usage: GetChatTypeIndex(type)");
  }

  const auto chat_type = SafeLuaString(L, 1);
  lua_pushnumber(
      L, static_cast<lua_Number>(openwow::game::ChatLuaBridge::Get().GetChatTypeIndex(chat_type)));
  return 1;
}

int LuaGetChatWindowChannels(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetChatWindowChannels(index)");
  }

  const auto window = GetLuaChatWindowLayoutIndex(L, 1);
  if (!window) {
    return 0;
  }

  const auto channels = openwow::ui::game::ChatWindowState::Get().GetChannels(*window);
  int pushed = 0;
  for (const auto &channel : channels) {
    lua_pushstring(L, channel.name.c_str());
    lua_pushnumber(L, static_cast<lua_Number>(channel.number));
    pushed += 2;
  }
  return pushed;
}

int LuaGetChatWindowInfo(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetChatWindowInfo(index)");
  }

  const auto window = GetLuaChatWindowLayoutIndex(L, 1);
  if (!window) {
    return 0;
  }

  const auto win = openwow::ui::game::ChatWindowState::Get().TryGetWindow(*window);
  if (!win.has_value()) {
    return 0;
  }

  lua_pushstring(L, win->name.c_str());
  lua_pushnumber(L, static_cast<lua_Number>(win->font_size));
  lua_pushnumber(L, static_cast<lua_Number>(win->r));
  lua_pushnumber(L, static_cast<lua_Number>(win->g));
  lua_pushnumber(L, static_cast<lua_Number>(win->b));
  lua_pushnumber(L, static_cast<lua_Number>(win->alpha));
  lua_pushwowbool(L, win->shown);
  lua_pushwowbool(L, win->locked);

  if (win->dock_target > 0) {
    lua_pushnumber(L, static_cast<lua_Number>(win->dock_target));
  } else {
    lua_pushnil(L);
  }

  if (win->uninteractable) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 10;
}

int LuaGetChatWindowMessages(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetChatWindowMessages(index)");
  }
  const auto window = GetLuaChatWindowLayoutIndex(L, 1);
  if (!window) {
    return 0;
  }
  const auto groups = openwow::ui::game::ChatWindowState::Get().GetMessageGroups(*window);
  const int result_count = openwow::ui::ReserveLuaResultCapacity(
      L, groups.size(), "chat message groups");
  for (const auto& group : groups) {
    lua_pushstring(L, group.c_str());
  }
  return result_count;
}

int LuaGetChatWindowSavedPosition(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetChatWindowSavedPosition(index)");
  }
  const auto window = GetLuaChatWindowLayoutIndex(L, 1);
  if (!window)
    return 0;
  const auto saved_position = openwow::ui::game::ChatWindowState::Get().GetSavedPosition(*window);
  if (!saved_position)
    return 0;
  lua_pushstring(L, openwow::ui::FramePointToString(saved_position->point));
  lua_pushnumber(L, static_cast<lua_Number>(saved_position->x));
  lua_pushnumber(L, static_cast<lua_Number>(saved_position->y));
  return 3;
}

int LuaGetChatWindowSavedDimensions(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetChatWindowSavedDimensions(index)");
  }
  const auto window = GetLuaChatWindowLayoutIndex(L, 1);
  if (!window)
    return 0;
  const auto saved_dimensions =
      openwow::ui::game::ChatWindowState::Get().GetSavedDimensions(*window);
  if (!saved_dimensions)
    return 0;
  lua_pushnumber(L, static_cast<lua_Number>(saved_dimensions->width));
  lua_pushnumber(L, static_cast<lua_Number>(saved_dimensions->height));
  return 2;
}

int LuaComplainChat(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;

  constexpr const char* kUsage =
      "Usage: ComplainChat(lineID) or ComplainChat(name) or ComplainChat(name, text)";
  const auto record = FindChatComplaintRecord(L, *session, kUsage);
  if (!record.has_value() || !CanComplainAboutChatRecord(*session, *record)) {
    return 0;
  }

  if (session->social().HasRecentComplaintGuid(record->sender_guid)) {
    const std::string message =
        openwow::game::Localization::Get().GetString("COMPLAINT_ADDED", "COMPLAINT_ADDED");
    openwow::game::ChatFrame_DisplayMessage(
        session->objects(), message.c_str(), openwow::game::ChatDisplayType::kSystem, nullptr, 0, nullptr,
        nullptr, nullptr, 0, 0, 0, 0, 0, nullptr);
    return 0;
  }

  session->social().RememberRecentComplaintGuid(record->sender_guid);
  session->interaction().SendChatComplain(
      record->sender_guid, record->aux_value,
      static_cast<std::uint32_t>(record->chat_type), record->channel_lookup_id,
      record->recorded_at, record->formatted_line);
  return 0;
}

int LuaSetChatColorNameByClass(lua_State *L) {
  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "Usage: SetChatColorNameByClass(chatType, colorNameByClass)");
  }

  const auto chat_type = SafeLuaString(L, 1);
  const bool enabled = lua_toboolean(L, 2) != 0;
  if (!openwow::game::ChatLuaBridge::Get().SetChatColorNameByClass(chat_type, enabled)) {
    return 0;
  }

  if (auto *mgr = GetRegisteredGameUiManager(L); mgr) {
    mgr->frame_events().dispatcher().FireEventArgs(
        events::UPDATE_CHAT_COLOR_NAME_BY_CLASS, {chat_type, enabled});
  }
  return 0;
}

int LuaSetChatWindowSavedDimensions(lua_State *L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2) || !lua_isnumber(L, 3)) {
    return luaL_error(L, "Usage: SetChatWindowSavedDimensions(index, width, height)");
  }
  const auto window = GetLuaChatWindowLayoutIndex(L, 1);
  if (!window) {
    return 0;
  }
  const float width = static_cast<float>(lua_tonumber(L, 2));
  const float height = static_cast<float>(lua_tonumber(L, 3));
  openwow::ui::game::ChatWindowState::Get().SetSavedDimensions(*window, width, height);
  return 0;
}

int LuaSetChatWindowSavedPosition(lua_State *L) {
  if (!lua_isnumber(L, 1) || !lua_isstring(L, 2) || !lua_isnumber(L, 3) || !lua_isnumber(L, 4)) {
    return luaL_error(
        L, "Usage: SetChatWindowSavedPosition(index, \"point\", xOffsetRatio, yOffsetRatio)");
  }

  const auto window = GetLuaChatWindowLayoutIndex(L, 1);
  if (!window) {
    return 0;
  }

  int frame_point = 0;
  if (!openwow::ui::StringToFramePoint(lua_tostring(L, 2), &frame_point)) {
    return luaL_error(L, "Unknown Region Point");
  }

  const float x = static_cast<float>(lua_tonumber(L, 3));
  const float y = static_cast<float>(lua_tonumber(L, 4));
  openwow::ui::game::ChatWindowState::Get().SetSavedPosition(*window, frame_point, x, y);
  return 0;
}

int LuaDoEmote(lua_State *L) {
  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "Usage: DoEmote(\"emote\"[, \"target\"])");
  }

  const char *token = lua_tostring(L, 1);
  if (token == nullptr || token[0] == '\0') {
    return 0;
  }

  const char *target_token_or_name = lua_tostring(L, 2);
  auto *session = GetWorldSession(L);
  if (openwow::core::SStrCmpNoCase(token, "DANCE", 0x7FFFFFFFu) == 0 &&
      session != nullptr &&
      target_token_or_name != nullptr &&
      session->dance_studio().HasDanceByName(target_token_or_name)) {
    (void)session->dance_studio().SendPlayDance(target_token_or_name);
    return 0;
  }

  if (session == nullptr) {
    return 0;
  }

  const auto *dbc = session->GetDbcLoader();
  if (dbc == nullptr) {
    return 0;
  }

  const auto *text_emote = dbc->emotes_text().LookupByNameCaseInsensitive(token);
  if (text_emote == nullptr) {
    return 0;
  }

  std::uint64_t target_guid = session->objects().GetTargetGuid().GetRawValue();
  if (target_token_or_name != nullptr && target_token_or_name[0] != '\0' &&
      target_token_or_name[0] != '%') {
    target_guid =
        ResolveGameUiLookup(session, target_token_or_name, openwow::game::kTypeMaskPlayer, 0,
                            false, false)
            .GetRawValue();
  }

  session->interaction().SendTextEmote(text_emote->id, target_guid);
  return 0;
}

int LuaConsoleAddMessage(lua_State *L) {
  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "Usage: ConsoleAddMessage(string)");
  }

  const char *msg = lua_tostring(L, 1);
  if (msg != nullptr && msg[0] != '\0') {
    openwow::core::ida::ConsoleLog("%s", msg);
  }
  return 0;
}

int LuaSendSystemMessage(lua_State *L) {
  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "Usage: SendSystemMessage(\"message\")");
  }

  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  const char *msg = lua_tostring(L, 1);
  openwow::game::ChatFrame_DisplayMessage(
      session->objects(), msg, openwow::game::ChatDisplayType::kSystem, nullptr, 0, nullptr, nullptr, nullptr, 0, 0, 0,
      0, 0, nullptr);
  return 0;
}

}

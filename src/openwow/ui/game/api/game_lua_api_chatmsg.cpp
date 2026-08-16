#include "openwow/ui/lua_c_api_convenience.h"

#include "openwow/ui/game/api/game_lua_api_chatmsg.h"
#include "openwow/core/storm_string.h"
#include "openwow/data/formats/dbc/dbc_enums.h"
#include "openwow/game/battlenet_api.h"
#include "openwow/game/chat_cache.h"
#include "openwow/game/chat_channel_location.h"
#include "openwow/game/chat_display.h"
#include "openwow/game/chat_manager.h"
#include "openwow/game/chat_system.h"
#include "openwow/game/chat_types.h"
#include "openwow/game/group_system.h"
#include "openwow/game/localization.h"
#include "openwow/game/object_manager.h"
#include "openwow/net/client_services.h"
#include "openwow/net/client_services_packet_sender.h"
#include "openwow/ui/game/autocomplete.h"
#include "openwow/ui/game/chat_window_state.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/lua_numeric.h"
#include "openwow/ui/lua_result_capacity.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

namespace {

constexpr openwow::game::BNetErrorCode kBNetWhisperUndeliverableError = 0x13E;
constexpr std::size_t kAddonMessagePayloadBufferSize = 3000;
constexpr std::uint32_t kChatChannelFlagStandardRecruitmentMode = 0x20000u;
struct ResolvedJoinChannelRequest {
  std::string packet_name;
  std::string packet_password;
  std::uint32_t lookup_id = 0;
  std::optional<std::string> canonical_name;
};

std::optional<openwow::game::ChatMsg> ParseAddonMessageChatType(
    const std::string_view chat_type) {
  using openwow::game::ChatMsg;

  ChatMsg resolved = ChatMsg::kParty;
  const std::string lookup_key(chat_type);
  if (!openwow::game::ChatTypeStringToID(lookup_key.c_str(), &resolved)) {
    return std::nullopt;
  }

  switch (resolved) {
    case ChatMsg::kParty:
    case ChatMsg::kRaid:
    case ChatMsg::kGuild:
    case ChatMsg::kWhisper:
    case ChatMsg::kBattleground:
      return resolved;
    default:
      return std::nullopt;
  }
}

std::string BuildAddonMessagePayload(const char* prefix, const char* message) {
  const std::string_view prefix_view = prefix != nullptr ? std::string_view(prefix)
                                                         : std::string_view();
  const std::string_view message_view = message != nullptr ? std::string_view(message)
                                                           : std::string_view();

  std::string payload;
  constexpr std::size_t kMaxPayloadBytes = kAddonMessagePayloadBufferSize - 1;
  payload.reserve(kMaxPayloadBytes);

  const auto append_bounded = [&](const std::string_view text) {
    const auto remaining = kMaxPayloadBytes - payload.size();
    payload.append(text.data(), std::min(text.size(), remaining));
  };

  append_bounded(prefix_view);
  append_bounded("\t");
  append_bounded(message_view);
  return payload;
}

bool HasBNetWhisperLuaAccess() {
  const auto &client_services = openwow::net::ClientServices::Instance();
  const auto &api = openwow::game::BattleNetApi::Instance();
  return client_services.HasBattleNetRidTransport() && api.IsRIDEnabled();
}

bool IsWhisperRecipientToonName(const openwow::game::BNetPresenceValue &value) {

  return value.type == openwow::game::BNetPresenceValue::Type::kToonName;
}

std::string GetLocalizedGlobalString(const char *key) {
  const std::string key_string = key != nullptr ? key : "";
  return openwow::game::Localization::Get().GetString(key_string, key_string);
}

void PushChannelDisplayCount(lua_State *L, const std::uint32_t count) {
  if (count != 0) {
    lua_pushnumber(L, static_cast<lua_Integer>(count));
  } else {
    lua_pushnil(L);
  }
}

ResolvedJoinChannelRequest ResolveJoinChannelRequest(
    openwow::game::WorldSession& session,
    const openwow::data::dbc::DbcLoader* dbc,
    const std::string_view requested_name,
    const std::string_view password,
    const bool permanent,
    bool* const valid) {
  ResolvedJoinChannelRequest request;
  request.packet_name.assign(requested_name);
  request.packet_password.assign(password);
  *valid = true;

  if (const auto* definition = openwow::game::FindChatChannelDefinitionByRowName(
          dbc, request.packet_name.c_str());
      definition != nullptr) {
    const auto channel_name =
        openwow::game::ResolveBuiltinChatChannelName(session, *definition);
    if (!channel_name.has_value()) {
      *valid = false;
      return request;
    }

    request.lookup_id = definition->id;
    request.canonical_name = std::string(definition->name);
    request.packet_name = *channel_name;
    request.packet_password.clear();
    if (permanent &&
        (definition->flags & kChatChannelFlagStandardRecruitmentMode) != 0) {
      openwow::game::SetGuildRecruitmentChannelAutoJoin(session, false, true);
    }
  }

  return request;
}

}

namespace openwow::ui::game::detail {

namespace {

struct ChatLanguageQueryContext {
  const openwow::game::CGPlayer_C* player = nullptr;
  const openwow::data::dbc::DbcLoader* dbc = nullptr;
};

std::optional<ChatLanguageQueryContext> ResolveChatLanguageQueryContext(
    lua_State* L) {
  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    return std::nullopt;
  }

  const auto* player = session->objects().GetLocalPlayerTyped();
  if (player == nullptr) {
    return std::nullopt;
  }

  auto* dbc = session->GetDbcLoader();
  if (dbc == nullptr) {
    dbc = GetDbcLoader(L);
  }
  if (dbc == nullptr) {
    return std::nullopt;
  }

  return ChatLanguageQueryContext{
      .player = player,
      .dbc = dbc,
  };
}

}

int LuaGetNumLanguages(lua_State *L) {
  const auto context = ResolveChatLanguageQueryContext(L);
  if (!context.has_value()) {
    return 0;
  }

  const auto languages =
      ::openwow::game::CollectAvailableChatLanguages(*context->player,
                                                     *context->dbc);
  lua_pushnumber(L, static_cast<lua_Number>(languages.size()));
  return 1;
}

int LuaGetLanguageByIndex(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetLanguageByIndex(index)");
  }

  const auto context = ResolveChatLanguageQueryContext(L);
  if (!context.has_value()) {
    return 0;
  }

  const auto one_based_index = openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1));
  if (one_based_index == 0) {
    return 0;
  }

  const auto languages =
      ::openwow::game::CollectAvailableChatLanguages(*context->player,
                                                     *context->dbc);
  if (one_based_index > languages.size()) {
    return 0;
  }

  const auto language_name = languages[one_based_index - 1].name;
  lua_pushlstring(L, language_name.data(),
                  static_cast<size_t>(language_name.size()));
  return 1;
}

int LuaJoinChannelImpl(lua_State *L, const bool permanent, const char *api_name) {
  if (!lua_isstring(L, 1)) {
    luaL_error(L, "Usage: %s(\"name\" [,\"password\"] [,index] [,hasVoice])", api_name);
  }

  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  const char *raw_name = lua_tostring(L, 1);
  const char *raw_password = lua_isstring(L, 2) ? lua_tostring(L, 2) : "";
  const std::string_view name =
      raw_name != nullptr ? std::string_view(raw_name) : std::string_view();
  const std::string_view password =
      raw_password != nullptr ? std::string_view(raw_password) : std::string_view();
  if (name.find(' ') != std::string_view::npos || name.size() >= 128 || password.size() >= 128) {
    return 0;
  }

  std::string filtered_name(name);
  if (::openwow::game::ChatFrame_MatureLanguageFilter(filtered_name, false, true)) {
    DisplaySystemMessage(590);
    return 0;
  }

  const auto *dbc = GetDbcLoader(L);
  if (dbc == nullptr) {
    dbc = session->GetDbcLoader();
  }

  const bool has_voice = ScriptReadBoolArgOrDefault(L, 4, false);
  bool join_request_valid = false;
  const auto join_request =
      ResolveJoinChannelRequest(*session, dbc, name, password, permanent,
                                &join_request_valid);
  if (!join_request_valid) {
    return 0;
  }

  auto& chat_system = ::openwow::game::ChatSystem::Get();
  const auto* existing_channel =
      join_request.lookup_id != 0 ? chat_system.GetChannelByLookupId(join_request.lookup_id)
                                  : chat_system.GetChannelByName(join_request.packet_name);
  if (existing_channel == nullptr) {
    const std::string pending_display_name = join_request.canonical_name.has_value()
                                                 ? *join_request.canonical_name
                                                 : std::string();
    const auto queued = chat_system.QueuePendingNumberedChannel(
        join_request.packet_name, join_request.lookup_id, pending_display_name,
        has_voice, permanent);
    if (!queued.has_value()) {
      return 0;
    }
  }

  if (lua_isnumber(L, 3)) {
    const auto requested_window = openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 3));
    if (requested_window >= 1 &&
        requested_window <= static_cast<std::uint32_t>(::openwow::ui::game::kMaxChatWindows)) {
      ::openwow::ui::game::ChatWindowState::Get().AddChannel(
          static_cast<int>(requested_window - 1),
          join_request.canonical_name.value_or(join_request.packet_name),
          join_request.lookup_id);
    }
  }

  if (existing_channel == nullptr) {
    session->interaction().SendJoinChannel(
        join_request.lookup_id, join_request.packet_name,
        join_request.packet_password, has_voice);
  }

  lua_pushinteger(L, static_cast<lua_Integer>(join_request.lookup_id));
  if (join_request.canonical_name.has_value()) {
    lua_pushlstring(L, join_request.canonical_name->data(),
                    join_request.canonical_name->size());
  } else {
    lua_pushnil(L);
  }
  return 2;
}

int LuaJoinChannelByName(lua_State *L) {
  return LuaJoinChannelImpl(L, false, "JoinChannelByName");
}

int LuaLeaveChannelByName(lua_State *L) {
  if (!lua_isstring(L, 1)) {
    luaL_error(L, "Usage: LeaveChannelByName(\"name\")");
  }
  const char *name = lua_tostring(L, 1);

  const auto resolved_name = ResolveChannelNameOrIndex(name);
  if (!resolved_name.has_value()) {
    return 0;
  }

  auto *session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  const auto *dbc = GetDbcLoader(L);
  if (dbc == nullptr) {
    dbc = session->GetDbcLoader();
  }

  const auto *definition =
      ::openwow::game::FindChatChannelDefinitionByRowName(dbc, name);
  std::string window_key = *resolved_name;
  std::uint32_t definition_id = 0;
  if (definition != nullptr) {

    if (!::openwow::game::ResolveBuiltinChatChannelName(*session, *definition)
             .has_value()) {
      return 0;
    }

    definition_id = definition->id;
    window_key.assign(definition->name);
    if ((definition->flags & kChatChannelFlagStandardRecruitmentMode) != 0) {

      ::openwow::game::SetGuildRecruitmentChannelAutoJoin(*session, false, true);
    }
  }

  ::openwow::ui::game::ChatWindowState::Get().RemoveChannelFromAllWindows(window_key);

  auto &cs = ::openwow::game::ChatSystem::Get();
  const auto *channel = definition_id != 0 ? cs.GetChannelByLookupId(definition_id)
                                           : cs.GetChannelByName(*resolved_name);
  if (channel == nullptr || channel->lua_hidden) {
    return 0;
  }

  const std::string leave_name = channel->name;
  const std::uint32_t leave_lookup_id = channel->lookup_id;
  auto pkt = ::openwow::game::ChatManager::BuildLeaveChannel(leave_lookup_id,
                                                             leave_name);
  (void)::openwow::net::ClientServices__SendPacket(pkt);

  cs.MarkChannelClientRequestedLeave(leave_name);

  return 0;
}

int LuaGetChannelName(lua_State *L) {
  const auto &cs = ::openwow::game::ChatSystem::Get();
  const ::openwow::game::ChatChannel *channel = nullptr;

  if (lua_isnumber(L, 1)) {

    const int index = TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
    std::size_t count = cs.GetChannelSlotCount();
    if (index >= 1 && static_cast<std::size_t>(index) <= count) {
      channel = cs.GetLuaChannelBySlot(static_cast<std::size_t>(index) - 1);
    }
  } else if (lua_isstring(L, 1)) {
    const char *ch_name = lua_tostring(L, 1);
    channel = cs.GetChannelByName(ch_name ? ch_name : "");
  } else {
    luaL_error(L, "Usage: GetChannelName([channelIndex] or [channelName])");
  }

  lua_pushnumber(L, channel ? static_cast<lua_Integer>(channel->id) : 0);
  if (channel) {
    lua_pushstring(L, channel->name.c_str());
  } else {
    lua_pushnil(L);

  }
  lua_pushnumber(L, channel ? static_cast<lua_Integer>(channel->instance_id) : 0);
  return 3;
}

int LuaGetNumDisplayChannels(lua_State *L) {
  const auto &cs = ::openwow::game::ChatSystem::Get();
  lua_pushnumber(L, static_cast<lua_Integer>(cs.GetNumDisplayChannels()));
  return 1;
}

namespace {

int LuaSetChannelHeaderCollapsed(lua_State *L, const bool collapsed) {
  if (!lua_isnumber(L, 1)) {
    return 0;
  }

  const auto one_based_index = openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1));
  if (one_based_index == 0) {
    return 0;
  }

  auto &chat_system = ::openwow::game::ChatSystem::Get();
  const bool changed = collapsed ? chat_system.CollapseDisplayChannelHeader(one_based_index - 1u)
                                 : chat_system.ExpandDisplayChannelHeader(one_based_index - 1u);
  if (changed) {
    ::openwow::ui::game::ScriptEventDispatch::Get().FireChannelUiUpdate();
  }

  return 0;
}

}

int LuaExpandChannelHeader(lua_State *L) {
  return LuaSetChannelHeaderCollapsed(L, false);
}

int LuaCollapseChannelHeader(lua_State *L) {
  return LuaSetChannelHeaderCollapsed(L, true);
}

int LuaGetChannelDisplayInfo(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetChannelDisplayInfo(index)");
  }

  const auto one_based_index = openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1));
  if (one_based_index == 0) {
    return 0;
  }

  const auto index = static_cast<std::size_t>(one_based_index - 1u);
  const auto &cs = ::openwow::game::ChatSystem::Get();
  const auto info = cs.GetDisplayChannelInfo(index);
  if (!info.has_value()) {
    return 0;
  }

  if (info->is_header) {
    const char *category_key = nullptr;
    switch (info->category) {
    case ::openwow::game::DisplayChannelCategory::kGroup:
      category_key = "CHANNEL_CATEGORY_GROUP";
      break;
    case ::openwow::game::DisplayChannelCategory::kWorld:
      category_key = "CHANNEL_CATEGORY_WORLD";
      break;
    case ::openwow::game::DisplayChannelCategory::kCustom:
      category_key = "CHANNEL_CATEGORY_CUSTOM";
      break;
    }

    lua_pushstring(L, GetLocalizedGlobalString(category_key).c_str());
    lua_pushnumber(L, 1.0);
    lua_pushwowbool(L, info->collapsed);
    lua_pushnil(L);
    PushChannelDisplayCount(L, info->member_count);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    return 9;
  }

  switch (info->kind) {
  case ::openwow::game::DisplayChannelKind::kJoinedChannel:
    lua_pushstring(L, info->display_name.c_str());
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnumber(L, static_cast<lua_Integer>(info->channel_number));
    PushChannelDisplayCount(L, info->member_count);
    lua_pushwowbool(L, info->active);
    lua_pushstring(L, info->category == ::openwow::game::DisplayChannelCategory::kWorld
                          ? "CHANNEL_CATEGORY_WORLD"
                          : "CHANNEL_CATEGORY_CUSTOM");
    lua_pushwowbool(L, info->voice_enabled);
    lua_pushwowbool(L, info->selected);
    return 9;

  case ::openwow::game::DisplayChannelKind::kSpecialSlot1:
  case ::openwow::game::DisplayChannelKind::kSpecialSlot2:
  case ::openwow::game::DisplayChannelKind::kSpecialSlot3: {
    const char *name_key = "CHAT_MSG_BATTLEGROUND";
    if (info->kind == ::openwow::game::DisplayChannelKind::kSpecialSlot2) {
      name_key = "CHAT_MSG_PARTY";
    } else if (info->kind == ::openwow::game::DisplayChannelKind::kSpecialSlot3) {
      name_key = "CHAT_MSG_RAID";
    }

    lua_pushstring(L, GetLocalizedGlobalString(name_key).c_str());
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    PushChannelDisplayCount(L, info->member_count);
    lua_pushwowbool(L, info->active);
    lua_pushstring(L, "CHANNEL_CATEGORY_GROUP");
    lua_pushwowbool(L, info->voice_enabled);
    lua_pushwowbool(L, info->selected);
    return 9;
  }

  case ::openwow::game::DisplayChannelKind::kInvalid:
    break;
  }

  for (int i = 0; i < 9; ++i) {
    lua_pushnil(L);
  }
  return 9;
}

int LuaListChannelByName(lua_State *L) {
  return LuaSingleChannelCommand(L, "ListChannelByName",
                                 static_cast<std::uint16_t>(net::wotlk::Opcode::CMSG_CHANNEL_LIST));
}

int LuaSendAddonMessage(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;

  if (session->objects().GetActivePlayer() == nullptr) {
    return 0;
  }

  const char* prefix = lua_tostring(L, 1);
  const char* text = lua_tostring(L, 2);
  if (prefix == nullptr) {
    prefix = "";
  }
  if (text == nullptr) {
    text = "";
  }

  if (prefix[0] == '\0' && text[0] == '\0') {
    return luaL_error(
        L, "Usage: SendAddonMessage(\"prefix\", \"message\" [,\"type\"] [,\"target\"])");
  }

  using openwow::game::ChatMsg;

  ChatMsg chat_type = ChatMsg::kParty;
  if (lua_isstring(L, 3)) {
    const std::string chat_type_string = SafeLuaString(L, 3);
    const auto resolved_type = ParseAddonMessageChatType(chat_type_string);
    if (!resolved_type.has_value()) {
      return luaL_error(L, "Unknown addon chat type");
    }

    chat_type = *resolved_type;
    auto& group_system = openwow::game::GroupSystem::Get();
    if (chat_type == ChatMsg::kRaid
        && (!group_system.IsInRaid() || group_system.GetMemberCount() == 0)) {
      chat_type = ChatMsg::kParty;
    }
  }

  if (chat_type == ChatMsg::kParty
      && !openwow::game::GroupSystem::Get().HasPartyMembers()) {
    return 0;
  }

  const char* target = lua_tostring(L, 4);
  if (target == nullptr) {
    target = "";
  }
  if (chat_type == ChatMsg::kWhisper && target[0] == '\0') {
    return luaL_error(L, "SendAddonMessage(): Whisper message missing target player!");
  }

  session->interaction().SendAddonMessage(static_cast<std::uint32_t>(chat_type),
                                          BuildAddonMessagePayload(prefix, text), target);
  return 0;
}

int LuaBNSendWhisper(lua_State *L) {
  auto &api = openwow::game::BattleNetApi::Instance();
  if (!HasBNetWhisperLuaAccess()) {
    return 0;
  }

  if (!lua_isnumber(L, 1) || !lua_isstring(L, 2)) {
    return luaL_error(L, "Usage: BNSendWhisper(id,text)");
  }

  if (!api.IsFullyConnected()) {
    return 0;
  }

  const auto presence_id = openwow::ui::SignedI32FromU32Bits(
      openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1)));
  const char *text = lua_tostring(L, 2);
  std::array<char, kBNetSanitizedChatTextBufferSize> whisper_text{};
  CopySanitizedBNetChatText(text ? std::string_view(text) : std::string_view(), whisper_text.data(),
                            whisper_text.size());

  const auto toon_name = api.GetPresenceValue(
      presence_id, static_cast<std::int32_t>(openwow::game::BNetPresenceKey::kToonName));
  if (!IsWhisperRecipientToonName(toon_name)) {
    api.HandleError(kBNetWhisperUndeliverableError, presence_id);
    return 0;
  }

  api.SendChatWhisper(presence_id, whisper_text.data());
  return 0;
}

int LuaGetAutoCompleteResults(lua_State *L) {
  if (lua_gettop(L) < 4) {
    luaL_error(L, "Usage: GetAutoCompleteResults(text, includeBitField, "
                  "excludeBitField, numReturns[, cursorPosition"
                  "[, allowFullMatch]])");
  }
  const char *text = lua_tostring(L, 1);

  const auto include_flags = static_cast<std::uint32_t>(lua_tonumber(L, 2));
  const auto exclude_flags = static_cast<std::uint32_t>(lua_tonumber(L, 3));
  int max_results = static_cast<int>(lua_tonumber(L, 4));
  if (max_results <= 0) {
    return 0;
  }

  if (!text || text[0] == '\0')
    return 0;

  std::size_t cursor_position = std::strlen(text);
  if (lua_isnumber(L, 5)) {
    const auto requested_cursor = static_cast<int>(lua_tonumber(L, 5));
    if (requested_cursor >= 0 && requested_cursor <= static_cast<int>(cursor_position)) {
      cursor_position = static_cast<std::size_t>(requested_cursor);
    }
  }

  const bool allow_full_match = lua_toboolean(L, 6) != 0;
  const auto &ac = openwow::ui::game::AutoComplete::Get();
  auto completions = ac.GetRecentLuaCompletions(
      text, include_flags, exclude_flags, static_cast<std::size_t>(max_results), cursor_position,
      allow_full_match);

  if (completions.size() < static_cast<std::size_t>(max_results)) {
    const auto append_unique = [&](const std::vector<std::string> &names) {
      for (const auto &name : names) {
        const auto duplicate =
            std::any_of(completions.begin(), completions.end(), [&](const std::string &existing) {
              const auto max_count = std::max(existing.size(), name.size()) + 1;
              return core::SStrCmpUTF8NoCase(existing.c_str(), name.c_str(), max_count) == 0;
            });
        if (!duplicate) {
          completions.push_back(name);
          if (completions.size() == static_cast<std::size_t>(max_results)) {
            return;
          }
        }
      }
    };

    append_unique(ac.GetLuaCompletions(text, include_flags, exclude_flags,
                                       static_cast<std::size_t>(max_results) -
                                           completions.size()));
  }

  const int result_count = openwow::ui::ReserveLuaResultCapacity(
      L, completions.size(), "chat autocomplete results");
  for (const auto &name : completions) {
    lua_pushstring(L, name.c_str());
  }
  return result_count;
}

}

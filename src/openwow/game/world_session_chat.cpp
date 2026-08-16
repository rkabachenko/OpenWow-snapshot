
#include "openwow/game/world_session.h"

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/core/storm_string.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/chat_display.h"
#include "openwow/game/chat_system.h"
#include "openwow/game/knowledge_base.h"
#include "openwow/game/localization.h"
#include "openwow/game/object_types.h"
#include "openwow/game/player_chat_flags.h"
#include "openwow/game/voice_chat.h"
#include "openwow/net/wotlk/protocol/packet_sender.h"
#include "openwow/ui/game/autocomplete.h"
#include "openwow/ui/game/chat_window_state.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <cstring>

namespace openwow::game {

namespace {

void UpsertChannelDisplayState(const std::string &channel_name,
                               std::uint32_t channel_lookup_id,
                               std::uint8_t flags, std::uint32_t member_count,
                               bool is_joined, bool show_in_display);

constexpr std::size_t kChannelListDisplayLineLimit = 0x100;
constexpr std::size_t kSpamFilterPatternBufferSize = 0x200;
constexpr std::uint32_t kSpeechEmoteTalkSlot = 0;
constexpr std::uint32_t kSpeechEmoteQuestionSlot = 1;
constexpr std::uint32_t kSpeechEmoteExclamationSlot = 2;
constexpr std::uint32_t kSpeechEmoteYellSlot = 3;
constexpr std::uint32_t kSpeechEmoteLaughSlot = 4;
constexpr int kPlayerSilencedSystemMessageId = 576;
constexpr int kPlayerUnsilencedSystemMessageId = 577;
constexpr int kPlayerSilencedEchoSystemMessageId = 591;
constexpr int kPlayerUnsilencedEchoSystemMessageId = 592;

constexpr int kChatWrongFactionSystemMessageId = 0xff;

constexpr int kNotInGroupSystemMessageId = 0x50;
constexpr int kNotInRaidSystemMessageId = 0x1bd;

constexpr int kChatRestrictedGenericMessageId = 0x1ef;
constexpr int kChatThrottledMessageId = 0x1f0;
constexpr int kChatUserSquelchedMessageId = 0x231;
constexpr int kChatYellRestrictedMessageId = 0x2a7;

constexpr int kChatPlayerAmbiguousMessageId = 0x1fa;

struct PartySquelchNotificationPacket {
  std::uint32_t squelched = 0;
};

struct PartySquelchEchoPacket {
  std::uint64_t player_guid = 0;
  std::uint32_t squelched = 0;
};

PartySquelchNotificationPacket ParsePartySquelchNotificationPacket(const std::uint8_t *data,
                                                                  const std::size_t len) {
  PacketReader reader(data, len);
  PartySquelchNotificationPacket packet;
  (void)reader.ReadU32(packet.squelched);
  return packet;
}

PartySquelchEchoPacket ParsePartySquelchEchoPacket(const std::uint8_t *data,
                                                   const std::size_t len) {
  PacketReader reader(data, len);
  PartySquelchEchoPacket packet;
  (void)reader.ReadU64(packet.player_guid);
  (void)reader.ReadU32(packet.squelched);
  return packet;
}

bool ShouldRunIncomingChatSpamFilter(const ChatMessage &msg) {
  if (msg.language == Language::kAddon) {
    return false;
  }

  if (!ui::game::CVarSystem::Instance().GetCVarBool("spamFilter")) {
    return false;
  }

  if (msg.type == ChatMsg::kSystem || msg.type == ChatMsg::kRaidWarning) {
    return false;
  }

  return true;
}

[[nodiscard]] bool MatchesLocalizedLaughWord(const std::string &message) {
  if (message.empty()) {
    return false;
  }

  auto &localization = Localization::Get();
  for (std::uint32_t index = 1;; ++index) {
    const std::string key = "LAUGH_WORD" + std::to_string(index);
    const std::string localized = localization.GetString(key, "");
    if (localized.empty()) {
      return false;
    }

    if (openwow::core::SStrCmpNoCase(localized.c_str(), message.c_str(), 0x7FFFFFFFu) == 0) {
      return true;
    }
  }
}

void TryPlayIncomingChatSpeechEmote(WorldSession &session, const ChatMessage &msg) {
  if (msg.language == Language::kAddon) {
    return;
  }

  auto *speaker = session.objects().GetMutableUnit(msg.sender_guid);
  if (speaker == nullptr) {
    return;
  }

  switch (msg.type) {
    case ChatMsg::kSay:
    case ChatMsg::kYell:
    case ChatMsg::kParty:
    case ChatMsg::kPartyLeader:
      break;
    default:
      return;
  }

  if (MatchesLocalizedLaughWord(msg.message)) {
    speaker->Animation().TryPlaySpeechEmoteSlot(kSpeechEmoteLaughSlot);
    return;
  }

  if (msg.type != ChatMsg::kSay && msg.type != ChatMsg::kYell) {
    return;
  }

  std::uint32_t slot = msg.type == ChatMsg::kYell ? kSpeechEmoteYellSlot : kSpeechEmoteTalkSlot;
  if (!msg.message.empty()) {
    const char last_char = msg.message.back();
    if (last_char == '?') {
      slot = kSpeechEmoteQuestionSlot;
    } else if (last_char == '!') {
      slot = kSpeechEmoteExclamationSlot;
    }
  }

  speaker->Animation().TryPlaySpeechEmoteSlot(slot);
}

std::string ReadSpamFilterPattern(const std::uint8_t* data, const std::size_t size,
                                  std::size_t* offset) {
  if (offset == nullptr || *offset > size) {
    return {};
  }

  std::string pattern;
  pattern.reserve(kSpamFilterPatternBufferSize - 1);

  for (std::size_t copied = 0; copied < kSpamFilterPatternBufferSize; ++copied) {
    if (*offset >= size) {
      *offset = size + 1;
      return {};
    }

    const char value = static_cast<char>(data[*offset]);
    ++(*offset);
    if (value == '\0') {
      return pattern;
    }

    pattern.push_back(value);
  }

  *offset = size + 1;
  return {};
}

struct ChatDisplayExtraPayload {
  std::uint32_t primary = 0;
  std::uint32_t secondary = 0;
  std::uint8_t flags = 0xFF;
};

void DisplayChatMessageThroughChatFrame(WorldSession &session, const ChatMessage &msg) {
  ChatDisplayExtraPayload extra_data;
  const void *extra_payload = nullptr;
  const std::string display_tag = ResolvePlayerChatTagToken(msg.chat_tag);
  if (msg.achievement_id != 0) {
    extra_data.secondary = msg.achievement_id;
    extra_payload = &extra_data;
  }

  ChatFrame_DisplayMessage(
      session.objects(), msg.message.c_str(), static_cast<int>(msg.type),
      msg.sender_name.empty() ? nullptr : msg.sender_name.c_str(),
      static_cast<int>(msg.language),
      msg.channel_name.empty() ? nullptr : msg.channel_name.c_str(),
      msg.secondary_name.empty() ? nullptr : msg.secondary_name.c_str(),
      display_tag.empty() ? nullptr : display_tag.c_str(), msg.sender_guid.GetRawValue(), 0,
      msg.receiver_guid.GetRawValue(), 0, msg.is_gm ? 1 : 0, extra_payload);
  if (msg.language == Language::kAddon) {
    return;
  }
  session.chat().AddMessage(msg);
}

bool IsIgnoreBypassChatType(const ChatMsg type) {
  return type == ChatMsg::kWhisperInform || type == ChatMsg::kIgnored ||
         type == ChatMsg::kRestricted;
}

bool ShouldSuppressCommentatorDirectMessage(const WorldSession &session, const ChatMessage &msg,
                                            const bool is_gm) {
  if (is_gm || msg.sender_guid.IsEmpty()) {
    return false;
  }

  if (msg.type != ChatMsg::kWhisper && msg.type != ChatMsg::kBattlenet) {
    return false;
  }

  if (!HasPlayerChatTag(msg.chat_tag, ChatTag::kCom) ||
      HasPlayerChatTag(msg.chat_tag, ChatTag::kGm) ||
      msg.language == Language::kAddon) {
    return false;
  }

  const ObjectGuid local_player_guid = session.objects().GetLocalPlayerGuid();
  if (!local_player_guid.IsEmpty() && msg.sender_guid == local_player_guid) {
    return false;
  }

  if (session.social().HasContact(msg.sender_guid)) {
    return false;
  }

  if (ChatSystem::Get().IsInRecentChatMru(msg.sender_guid.GetRawValue())) {
    return false;
  }

  return true;
}

bool ShouldSuppressIgnoredSenderChat(const WorldSession &session, const ChatMessage &msg,
                                     const bool is_gm) {
  if (is_gm || msg.sender_guid.IsEmpty() || IsIgnoreBypassChatType(msg.type)) {
    return false;
  }

  return session.social().IsIgnored(msg.sender_guid);
}

std::string ResolveChannelActorName(const WorldSession &session, const std::uint64_t guid) {
  if (guid == 0) {
    return {};
  }

  if (const auto *player_name = session.query_cache().GetPlayerName(guid)) {
    return player_name->name;
  }

  return session.objects().GetPlayerName(ObjectGuid(guid));
}

std::string FormatKbServerMessage(const WorldSession& session,
                                  const ChatServerMessage& message) {
  if (const auto* dbc = session.GetDbcLoader(); dbc != nullptr) {
    if (const auto* entry =
            dbc->server_messages().LookupEntry(message.message_type);
        entry != nullptr && !entry->text.empty()) {
      if (message.message.empty()) {
        return std::string(entry->text);
      }

      const std::string format(entry->text);
      char buffer[1024]{};
      core::SStrPrintf(buffer, sizeof(buffer), format.c_str(),
                       message.message.c_str());
      return buffer;
    }
  }

  char buffer[1024]{};
  core::SStrPrintf(buffer, sizeof(buffer), "[%d]: %s",
                   static_cast<int>(message.message_type),
                   message.message.c_str());
  return buffer;
}

std::uint32_t NormalizeDefenseAreaId(const data::dbc::DbcLoader *dbc, std::uint32_t area_id) {
  if (dbc == nullptr) {
    return area_id;
  }

  const auto &area_table = dbc->area_table();
  std::uint32_t current = area_id;
  for (std::size_t depth = 0; depth < 32; ++depth) {
    const auto *area = area_table.LookupEntry(current);
    if (area == nullptr || area->parent_area == 0) {
      return current;
    }
    current = area->parent_area;
  }

  return current;
}

const data::dbc::ChatChannelsEntry *
LookupDefenseChatChannelDefinition(const data::dbc::DbcLoader *dbc,
                                   const std::string &channel_name) {
  if (dbc == nullptr || channel_name.empty()) {
    return nullptr;
  }

  if (const auto *exact = dbc->chat_channels().LookupByNameCaseInsensitive(channel_name);
      exact != nullptr) {
    return exact;
  }

  for (const auto &entry : dbc->chat_channels().entries()) {
    if (entry.pattern.empty()) {
      continue;
    }

    const std::string pattern(entry.pattern);
    const std::size_t placeholder = pattern.find("%s");
    if (placeholder == std::string::npos) {
      continue;
    }

    const std::string prefix = pattern.substr(0, placeholder);
    const std::string suffix = pattern.substr(placeholder + 2);
    if (channel_name.size() < prefix.size() + suffix.size()) {
      continue;
    }
    if (openwow::core::SStrCmpUTF8NoCase(channel_name.c_str(), prefix.c_str(),
                                         static_cast<std::uint32_t>(prefix.size())) != 0) {
      continue;
    }
    if (!suffix.empty()) {
      const char *const channel_suffix =
          channel_name.c_str() + (channel_name.size() - suffix.size());
      if (openwow::core::SStrCmpUTF8NoCase(channel_suffix, suffix.c_str(),
                                           static_cast<std::uint32_t>(suffix.size())) != 0) {
        continue;
      }
    }

    return &entry;
  }

  return nullptr;
}

std::vector<std::string> CollectDefenseMessageChannels(const WorldSession &session,
                                                       const std::uint32_t packet_area_id) {
  const auto *dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return {};
  }

  const std::uint32_t normalized_area_id = NormalizeDefenseAreaId(dbc, packet_area_id);
  const std::uint32_t current_zone_id = NormalizeDefenseAreaId(dbc, session.objects().GetZoneId());

  std::vector<std::string> channels;
  auto &chat_system = ChatSystem::Get();
  for (std::size_t index = 0; index < chat_system.GetNumChannels(); ++index) {
    const auto *channel = chat_system.GetChannel(index);
    if (channel == nullptr || !channel->is_joined || channel->lua_hidden) {
      continue;
    }

    const auto *definition = LookupDefenseChatChannelDefinition(dbc, channel->name);
    if (definition == nullptr) {
      continue;
    }
    if ((definition->flags & 0x10000u) == 0) {
      continue;
    }
    if ((definition->flags & 0x2u) != 0 && normalized_area_id != current_zone_id) {
      continue;
    }

    channels.push_back(channel->DisplayNameOrName());
  }

  return channels;
}

std::string ResolveCachedChannelRosterMemberName(const WorldSession &session,
                                                 const std::uint64_t raw_guid) {
  if (const auto *cached_name = session.query_cache().GetPlayerName(raw_guid)) {
    if (!cached_name->name.empty()) {
      if (!cached_name->realm_name.empty()) {
        return cached_name->name + "-" + cached_name->realm_name;
      }
      return cached_name->name;
    }
  }

  return {};
}

[[nodiscard]] std::uint32_t FindJoinedDisplayChannelSlot(const std::string &channel_name) {
  auto &chat_system = ChatSystem::Get();
  const auto *target = chat_system.GetChannelByName(channel_name);
  if (target == nullptr) {
    return 0;
  }
  const auto target_lookup_id = target->lookup_id;
  const auto count = chat_system.GetNumDisplayChannels();
  for (std::size_t index = 0; index < count; ++index) {
    const auto resolved = chat_system.ResolveDisplayChannel(index);
    if (!resolved.has_value() ||
        resolved->kind != DisplayChannelKind::kJoinedChannel ||
        !resolved->channel.has_value()) {
      continue;
    }
    if (resolved->channel->lookup_id == target_lookup_id) {
      return static_cast<std::uint32_t>(index) + 1u;
    }
  }
  return 0;
}

void FireChannelUserlistEvents(const std::string &channel_name,
                               const bool flags_changed,
                               const std::uint32_t member_count) {
  const auto slot = FindJoinedDisplayChannelSlot(channel_name);
  if (slot == 0) {
    return;
  }
  auto &dispatch = ui::game::ScriptEventDispatch::Get();
  if (flags_changed) {
    dispatch.FireEventArgs(ui::game::events::CHANNEL_FLAGS_UPDATED,
                           {static_cast<int>(slot)});
  }
  dispatch.FireEventArgs(ui::game::events::CHANNEL_COUNT_UPDATE,
                         {static_cast<int>(slot), static_cast<int>(member_count)});
}

bool UpdateJoinedChannelDisplayState(const std::string &channel_name, const std::uint8_t flags,
                                     const std::uint32_t member_count) {
  auto &chat_system = ChatSystem::Get();
  const auto *existing = chat_system.GetChannelByName(channel_name);
  const bool flags_changed = existing != nullptr && existing->flags != flags;
  const std::uint32_t channel_lookup_id = existing != nullptr ? existing->lookup_id : 0;
  const bool is_joined = existing != nullptr ? existing->is_joined : true;
  const bool show_in_display = existing != nullptr ? existing->show_in_display : true;
  UpsertChannelDisplayState(channel_name, channel_lookup_id, flags, member_count, is_joined,
                            show_in_display);
  return flags_changed;
}

void FireChannelRosterUpdateForChannel(const std::string &channel_name,
                                       const std::uint32_t member_count) {
  const auto slot = FindJoinedDisplayChannelSlot(channel_name);
  if (slot == 0) {
    return;
  }
  ui::game::ScriptEventDispatch::Get().FireChannelRosterUpdate(
      static_cast<int>(slot), static_cast<int>(member_count));
}

void FireWatchedChannelRosterEvents(const std::string &channel_name, const bool fire_ui_update) {
  if (fire_ui_update) {
    ui::game::ScriptEventDispatch::Get().FireChannelUiUpdate();
  }

  FireChannelRosterUpdateForChannel(
      channel_name,
      static_cast<std::uint32_t>(ChatSystem::Get().GetWatchedChannelRosterSize()));
}

void UpsertChannelDisplayState(const std::string &channel_name,
                               const std::uint32_t channel_lookup_id,
                               const std::uint8_t flags, const std::uint32_t member_count,
                               const bool is_joined, const bool show_in_display) {
  auto &chat_system = ChatSystem::Get();
  const auto *existing =
      channel_lookup_id != 0 ? chat_system.GetChannelByLookupId(channel_lookup_id) : nullptr;
  if (existing == nullptr) {
    existing = chat_system.GetChannelByName(channel_name);
  }
  if (existing == nullptr) {
    return;
  }

  ChatChannel channel = *existing;

  channel.name = channel_name;
  if (channel_lookup_id != 0) {
    channel.lookup_id = channel_lookup_id;
  }
  channel.flags = flags;
  channel.member_count = member_count;
  channel.is_joined = is_joined;
  channel.show_in_display = show_in_display;
  channel.voice_enabled = (flags & 0x80u) != 0;
  if (is_joined) {
    channel.lua_hidden = false;
  }
  chat_system.UpdateChannel(existing->name, channel);
}

const ChatChannel *ResolveChannelNotifyChannel(const ChannelNotifyPacket &notify) {
  auto &chat_system = ChatSystem::Get();
  if (notify.channel_id != 0) {
    if (const auto *channel = chat_system.GetChannelByLookupId(notify.channel_id)) {
      return channel;
    }
  }

  return chat_system.GetChannelByName(notify.channel_name);
}

void RemoveChannelDisplayState(const std::string &channel_name) {
  ui::game::ChatWindowState::Get().RemoveChannelFromAllWindows(channel_name);
  ChatSystem::Get().LeaveChannel(channel_name);
}

void SetChannelVoiceState(const std::string &channel_name, const bool voice_enabled) {
  auto &chat_system = ChatSystem::Get();
  const auto *existing = chat_system.GetChannelByName(channel_name);
  if (!existing) {
    return;
  }

  ChatChannel updated = *existing;
  updated.voice_enabled = voice_enabled;
  if (voice_enabled) {
    updated.flags |= 0x80u;
  } else {
    updated.flags &= static_cast<std::uint8_t>(~0x80u);
  }
  chat_system.UpdateChannel(channel_name, updated);
}

void ClearRemovedChannelVoiceSelection(const ChatChannel &channel,
                                       openwow::audio::SoundRuntime &sound_runtime) {
  auto &sound_interface = sound_runtime;
  if (!sound_interface.HasBackgroundSoundSelection()) {
    return;
  }

  auto &chat_system = ChatSystem::Get();
  const auto active_slot = static_cast<std::size_t>(sound_interface.GetBackgroundSoundState());
  const auto resolved = chat_system.ResolveDisplayChannel(active_slot);
  if (!resolved.has_value() || resolved->kind != DisplayChannelKind::kJoinedChannel ||
      !resolved->channel.has_value()) {
    return;
  }

  if (openwow::core::SStrCmpUTF8NoCase(resolved->channel->name.c_str(), channel.name.c_str(),
                                       0x7FFFFFFF) != 0) {
    return;
  }

  std::vector<std::string> args{std::to_string(active_slot + 1u)};
  if (channel.voice_enabled) {
    args.emplace_back("1");
  }
  ui::game::ScriptEventDispatch::Get().FireGlobalEventWithArgs(
      ui::game::events::CHANNEL_VOICE_UPDATE, args);

  chat_system.ClearSelectedDisplayChannel();
  sound_interface.ClearBackgroundSoundState();
}

bool IsIgnoredChannelInvite(const WorldSession &session, const std::uint64_t inviter_guid) {
  return inviter_guid != 0 && session.social().IsIgnored(ObjectGuid(inviter_guid));
}

const char *GetChannelListMemberPrefix(const std::uint8_t member_flags) {
  if ((member_flags & 0x01u) != 0) {
    return "*";
  }
  if ((member_flags & 0x02u) != 0) {
    return "@";
  }
  if ((member_flags & 0x04u) == 0) {
    return "#";
  }
  return "";
}

}

void WorldSession::DispatchIncomingChatMessage(ChatMessage msg) {
  TryPlayIncomingChatSpeechEmote(*this, msg);
  DisplayChatMessageThroughChatFrame(*this, msg);
}

void WorldSession::EmitChannelMessage(const ChatMsg type, const std::string &message,
                                      const std::string &channel_name,
                                      const std::string &sender_name,
                                      const std::uint64_t sender_guid,
                                      const std::uint64_t receiver_guid) {

  if (message.empty() && type != ChatMsg::kChannelJoin && type != ChatMsg::kChannelLeave) {
    return;
  }

  ChatMessage msg;
  msg.type = type;
  msg.language = Language::kUniversal;
  msg.sender_guid = ObjectGuid(sender_guid);
  msg.receiver_guid = ObjectGuid(receiver_guid);
  msg.sender_name = sender_name;
  msg.channel_name = channel_name;
  msg.message = message;
  QueueOrDispatchChatMessage(std::move(msg), false);
}

std::string WorldSession::ResolveImmediateChatParticipantName(const ObjectGuid &guid) const {
  if (guid.IsEmpty()) {
    return {};
  }

  if (const auto *object = objects().Get(guid)) {
    const std::string object_name = object->GetName();
    if (!object_name.empty()) {
      return object_name;
    }
  }

  if (guid.IsPlayer()) {
    if (const auto *cached_name = query_cache_.GetPlayerName(guid.GetRawValue())) {
      return cached_name->name;
    }

    return objects().GetPlayerName(guid);
  }

  if (guid.IsCreatureOrPetOrVehicle() && guid.GetEntry() != 0) {
    if (const auto *creature_template = query_cache_.GetCreatureTemplate(guid.GetEntry())) {
      return creature_template->name;
    }
  }

  return {};
}

std::uint8_t WorldSession::ApplyLocalChannelRosterMuteFlag(const std::string &channel_name,
                                                           const std::uint64_t raw_guid,
                                                           const std::uint8_t raw_flags) const {
  const ObjectGuid guid(raw_guid);
  if (guid.IsEmpty()) {
    return raw_flags;
  }

  std::uint8_t flags = raw_flags;
  if (social_.IsIgnored(guid) || social_.IsMuted(guid) || VoiceChat::Get().IsPlayerMuted(guid) ||
      VoiceChat::Get().IsSessionPlayerMuted(channel_name, guid)) {
    flags |= 0x20u;
  }

  return flags;
}

std::uint8_t WorldSession::DecorateWatchedChannelRosterVoiceFlags(
    const std::string& channel_name, const std::uint64_t raw_guid, const std::uint8_t base_flags,
    const bool include_silenced_bit) const {
  return VoiceChat::Get().DecorateChannelRosterMemberFlags(
      VoiceChatChannelType::kCustom, channel_name, ObjectGuid(raw_guid), base_flags,
      include_silenced_bit);
}

void WorldSession::RefreshWatchedChannelRosterLocalMuteFlags() {
  auto &chat_system = ChatSystem::Get();
  const std::string channel_name = chat_system.GetWatchedJoinedChannelName();
  if (channel_name.empty() || !chat_system.IsWatchingJoinedChannel(channel_name)) {
    return;
  }

  bool changed = false;
  const std::size_t roster_size = chat_system.GetWatchedChannelRosterSize();
  for (std::size_t index = 0; index < roster_size; ++index) {
    const auto member = chat_system.GetWatchedChannelRosterMember(index);
    if (!member.has_value()) {
      continue;
    }

    const std::uint8_t display_flags =
        ApplyLocalChannelRosterMuteFlag(channel_name, member->guid, member->raw_flags);
    if (display_flags == member->flags) {
      continue;
    }

    chat_system.UpdateWatchedChannelRosterMemberFlags(channel_name, member->guid, member->raw_flags,
                                                      display_flags);
    changed = true;
  }

  if (changed) {
    FireWatchedChannelRosterEvents(channel_name, true);
  }
}

void WorldSession::RefreshSelectedJoinedChannelVoiceRoster(const std::string& channel_name) {
  auto& chat_system = ChatSystem::Get();
  if (!chat_system.MatchesSelectedDisplayChannel(DisplayChannelKind::kJoinedChannel, channel_name) ||
      !chat_system.IsWatchingJoinedChannel(channel_name)) {
    return;
  }

  const std::size_t roster_size = chat_system.GetWatchedChannelRosterSize();
  for (std::size_t index = 0; index < roster_size; ++index) {
    const auto member = chat_system.GetWatchedChannelRosterMember(index);
    if (!member.has_value()) {
      continue;
    }

    const std::uint8_t raw_flags = DecorateWatchedChannelRosterVoiceFlags(
        channel_name, member->guid, member->raw_flags, true);
    const std::uint8_t display_flags =
        ApplyLocalChannelRosterMuteFlag(channel_name, member->guid, raw_flags);
    if (raw_flags == member->raw_flags && display_flags == member->flags) {
      continue;
    }

    chat_system.UpdateWatchedChannelRosterMemberFlags(channel_name, member->guid, raw_flags,
                                                      display_flags);
  }
}

bool WorldSession::TryResolveChatDisplayMessage(ChatMessage &msg,
                                                const bool request_resolution) {
  bool resolved = true;
  const auto resolve_participant = [this, request_resolution, &resolved](
                                       const ObjectGuid &guid,
                                       std::string &name) {
    if (!name.empty() || guid.IsEmpty()) {
      return;
    }

    name = ResolveImmediateChatParticipantName(guid);
    if (!name.empty()) {
      return;
    }

    if (!request_resolution) {
      resolved = false;
      return;
    }

    if (guid.IsPlayer()) {
      if (pending_chat_name_queries_.insert(guid.GetRawValue()).second) {
        (void)query_cache_.RequestNameQuery(guid.GetRawValue());
      }
      resolved = false;
      return;
    }

    if (guid.IsCreatureOrPetOrVehicle() && guid.GetEntry() != 0) {
      (void)query_cache_.GetOrRequestCreatureTemplate(guid.GetEntry(),
                                                      guid.GetRawValue());
      resolved = false;
    }
  };

  resolve_participant(msg.sender_guid, msg.sender_name);
  resolve_participant(msg.receiver_guid, msg.secondary_name);
  return resolved;
}

void WorldSession::QueueOrDispatchChatMessage(ChatMessage msg,
                                              const bool dispatch_as_incoming) {
  if (!TryResolveChatDisplayMessage(msg, true)) {
    pending_chat_messages_.push_back(
        PendingIncomingChatMessage{std::move(msg), dispatch_as_incoming});
    return;
  }

  if (incoming_chat_delivery_suspended_) {
    pending_chat_messages_.push_back(
        PendingIncomingChatMessage{std::move(msg), dispatch_as_incoming});
    return;
  }

  if (dispatch_as_incoming) {
    DispatchIncomingChatMessage(std::move(msg));
    return;
  }

  DisplayChatMessageThroughChatFrame(*this, msg);
}

void WorldSession::SuspendIncomingChatDelivery() {
  incoming_chat_delivery_suspended_ = true;
}

void WorldSession::ResumeIncomingChatDelivery() {
  if (!incoming_chat_delivery_suspended_) {
    return;
  }

  incoming_chat_delivery_suspended_ = false;
  FlushResolvedPendingChatDelivery();
}

void WorldSession::FlushResolvedPendingChatDelivery() {
  if (incoming_chat_delivery_suspended_) {
    return;
  }

  auto chat_it = pending_chat_messages_.begin();
  while (chat_it != pending_chat_messages_.end()) {
    if (!TryResolveChatDisplayMessage(chat_it->message, false)) {
      ++chat_it;
      continue;
    }

    if (chat_it->dispatch_as_incoming) {
      DispatchIncomingChatMessage(std::move(chat_it->message));
    } else {
      DisplayChatMessageThroughChatFrame(*this, chat_it->message);
    }
    chat_it = pending_chat_messages_.erase(chat_it);
  }

  auto text_emote_it = pending_text_emotes_.begin();
  while (text_emote_it != pending_text_emotes_.end()) {
    if (!TryResolveIncomingTextEmote(*text_emote_it, false)) {
      ++text_emote_it;
      continue;
    }

    DispatchIncomingTextEmote(*text_emote_it);
    text_emote_it = pending_text_emotes_.erase(text_emote_it);
  }

  auto channel_list_it = pending_channel_lists_.begin();
  while (channel_list_it != pending_channel_lists_.end()) {
    if (!TryDisplayPendingChannelList(*channel_list_it)) {
      ++channel_list_it;
      continue;
    }

    channel_list_it = pending_channel_lists_.erase(channel_list_it);
  }
}

void WorldSession::RetryPendingChatMessagesForGuid(const ObjectGuid &guid,
                                                   const bool drop_if_unresolved) {
  auto it = pending_chat_messages_.begin();
  while (it != pending_chat_messages_.end()) {
    if (it->message.sender_guid != guid && it->message.receiver_guid != guid) {
      ++it;
      continue;
    }

    if (TryResolveChatDisplayMessage(it->message, false)) {
      if (incoming_chat_delivery_suspended_) {
        ++it;
        continue;
      }

      if (it->dispatch_as_incoming) {
        DispatchIncomingChatMessage(std::move(it->message));
      } else {
        DisplayChatMessageThroughChatFrame(*this, it->message);
      }
      it = pending_chat_messages_.erase(it);
      continue;
    }

    if (drop_if_unresolved) {
      it = pending_chat_messages_.erase(it);
      continue;
    }

    ++it;
  }
}

void WorldSession::RetryPendingTextEmotesForGuid(const ObjectGuid &guid,
                                                 const bool drop_if_unresolved) {
  auto it = pending_text_emotes_.begin();
  while (it != pending_text_emotes_.end()) {
    if (it->source_guid != guid) {
      ++it;
      continue;
    }

    if (TryResolveIncomingTextEmote(*it, false)) {
      if (incoming_chat_delivery_suspended_) {
        ++it;
        continue;
      }

      DispatchIncomingTextEmote(*it);
      it = pending_text_emotes_.erase(it);
      continue;
    }

    if (drop_if_unresolved) {
      it = pending_text_emotes_.erase(it);
      continue;
    }

    ++it;
  }
}

void WorldSession::RetryPendingChatMessagesForCreatureEntry(const std::uint32_t entry,
                                                            const bool drop_if_unresolved) {
  auto it = pending_chat_messages_.begin();
  while (it != pending_chat_messages_.end()) {
    const ObjectGuid sender_guid = it->message.sender_guid;
    const ObjectGuid receiver_guid = it->message.receiver_guid;
    const bool sender_matches =
        sender_guid.IsCreatureOrPetOrVehicle() && sender_guid.GetEntry() == entry;
    const bool receiver_matches =
        receiver_guid.IsCreatureOrPetOrVehicle() && receiver_guid.GetEntry() == entry;
    if (!sender_matches && !receiver_matches) {
      ++it;
      continue;
    }

    if (TryResolveChatDisplayMessage(it->message, false)) {
      if (incoming_chat_delivery_suspended_) {
        ++it;
        continue;
      }

      if (it->dispatch_as_incoming) {
        DispatchIncomingChatMessage(std::move(it->message));
      } else {
        DisplayChatMessageThroughChatFrame(*this, it->message);
      }
      it = pending_chat_messages_.erase(it);
      continue;
    }

    if (drop_if_unresolved) {
      it = pending_chat_messages_.erase(it);
      continue;
    }

    ++it;
  }
}

void WorldSession::RetryPendingTextEmotesForCreatureEntry(const std::uint32_t entry,
                                                          const bool drop_if_unresolved) {
  auto it = pending_text_emotes_.begin();
  while (it != pending_text_emotes_.end()) {
    const ObjectGuid guid = it->source_guid;
    if (!guid.IsCreatureOrPetOrVehicle() || guid.GetEntry() != entry) {
      ++it;
      continue;
    }

    if (TryResolveIncomingTextEmote(*it, false)) {
      if (incoming_chat_delivery_suspended_) {
        ++it;
        continue;
      }

      DispatchIncomingTextEmote(*it);
      it = pending_text_emotes_.erase(it);
      continue;
    }

    if (drop_if_unresolved) {
      it = pending_text_emotes_.erase(it);
      continue;
    }

    ++it;
  }
}

std::string
WorldSession::ResolveImmediateChannelListMemberName(const std::uint64_t raw_guid) const {
  return ResolveCachedChannelRosterMemberName(*this, raw_guid);
}

bool WorldSession::TryDisplayPendingChannelList(const PendingChannelListDisplay &pending) {
  if (incoming_chat_delivery_suspended_) {
    return false;
  }

  std::string current_line;
  std::size_t entries_in_line = 0;

  for (const PendingChannelListMember &member : pending.members) {
    const std::string resolved_name = ResolveImmediateChannelListMemberName(member.guid);
    if (resolved_name.empty()) {
      return false;
    }

    std::string entry_text = GetChannelListMemberPrefix(member.member_flags);
    entry_text += resolved_name;

    const std::string rendered_entry = current_line.empty() ? entry_text : ", " + entry_text;
    if (current_line.size() + rendered_entry.size() < kChannelListDisplayLineLimit) {
      current_line += rendered_entry;
      ++entries_in_line;
      continue;
    }

    EmitChannelMessage(ChatMsg::kChannelList, current_line, pending.channel_name);
    current_line = entry_text;
    entries_in_line = 1;
  }

  if (entries_in_line == 0) {
    return true;
  }

  EmitChannelMessage(ChatMsg::kChannelList, current_line, pending.channel_name);
  return true;
}

void WorldSession::RetryPendingChannelListsForGuid(const ObjectGuid &guid,
                                                   const bool drop_if_unresolved) {
  if (guid.IsEmpty()) {
    return;
  }

  const std::uint64_t raw_guid = guid.GetRawValue();
  auto it = pending_channel_lists_.begin();
  while (it != pending_channel_lists_.end()) {
    if (drop_if_unresolved) {
      auto &members = it->members;
      members.erase(std::remove_if(members.begin(), members.end(),
                                   [raw_guid](const PendingChannelListMember &member) {
                                     return member.guid == raw_guid;
                                   }),
                    members.end());
    }

    if (TryDisplayPendingChannelList(*it)) {
      it = pending_channel_lists_.erase(it);
      continue;
    }

    ++it;
  }
}

void WorldSession::RetryPendingWatchedChannelRosterForGuid(const ObjectGuid &guid,
                                                           const bool drop_if_unresolved) {
  if (guid.IsEmpty()) {
    return;
  }

  auto &chat_system = ChatSystem::Get();
  const std::string watched_channel = chat_system.GetWatchedJoinedChannelName();
  if (watched_channel.empty()) {
    return;
  }

  if (!chat_system.ResolveWatchedChannelRosterMemberName(
          guid.GetRawValue(), ResolveCachedChannelRosterMemberName(*this, guid.GetRawValue()),
          drop_if_unresolved)) {
    return;
  }

  if (chat_system.GetWatchedChannelRosterPendingQueries() == 0) {
    ui::game::ScriptEventDispatch::Get().FireChannelUiUpdate();
  }
}

void WorldSession::ClearPendingChatMessages() {
  pending_chat_messages_.clear();
  pending_text_emotes_.clear();
  pending_channel_lists_.clear();
  pending_chat_name_queries_.clear();
  incoming_chat_delivery_suspended_ = false;
}

void WorldSession::TryDisplayPendingChannelInvite(const std::uint64_t inviter_guid) {
  if (!pending_channel_invite_ || pending_channel_invite_->inviter_guid != inviter_guid) {
    return;
  }

  const std::string inviter_name = ResolveChannelActorName(*this, inviter_guid);
  if (inviter_name.empty()) {
    return;
  }

  EmitChannelMessage(ChatMsg::kChannelNoticeUser, "INVITE",
                     pending_channel_invite_->channel_name, inviter_name, inviter_guid);
  ui::game::ScriptEventDispatch::Get().FireGlobalEventWithArgs(
      ui::game::events::CHANNEL_INVITE_REQUEST,
      {pending_channel_invite_->channel_name, inviter_name});
  pending_channel_invite_.reset();
}

void WorldSession::HandleChatMessage(const net::wotlk::WorldPacket &pkt, bool is_gm) {
  ChatMessage msg;
  if (ChatManager::ParseChatMessage(pkt.payload.data(), pkt.payload.size(), is_gm, msg)) {
    if (!msg.sender_guid.IsEmpty()) {
      if (msg.type == ChatMsg::kWhisper || msg.type == ChatMsg::kBattlenet) {
        ChatSystem::Get().RecordWhisperGuid(msg.sender_guid.GetRawValue());
      } else if (msg.type == ChatMsg::kWhisperInform) {
        ChatSystem::Get().RecordRecentChatGuid(msg.sender_guid.GetRawValue());
      }
    }

    std::string filtered_message = msg.message;
    if (!is_gm && ShouldRunIncomingChatSpamFilter(msg) &&
        ChatFrame_CheckProfanityFilter(filtered_message, true)) {
      if (msg.type == ChatMsg::kWhisper && !msg.sender_guid.IsEmpty()) {
        interaction_.SendChatFiltered(msg.sender_guid.GetRawValue());
      }
      return;
    }

    msg.message = std::move(filtered_message);
    if (ShouldSuppressCommentatorDirectMessage(*this, msg, is_gm)) {
      interaction_.SendChatIgnored(msg.sender_guid.GetRawValue(), true);
      return;
    }

    if (ShouldSuppressIgnoredSenderChat(*this, msg, is_gm)) {
      if (msg.type == ChatMsg::kWhisper && msg.language != Language::kAddon) {
        interaction_.SendChatIgnored(msg.sender_guid.GetRawValue(), false);
      }
      return;
    }

    QueueOrDispatchChatMessage(std::move(msg), true);
  }
}

void WorldSession::HandleChannelNotify(const net::wotlk::WorldPacket &pkt) {
  ChannelNotifyPacket notify;
  if (!ChatManager::ParseChannelNotify(pkt.payload.data(), pkt.payload.size(), notify)) {
    return;
  }

  const ChatChannel *existing_channel = ResolveChannelNotifyChannel(notify);

  const bool has_visible_slot =
      ChatSystem::Get().GetVisibleChannelSlotByName(notify.channel_name) != 0;
  const auto clear_channel_notify_record = [&]() {
    const std::string tracked_name =
        existing_channel != nullptr ? existing_channel->name : notify.channel_name;
    chat_.OnChannelLeft(tracked_name);
    RemoveChannelDisplayState(tracked_name);
  };
  bool fire_channel_ui_update = false;

  switch (notify.type) {
  case ChannelNotify::kJoined: {
    if (!has_visible_slot) {
      return;
    }

    const std::string actor_name = ResolveChannelActorName(*this, notify.actor_guid);
    if (!actor_name.empty()) {
      EmitChannelMessage(ChatMsg::kChannelJoin, "", notify.channel_name, actor_name,
                         notify.actor_guid);
    }
    return;
  }

  case ChannelNotify::kLeft: {
    if (!has_visible_slot) {
      return;
    }

    const std::string actor_name = ResolveChannelActorName(*this, notify.actor_guid);
    if (!actor_name.empty()) {
      EmitChannelMessage(ChatMsg::kChannelLeave, "", notify.channel_name, actor_name,
                         notify.actor_guid);
    }
    return;
  }

  case ChannelNotify::kYouJoined: {
    const bool channel_was_visible = existing_channel != nullptr && !existing_channel->lua_hidden;
    chat_.OnChannelJoined(notify.channel_name, notify.channel_id, notify.channel_flags);
    UpsertChannelDisplayState(notify.channel_name, notify.channel_id, notify.channel_flags,
                              notify.member_count, true, true);
    EmitChannelMessage(ChatMsg::kChannelNotice,
                       channel_was_visible ? "YOU_CHANGED" : "YOU_JOINED",
                       notify.channel_name);
    if (!notify.text.empty()) {
      EmitChannelMessage(ChatMsg::kChannelNotice, notify.text, notify.channel_name);
    }
    fire_channel_ui_update = true;
    break;
  }

  case ChannelNotify::kYouLeft: {
    if (existing_channel != nullptr) {
      ClearRemovedChannelVoiceSelection(*existing_channel, sound_runtime_);
    }
    const bool suspended = notify.status_flag != 0 && existing_channel != nullptr;
    if (suspended) {
      ChatChannel channel = *existing_channel;
      channel.lua_hidden = true;
      ChatSystem::Get().UpdateChannel(existing_channel->name, channel);
      if (ChatSystem::Get().IsWatchingJoinedChannel(existing_channel->name)) {
        ChatSystem::Get().ClearWatchedChannelSelection();
      }
      EmitChannelMessage(ChatMsg::kChannelNotice, "SUSPENDED", notify.channel_name);
    } else {
      EmitChannelMessage(ChatMsg::kChannelNotice, "YOU_LEFT", notify.channel_name);
      chat_.OnChannelLeft(notify.channel_name);
      RemoveChannelDisplayState(notify.channel_name);
    }
    fire_channel_ui_update = true;
    break;
  }

  case ChannelNotify::kWrongPassword:
    EmitChannelMessage(ChatMsg::kChannelNotice, "WRONG_PASSWORD", notify.channel_name);

    ui::game::ScriptEventDispatch::Get().FireEventArgs(
        ui::game::events::CHANNEL_PASSWORD_REQUEST, {notify.channel_name});
    clear_channel_notify_record();
    fire_channel_ui_update = true;
    break;

  case ChannelNotify::kNotMember:
    EmitChannelMessage(ChatMsg::kChannelNotice, "NOT_MEMBER", notify.channel_name);
    break;

  case ChannelNotify::kNotModerator:
    EmitChannelMessage(ChatMsg::kChannelNotice, "NOT_MODERATOR", notify.channel_name);
    break;

  case ChannelNotify::kPasswordChanged:
    EmitChannelMessage(ChatMsg::kChannelNoticeUser, "PASSWORD_CHANGED", notify.channel_name,
                       {}, notify.actor_guid);
    break;

  case ChannelNotify::kOwnerChanged:
    EmitChannelMessage(ChatMsg::kChannelNoticeUser, "OWNER_CHANGED", notify.channel_name, {},
                       notify.actor_guid);
    break;

  case ChannelNotify::kPlayerNotFound:
    EmitChannelMessage(ChatMsg::kChannelNoticeUser, "PLAYER_NOT_FOUND", notify.channel_name,
                       notify.text);
    break;

  case ChannelNotify::kNotOwner:
    EmitChannelMessage(ChatMsg::kChannelNotice, "NOT_OWNER", notify.channel_name);
    break;

  case ChannelNotify::kChannelOwner:
    EmitChannelMessage(ChatMsg::kChannelNoticeUser, "CHANNEL_OWNER", notify.channel_name,
                       notify.text);
    break;

  case ChannelNotify::kModeChange: {
    const std::string actor_name = ResolveChannelActorName(*this, notify.actor_guid);
    if (notify.actor_guid == objects().GetLocalPlayerGuid().GetRawValue()) {
      ChatSystem::Get().SetSelectedJoinedChannelSelfFlags(notify.channel_name,
                                                          notify.new_member_flags);
    }
    if ((notify.old_member_flags & 0x02u) == 0 && (notify.new_member_flags & 0x02u) != 0) {
      EmitChannelMessage(ChatMsg::kChannelNoticeUser, "SET_MODERATOR", notify.channel_name,
                         actor_name, notify.actor_guid);
    } else if ((notify.old_member_flags & 0x02u) != 0 && (notify.new_member_flags & 0x02u) == 0) {
      EmitChannelMessage(ChatMsg::kChannelNoticeUser, "UNSET_MODERATOR", notify.channel_name,
                         actor_name, notify.actor_guid);
    }

    if ((notify.old_member_flags & 0x04u) == 0 && (notify.new_member_flags & 0x04u) != 0) {
      EmitChannelMessage(ChatMsg::kChannelNoticeUser, "SET_VOICE", notify.channel_name,
                         actor_name, notify.actor_guid);
    } else if ((notify.old_member_flags & 0x04u) != 0 && (notify.new_member_flags & 0x04u) == 0) {
      EmitChannelMessage(ChatMsg::kChannelNoticeUser, "UNSET_VOICE", notify.channel_name,
                         actor_name, notify.actor_guid);
    }

    if ((notify.old_member_flags & 0x10u) == 0 && (notify.new_member_flags & 0x10u) != 0) {
      EmitChannelMessage(ChatMsg::kChannelNoticeUser, "SET_SPEAK", notify.channel_name,
                         actor_name, notify.actor_guid);
    } else if ((notify.old_member_flags & 0x10u) != 0 && (notify.new_member_flags & 0x10u) == 0) {
      EmitChannelMessage(ChatMsg::kChannelNoticeUser, "UNSET_SPEAK", notify.channel_name,
                         actor_name, notify.actor_guid);
    }
    break;
  }

  case ChannelNotify::kAnnouncementsOn:
    EmitChannelMessage(ChatMsg::kChannelNoticeUser, "ANNOUNCEMENTS_ON", notify.channel_name,
                       {}, notify.actor_guid);
    break;

  case ChannelNotify::kAnnouncementsOff:
    EmitChannelMessage(ChatMsg::kChannelNoticeUser, "ANNOUNCEMENTS_OFF", notify.channel_name,
                       {}, notify.actor_guid);
    break;

  case ChannelNotify::kModerationOn:
    EmitChannelMessage(ChatMsg::kChannelNoticeUser, "MODERATION_ON", notify.channel_name, {},
                       notify.actor_guid);
    break;

  case ChannelNotify::kModerationOff:
    EmitChannelMessage(ChatMsg::kChannelNoticeUser, "MODERATION_OFF", notify.channel_name,
                       {}, notify.actor_guid);
    break;

  case ChannelNotify::kMuted:
    EmitChannelMessage(ChatMsg::kChannelNotice, "MUTED", notify.channel_name);
    break;

  case ChannelNotify::kPlayerKicked:
    EmitChannelMessage(ChatMsg::kChannelNoticeUser, "PLAYER_KICKED", notify.channel_name, {},
                       notify.actor_guid, notify.secondary_guid);
    break;

  case ChannelNotify::kBanned:
    EmitChannelMessage(ChatMsg::kChannelNotice, "BANNED", notify.channel_name);
    clear_channel_notify_record();
    fire_channel_ui_update = true;
    break;

  case ChannelNotify::kPlayerBanned:
    EmitChannelMessage(ChatMsg::kChannelNoticeUser, "PLAYER_BANNED", notify.channel_name, {},
                       notify.actor_guid, notify.secondary_guid);
    break;

  case ChannelNotify::kPlayerUnbanned:
    EmitChannelMessage(ChatMsg::kChannelNoticeUser, "PLAYER_UNBANNED", notify.channel_name,
                       {}, notify.actor_guid, notify.secondary_guid);
    break;

  case ChannelNotify::kPlayerNotBanned:
    EmitChannelMessage(ChatMsg::kChannelNoticeUser, "PLAYER_NOT_BANNED", notify.channel_name,
                       notify.text);
    break;

  case ChannelNotify::kPlayerAlreadyMember:
    EmitChannelMessage(ChatMsg::kChannelNoticeUser, "PLAYER_ALREADY_MEMBER",
                       notify.channel_name, {}, notify.actor_guid);
    break;

  case ChannelNotify::kInvite:
    if (notify.actor_guid == 0 || IsIgnoredChannelInvite(*this, notify.actor_guid) ||
        pending_channel_invite_.has_value()) {
      return;
    }
    if (const std::string inviter_name = ResolveChannelActorName(*this, notify.actor_guid);
        !inviter_name.empty()) {
      EmitChannelMessage(ChatMsg::kChannelNoticeUser, "INVITE", notify.channel_name,
                         inviter_name, notify.actor_guid);
      ui::game::ScriptEventDispatch::Get().FireGlobalEventWithArgs(
          ui::game::events::CHANNEL_INVITE_REQUEST,
          {notify.channel_name, inviter_name});
    } else {
      PendingChannelInvite pending_invite;
      pending_invite.inviter_guid = notify.actor_guid;
      pending_invite.channel_name = notify.channel_name;
      pending_channel_invite_ = std::move(pending_invite);
      (void)query_cache_.RequestNameQuery(notify.actor_guid);
    }
    break;

  case ChannelNotify::kInviteWrongFaction:
    EmitChannelMessage(ChatMsg::kChannelNoticeUser, "INVITE_WRONG_FACTION",
                       notify.channel_name);
    break;

  case ChannelNotify::kWrongFaction:
    EmitChannelMessage(ChatMsg::kChannelNotice, "WRONG_FACTION", notify.channel_name);
    clear_channel_notify_record();
    fire_channel_ui_update = true;
    break;

  case ChannelNotify::kInvalidName:
    EmitChannelMessage(ChatMsg::kChannelNoticeUser, "INVALID_NAME", notify.channel_name);
    clear_channel_notify_record();
    fire_channel_ui_update = true;
    break;

  case ChannelNotify::kNotModerated:
    EmitChannelMessage(ChatMsg::kChannelNoticeUser, "NOT_MODERATED", notify.channel_name);
    break;

  case ChannelNotify::kPlayerInvited:
    EmitChannelMessage(ChatMsg::kChannelNoticeUser, "PLAYER_INVITED", notify.channel_name,
                       notify.text);
    break;

  case ChannelNotify::kPlayerInviteBanned:
    EmitChannelMessage(ChatMsg::kChannelNoticeUser, "PLAYER_INVITE_BANNED",
                       notify.channel_name, notify.text);
    break;

  case ChannelNotify::kThrottled:
    EmitChannelMessage(ChatMsg::kChannelNotice, "THROTTLED", notify.channel_name);
    break;

  case ChannelNotify::kNotInArea:
    EmitChannelMessage(ChatMsg::kChannelNotice, "NOT_IN_AREA", notify.channel_name);
    clear_channel_notify_record();
    fire_channel_ui_update = true;
    break;

  case ChannelNotify::kNotInLfg:
    EmitChannelMessage(ChatMsg::kChannelNotice, "NOT_IN_LFG", notify.channel_name);
    clear_channel_notify_record();
    fire_channel_ui_update = true;
    break;

  case ChannelNotify::kVoiceOn:
    SetChannelVoiceState(notify.channel_name, true);
    EmitChannelMessage(ChatMsg::kChannelNoticeUser, "VOICE_ON", notify.channel_name, {},
                       notify.actor_guid);
    fire_channel_ui_update = existing_channel != nullptr;
    break;

  case ChannelNotify::kVoiceOff:
    SetChannelVoiceState(notify.channel_name, false);
    EmitChannelMessage(ChatMsg::kChannelNoticeUser, "VOICE_OFF", notify.channel_name, {},
                       notify.actor_guid);
    fire_channel_ui_update = existing_channel != nullptr;
    break;

  case ChannelNotify::kVoiceOnSilent:
    SetChannelVoiceState(notify.channel_name, true);
    fire_channel_ui_update = existing_channel != nullptr;
    break;

  default:
    return;
  }

  if (fire_channel_ui_update) {
    ui::game::ScriptEventDispatch::Get().FireChannelUiUpdate();
  }
}

void WorldSession::HandleSpamFilterResult(const net::wotlk::WorldPacket &pkt) {
  const auto *data = pkt.payload.data();
  const std::size_t size = pkt.payload.size();
  if (size < sizeof(std::uint32_t)) {
    return;
  }

  std::uint32_t count = 0;
  std::memcpy(&count, data, sizeof(count));

  std::size_t offset = sizeof(count);
  std::vector<std::string> patterns;
  patterns.reserve(count);

  for (std::uint32_t i = 0; i < count; ++i) {
    patterns.push_back(ReadSpamFilterPattern(data, size, &offset));
  }

  SetChatDisplayServerSpamFilters(std::move(patterns));
}

void WorldSession::HandleChatPlayerNotFound(const net::wotlk::WorldPacket &pkt) {
  if (!chat_.HandleChatPlayerNotFound(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  ui::game::AutoComplete::Get().ClearRecentPlayerNameContextBits(chat_.last_player_not_found(),
                                                                 0x20u);
  ui::game::ScriptEventDispatch::Get().FireChatPlayerNotFound("");
}

void WorldSession::HandleChannelList(const net::wotlk::WorldPacket &pkt) {
  PacketReader reader(pkt.payload.data(), pkt.payload.size());
  std::uint8_t response_kind = 0;
  if (!reader.ReadU8(response_kind)) {
    return;
  }

  if (response_kind != 0) {
    std::string channel_name;
    std::uint8_t channel_flags = 0;
    std::uint32_t member_count = 0;
    if (!reader.ReadCString(channel_name) || !reader.ReadU8(channel_flags) ||
        !reader.ReadU32(member_count)) {
      return;
    }

    auto &chat_system = ChatSystem::Get();
    if (!chat_system.IsWatchingJoinedChannel(channel_name)) {
      return;
    }

    chat_system.SetSelectedJoinedChannelSelfFlags(channel_name, 0);
    std::vector<ChannelRosterMember> members;
    members.reserve(member_count);
    std::uint32_t pending_name_queries = 0;
    const std::uint64_t local_player_guid = objects().GetLocalPlayerGuid().GetRawValue();

    for (std::uint32_t i = 0; i < member_count; ++i) {
      ChannelRosterMember member;
      if (!reader.ReadU64(member.guid) || !reader.ReadU8(member.flags)) {
        return;
      }
      member.raw_flags =
          DecorateWatchedChannelRosterVoiceFlags(channel_name, member.guid, member.flags, false);
      member.flags = ApplyLocalChannelRosterMuteFlag(channel_name, member.guid, member.raw_flags);

      member.name = ResolveCachedChannelRosterMemberName(*this, member.guid);
      if (member.name.empty()) {
        member.name_query_pending = true;
        ++pending_name_queries;

        const ObjectGuid member_guid(member.guid);
        if (member_guid.IsPlayer() && pending_chat_name_queries_.insert(member.guid).second) {
          (void)query_cache_.RequestNameQuery(member.guid);
        }
      }

      if (member.guid == local_player_guid) {
        chat_system.SetSelectedJoinedChannelSelfFlags(channel_name, member.flags);
      }
      members.push_back(std::move(member));
    }

    chat_system.ReplaceWatchedChannelRoster(channel_name, std::move(members), pending_name_queries);
    const bool flags_changed =
        UpdateJoinedChannelDisplayState(channel_name, channel_flags, member_count);
    if (pending_name_queries == 0) {
      FireWatchedChannelRosterEvents(channel_name, true);
    } else if (flags_changed) {
      ui::game::ScriptEventDispatch::Get().FireChannelUiUpdate();
    }
    return;
  }

  if (!chat_.HandleChannelList(pkt.payload.data() + 1, pkt.payload.size() - 1)) {
    return;
  }

  const auto &response = chat_.last_channel_list();
  if (!response.has_value()) {
    return;
  }

  PendingChannelListDisplay pending;
  pending.channel_name = response->channel_name;
  pending.members.reserve(response->members.size());

  bool waiting_for_name_query = false;
  for (const ChannelMember &member : response->members) {
    pending.members.push_back({member.player_guid, member.member_flags});
    if (!ResolveImmediateChannelListMemberName(member.player_guid).empty()) {
      continue;
    }

    waiting_for_name_query = true;
    const ObjectGuid member_guid(member.player_guid);
    if (member_guid.IsPlayer() && pending_chat_name_queries_.insert(member.player_guid).second) {
      (void)query_cache_.RequestNameQuery(member.player_guid);
    }
  }

  if (waiting_for_name_query) {
    pending_channel_lists_.erase(
        std::remove_if(pending_channel_lists_.begin(), pending_channel_lists_.end(),
                       [&pending](const PendingChannelListDisplay &existing) {
                         return existing.channel_name == pending.channel_name;
                       }),
        pending_channel_lists_.end());
    pending_channel_lists_.push_back(std::move(pending));
  } else {
    (void)TryDisplayPendingChannelList(pending);
  }

  FireChannelRosterUpdateForChannel(
      response->channel_name, static_cast<std::uint32_t>(response->members.size()));
}

void WorldSession::HandleChatWrongFaction(const net::wotlk::WorldPacket &pkt) {
  (void)pkt;

  chat_.HandleChatWrongFaction();
  ui::game::DisplaySystemMessage(kChatWrongFactionSystemMessageId);
}

void WorldSession::HandleChatServerMessage(const net::wotlk::WorldPacket &pkt) {
  if (!chat_.HandleChatServerMessage(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto& server_message = chat_.last_server_message();
  if (!server_message.has_value()) {
    return;
  }

  const std::string formatted_message =
      FormatKbServerMessage(*this, *server_message);
  KnowledgeBase::Get().SetFormattedServerMessage(
      server_message->message_type, formatted_message);

  ChatFrame_DisplayMessage(
      objects(), formatted_message.c_str(), ChatDisplayType::kSystem, nullptr,
      static_cast<int>(Language::kUniversal), nullptr, nullptr, nullptr,
      0, 0, 0, 0, 0, nullptr);
  auto& dispatch = ui::game::ScriptEventDispatch::Get();
  dispatch.FireGlobalEventWithArgs(
      ui::game::events::KNOWLEDGE_BASE_SERVER_MESSAGE, {});
}

void WorldSession::HandleChatNotInParty(const net::wotlk::WorldPacket &pkt) {

  if (!chat_.HandleChatNotInParty(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  switch (static_cast<ChatMsg>(chat_.last_chat_not_in_party_type())) {
    case ChatMsg::kParty:
    case ChatMsg::kPartyLeader:
      ui::game::DisplaySystemMessage(kNotInGroupSystemMessageId);
      return;
    case ChatMsg::kRaid:
    case ChatMsg::kRaidLeader:
    case ChatMsg::kRaidWarning:
      ui::game::DisplaySystemMessage(kNotInRaidSystemMessageId);
      return;
    default:
      return;
  }
}

void WorldSession::HandleChatRestricted(const net::wotlk::WorldPacket &pkt) {

  if (!chat_.HandleChatRestricted(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  switch (chat_.last_chat_restriction()) {
    case 0:
      ui::game::DisplaySystemMessage(kChatRestrictedGenericMessageId);
      return;
    case 1:
      ui::game::DisplaySystemMessage(kChatThrottledMessageId);
      return;
    case 2:
      ui::game::DisplaySystemMessage(kChatUserSquelchedMessageId);
      return;
    case 3:
      ui::game::DisplaySystemMessage(kChatYellRestrictedMessageId);
      return;
    default:
      return;
  }
}

void WorldSession::HandleDefenseMessage(const net::wotlk::WorldPacket &pkt) {
  if (!chat_.HandleDefenseMessage(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &defense_message = chat_.last_defense_message();
  if (!defense_message.has_value() || defense_message->message.empty()) {
    return;
  }

  for (const auto &channel_name : CollectDefenseMessageChannels(*this, defense_message->zone_id)) {
    EmitChannelMessage(ChatMsg::kChannel, defense_message->message, channel_name);
  }
}

void WorldSession::HandleChatPlayerAmbiguous(const net::wotlk::WorldPacket &pkt) {

  if (!chat_.HandleChatPlayerAmbiguous(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  ui::game::DisplaySystemMessage(kChatPlayerAmbiguousMessageId,
                                 chat_.last_ambiguous_player().c_str());
}

void WorldSession::HandleChannelMemberCount(const net::wotlk::WorldPacket &pkt) {
  chat_.HandleChannelMemberCount(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleNotifyPartySquelch(const net::wotlk::WorldPacket &pkt) {
  const PartySquelchNotificationPacket packet =
      ParsePartySquelchNotificationPacket(pkt.payload.data(), pkt.payload.size());
  ui::game::DisplaySystemMessage(packet.squelched != 0 ? kPlayerSilencedSystemMessageId
                                                       : kPlayerUnsilencedSystemMessageId);
}

void WorldSession::HandleEchoPartySquelch(const net::wotlk::WorldPacket &pkt) {
  const PartySquelchEchoPacket packet =
      ParsePartySquelchEchoPacket(pkt.payload.data(), pkt.payload.size());
  const auto *name_entry = objects().GetNameEntry(ObjectGuid(packet.player_guid));
  if (name_entry == nullptr || name_entry->name.empty()) {
    return;
  }

  ui::game::DisplaySystemMessage(packet.squelched != 0 ? kPlayerSilencedEchoSystemMessageId
                                                       : kPlayerUnsilencedEchoSystemMessageId,
                                 name_entry->name.c_str());
}

void WorldSession::HandleComplainResult(const net::wotlk::WorldPacket &pkt) {
  chat_.HandleComplainResult(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleUserlistAdd(const net::wotlk::WorldPacket &pkt) {
  if (!chat_.HandleUserlistAdd(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &entry = chat_.last_userlist_add();
  if (!entry.has_value()) {
    return;
  }

  auto &chat_system = ChatSystem::Get();
  if (!chat_system.IsWatchingJoinedChannel(entry->channel_name)) {
    return;
  }

  ChannelRosterMember member;
  member.guid = entry->guid;
  member.raw_flags = DecorateWatchedChannelRosterVoiceFlags(entry->channel_name, entry->guid,
                                                            entry->user_flags, false);
  member.flags = ApplyLocalChannelRosterMuteFlag(entry->channel_name, entry->guid, member.raw_flags);
  member.name = ResolveCachedChannelRosterMemberName(*this, entry->guid);
  member.name_query_pending = member.name.empty();

  if (!chat_system.AddWatchedChannelRosterMember(entry->channel_name, member)) {
    return;
  }

  if (member.name_query_pending) {
    const ObjectGuid member_guid(entry->guid);
    if (member_guid.IsPlayer() && pending_chat_name_queries_.insert(entry->guid).second) {
      (void)query_cache_.RequestNameQuery(entry->guid);
    }
  } else {
    chat_system.SortWatchedChannelRoster();
  }

  const auto roster_size = static_cast<std::uint32_t>(chat_system.GetWatchedChannelRosterSize());
  const bool flags_changed =
      UpdateJoinedChannelDisplayState(entry->channel_name, entry->channel_flags, roster_size);

  FireChannelUserlistEvents(entry->channel_name, flags_changed, roster_size);
  FireWatchedChannelRosterEvents(entry->channel_name, true);
}

void WorldSession::HandleUserlistRemove(const net::wotlk::WorldPacket &pkt) {
  if (!chat_.HandleUserlistRemove(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &entry = chat_.last_userlist_remove();
  if (!entry.has_value()) {
    return;
  }

  auto &chat_system = ChatSystem::Get();
  if (!chat_system.IsWatchingJoinedChannel(entry->channel_name) ||
      !chat_system.RemoveWatchedChannelRosterMember(entry->channel_name, entry->guid)) {
    return;
  }

  const auto roster_size = static_cast<std::uint32_t>(chat_system.GetWatchedChannelRosterSize());
  const bool flags_changed =
      UpdateJoinedChannelDisplayState(entry->channel_name, entry->channel_flags, roster_size);
  FireChannelUserlistEvents(entry->channel_name, flags_changed, roster_size);
  FireWatchedChannelRosterEvents(entry->channel_name, true);
}

void WorldSession::HandleUserlistUpdate(const net::wotlk::WorldPacket &pkt) {
  if (!chat_.HandleUserlistUpdate(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &entry = chat_.last_userlist_update();
  if (!entry.has_value()) {
    return;
  }

  auto &chat_system = ChatSystem::Get();
  if (!chat_system.IsWatchingJoinedChannel(entry->channel_name)) {
    return;
  }

  const std::uint8_t raw_flags = DecorateWatchedChannelRosterVoiceFlags(
      entry->channel_name, entry->guid, entry->user_flags, false);
  const std::uint8_t display_flags =
      ApplyLocalChannelRosterMuteFlag(entry->channel_name, entry->guid, raw_flags);
  if (!chat_system.UpdateWatchedChannelRosterMemberFlags(entry->channel_name, entry->guid,
                                                         raw_flags, display_flags)) {
    return;
  }

  const auto roster_size = static_cast<std::uint32_t>(chat_system.GetWatchedChannelRosterSize());
  const bool flags_changed =
      UpdateJoinedChannelDisplayState(entry->channel_name, entry->channel_flags, roster_size);
  FireChannelUserlistEvents(entry->channel_name, flags_changed, roster_size);
  FireWatchedChannelRosterEvents(entry->channel_name, true);
}

}

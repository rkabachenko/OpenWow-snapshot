
#include "openwow/game/voice_chat.h"

#include <algorithm>

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/core/storm_string.h"
#include "openwow/game/chat_system.h"
#include "openwow/game/comsat_client.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/game/cvar_system.h"

namespace openwow::game {

namespace {

constexpr char kEnableVoiceChatCVarName[] = "EnableVoiceChat";
constexpr char kAutoJoinPartyVoiceCVarName[] = "autojoinPartyVoice";
constexpr char kAutoJoinBattlegroundVoiceCVarName[] = "autojoinBGVoice";

[[nodiscard]] std::optional<DisplayChannelKind>
ResolveDisplayChannelKindForSessionType(const VoiceChatChannelType type) {
  switch (type) {
  case VoiceChatChannelType::kBattleground:
    return DisplayChannelKind::kSpecialSlot1;
  case VoiceChatChannelType::kParty:
    return DisplayChannelKind::kSpecialSlot2;
  case VoiceChatChannelType::kRaid:
    return DisplayChannelKind::kSpecialSlot3;
  case VoiceChatChannelType::kCustom:
    return std::nullopt;
  }

  return std::nullopt;
}

[[nodiscard]] const char* ResolveAutoJoinCVarNameForSessionType(const VoiceChatChannelType type) {
  return type == VoiceChatChannelType::kBattleground ? kAutoJoinBattlegroundVoiceCVarName
                                                     : kAutoJoinPartyVoiceCVarName;
}

bool ReadEnableVoiceChatSetting(const bool fallback_value) {
  auto &cvars = ui::game::CVarSystem::Instance();
  if (!cvars.Exists(kEnableVoiceChatCVarName)) {
    return fallback_value;
  }

  return cvars.GetCVarBool(kEnableVoiceChatCVarName);
}

bool ReadVoiceAutoJoinSetting(const VoiceChatChannelType type) {
  auto& cvars = ui::game::CVarSystem::Instance();
  const char* const cvar_name = ResolveAutoJoinCVarNameForSessionType(type);
  return cvars.Exists(cvar_name) && cvars.GetCVarBool(cvar_name);
}

void WriteEnableVoiceChatSetting(const bool enabled) {
  auto &cvars = ui::game::CVarSystem::Instance();
  if (!cvars.Exists(kEnableVoiceChatCVarName)) {
    return;
  }

  cvars.SetCVar(kEnableVoiceChatCVarName, enabled ? "1" : "0", true);
}

template <typename SessionMap>
void RemoveChannelIfUnused(std::vector<VoiceChatChannel>* channels, const SessionMap& sessions,
                           std::string_view channel_name) {
  if (std::any_of(sessions.begin(), sessions.end(),
                  [channel_name](const auto& entry) {
                    return entry.second.channel_name == channel_name;
                  })) {
    return;
  }

  channels->erase(std::remove_if(channels->begin(), channels->end(),
                                 [channel_name](const VoiceChatChannel& channel) {
                                   return channel.channelName == channel_name;
                                 }),
                  channels->end());
}

template <typename SessionMap>
auto FindSessionEntryByName(SessionMap& sessions, const std::string_view session_name) {
  const std::string target(session_name);
  if (auto exact = sessions.find(target); exact != sessions.end()) {
    return exact;
  }

  for (auto it = sessions.begin(); it != sessions.end(); ++it) {
    if (openwow::core::SStrCmpUTF8NoCase(it->first.c_str(), target.c_str(), 0x7FFFFFFF) == 0) {
      return it;
    }
  }

  return sessions.end();
}

void SetJoinedChannelVoiceEnabled(const std::string_view channel_name, const bool voice_enabled) {
  auto& chat_system = ChatSystem::Get();
  const auto* existing = chat_system.GetChannelByName(std::string(channel_name));
  if (existing == nullptr) {
    return;
  }

  ChatChannel updated = *existing;
  updated.voice_enabled = voice_enabled;
  if (voice_enabled) {
    updated.flags |= 0x80u;
  } else {
    updated.flags &= static_cast<std::uint8_t>(~0x80u);
  }
  chat_system.UpdateChannel(std::string(channel_name), updated);
}

[[nodiscard]] std::optional<std::uint32_t> FindDisplaySlotForJoinedChannel(
    const std::string_view channel_name) {
  auto& chat_system = ChatSystem::Get();
  const auto display_count = chat_system.GetNumDisplayChannels();
  for (std::size_t index = 0; index < display_count; ++index) {
    const auto info = chat_system.GetDisplayChannelInfo(index);
    if (!info.has_value() || info->is_header || info->kind != DisplayChannelKind::kJoinedChannel) {
      continue;
    }

    if (openwow::core::SStrCmpUTF8NoCase(info->channel_name.c_str(),
                                         std::string(channel_name).c_str(),
                                         0x7FFFFFFF) == 0) {
      return static_cast<std::uint32_t>(index);
    }
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<std::uint32_t> FindDisplaySlotForSessionType(
    const VoiceChatChannelType type) {
  const auto target_kind = ResolveDisplayChannelKindForSessionType(type);
  if (!target_kind.has_value()) {
    return std::nullopt;
  }

  auto& chat_system = ChatSystem::Get();
  const auto display_count = chat_system.GetNumDisplayChannels();
  for (std::size_t index = 0; index < display_count; ++index) {
    const auto info = chat_system.GetDisplayChannelInfo(index);
    if (!info.has_value() || info->is_header || info->kind != *target_kind) {
      continue;
    }

    return static_cast<std::uint32_t>(index);
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<std::uint32_t> FindDisplaySlotForSession(
    const std::string_view session_name, const VoiceChatChannelType type) {
  if (type == VoiceChatChannelType::kCustom) {
    return FindDisplaySlotForJoinedChannel(session_name);
  }

  return FindDisplaySlotForSessionType(type);
}

[[nodiscard]] std::uint32_t ResolveVoiceSessionCode(const VoiceChatChannelType type) {
  switch (type) {
  case VoiceChatChannelType::kCustom:
    return 0;
  case VoiceChatChannelType::kBattleground:
    return 1;
  case VoiceChatChannelType::kParty:
    return 2;
  case VoiceChatChannelType::kRaid:
    return 3;
  }

  return 4;
}

[[nodiscard]] bool ResolveDisplaySlotVoiceEnabled(const std::uint32_t slot) {
  const auto info = ChatSystem::Get().GetDisplayChannelInfo(slot);
  return info.has_value() && !info->is_header && info->voice_enabled;
}

[[nodiscard]] bool IsBackgroundVoiceDisplaySlotSelected(
    const openwow::audio::SoundRuntime& sound_interface,
    const std::uint32_t slot) {
  return sound_interface.HasBackgroundSoundSelection() &&
         sound_interface.GetBackgroundSoundState() == slot;
}

}

VoiceChat &VoiceChat::Get() {
  static VoiceChat instance;
  return instance;
}

VoiceSpeaker *VoiceChat::FindSpeaker(std::uint64_t guid) {
  for (auto &s : speakers_) {
    if (s.guid == guid)
      return &s;
  }
  return nullptr;
}

const VoiceSpeaker *VoiceChat::FindSpeaker(std::uint64_t guid) const {
  for (const auto &s : speakers_) {
    if (s.guid == guid)
      return &s;
  }
  return nullptr;
}

VoiceChatChannel *VoiceChat::FindChannel(const std::string &name) {
  for (auto &ch : channels_) {
    if (ch.channelName == name)
      return &ch;
  }
  return nullptr;
}

const VoiceChatChannel *VoiceChat::FindChannel(const std::string &name) const {
  for (const auto &ch : channels_) {
    if (ch.channelName == name)
      return &ch;
  }
  return nullptr;
}

const VoiceChatChannel *VoiceChat::FindChannelByType(const VoiceChatChannelType type) const {
  for (const auto &channel : channels_) {
    if (channel.channelType == type) {
      return &channel;
    }
  }
  return nullptr;
}

std::optional<std::size_t> VoiceChat::FindSessionSlotByIdLocked(
    const std::uint64_t session_id) const {
  if (session_id == 0) {
    return std::nullopt;
  }

  for (std::size_t index = 0; index < session_slots_.size(); ++index) {
    if (session_slots_[index].has_value() && session_slots_[index]->session_id == session_id) {
      return index;
    }
  }

  return std::nullopt;
}

std::optional<std::size_t> VoiceChat::FindFirstFreeSessionSlotLocked() const {
  for (std::size_t index = 0; index < session_slots_.size(); ++index) {
    if (!session_slots_[index].has_value()) {
      return index;
    }
  }

  return std::nullopt;
}

std::optional<std::size_t> VoiceChat::FindSessionSlotByOrdinalLocked(
    const std::uint32_t ordinal) const {
  if (ordinal == 0) {
    return std::nullopt;
  }

  std::uint32_t active_count = 0;
  for (std::size_t index = 0; index < session_slots_.size(); ++index) {
    if (!session_slots_[index].has_value()) {
      continue;
    }

    ++active_count;
    if (active_count == ordinal) {
      return index;
    }
  }

  return std::nullopt;
}

std::optional<std::uint32_t> VoiceChat::GetSessionOrdinalForSlotLocked(
    const std::size_t slot_index) const {
  if (slot_index == kInvalidSessionSlot || slot_index >= session_slots_.size() ||
      !session_slots_[slot_index].has_value()) {
    return std::nullopt;
  }

  std::uint32_t active_count = 0;
  for (std::size_t index = 0; index <= slot_index; ++index) {
    if (session_slots_[index].has_value()) {
      ++active_count;
    }
  }

  return active_count == 0 ? std::nullopt : std::optional<std::uint32_t>(active_count);
}

void VoiceChat::ClearRuntimeStateLocked(const bool clear_muted_players) {
  current_channel_.clear();
  speakers_.clear();
  channels_.clear();
  session_slots_ = {};
  current_session_slot_ = kInvalidSessionSlot;
  active_voice_display_slot_.reset();
  channel_sessions_.clear();
  session_rosters_.clear();
  session_speakers_.clear();
  speaking_players_.clear();
  if (clear_muted_players) {
    muted_players_.clear();
  }
}

void VoiceChat::SetEnabled(openwow::audio::SoundRuntime& sound_runtime,
                           bool enabled) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    enabled_ = enabled;
    if (!enabled) {
      state_ = VoiceChatState::kDisabled;
      ClearRuntimeStateLocked(true);
    }
  }

  WriteEnableVoiceChatSetting(enabled);
  if (!enabled) {
    sound_runtime.ClearBackgroundSoundState();
  }
}

bool VoiceChat::IsEnabled() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return ReadEnableVoiceChatSetting(enabled_);
}

void VoiceChat::SetServerAllowed(openwow::audio::SoundRuntime& sound_runtime,
                                 bool allowed) {
  bool clear_background_sound_state = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (server_allowed_ == allowed) {
      return;
    }

    server_allowed_ = allowed;
    if (allowed) {
      return;
    }

    ClearRuntimeStateLocked(false);
    if (state_ != VoiceChatState::kDisabled) {
      state_ = ReadEnableVoiceChatSetting(enabled_) ? VoiceChatState::kConnected
                                                    : VoiceChatState::kDisconnected;
    }
    clear_background_sound_state = true;
  }

  if (clear_background_sound_state) {
    sound_runtime.ClearBackgroundSoundState();
  }
}

bool VoiceChat::IsServerAllowed() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return server_allowed_;
}

bool VoiceChat::IsEnabledAndActive() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return server_allowed_ && ReadEnableVoiceChatSetting(enabled_) && !VoiceChat_IsDisabled();
}

bool VoiceChat::IsAllowedAndEnabled() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return server_allowed_ && !VoiceChat_IsDisabled();
}

VoiceChatState VoiceChat::GetState() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}

void VoiceChat::SetState(VoiceChatState state) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = state;
    if (state == VoiceChatState::kDisabled) {
      enabled_ = false;
    }
  }

  if (state == VoiceChatState::kDisabled) {
    WriteEnableVoiceChatSetting(false);
  }
}

void VoiceChat::SetMicrophoneMuted(bool muted) {
  std::lock_guard<std::mutex> lock(mutex_);
  microphone_muted_ = muted;
}

bool VoiceChat::IsMicrophoneMuted() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return microphone_muted_;
}

void VoiceChat::SetOutputVolume(float vol) {
  std::lock_guard<std::mutex> lock(mutex_);
  master_volume_ = std::clamp(vol, 0.0f, 1.0f);
}

float VoiceChat::GetOutputVolume() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return master_volume_;
}

void VoiceChat::SetInputVolume(float vol) {
  std::lock_guard<std::mutex> lock(mutex_);
  microphone_volume_ = std::clamp(vol, 0.0f, 1.0f);
}

float VoiceChat::GetInputVolume() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return microphone_volume_;
}

void VoiceChat::SetVoiceActivated(bool activated) {
  std::lock_guard<std::mutex> lock(mutex_);
  voice_activated_ = activated;
}

bool VoiceChat::IsVoiceActivated() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return voice_activated_;
}

void VoiceChat::SetPushToTalkKey(const std::string &keyName) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    push_to_talk_key_ = keyName;
  }

  VoiceChat_HandlePushToTalkReassign();
}

std::string VoiceChat::GetPushToTalkKey() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return push_to_talk_key_;
}

void VoiceChat::JoinChannel(const std::string &name, const VoiceChatChannelType type) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (FindChannel(name))
    return;
  VoiceChatChannel ch;
  ch.channelName = name;
  ch.channelType = type;
  channels_.push_back(std::move(ch));
  state_ = VoiceChatState::kInChannel;
}

void VoiceChat::LeaveChannel(const std::string &name) {
  std::lock_guard<std::mutex> lock(mutex_);
  channels_.erase(
      std::remove_if(channels_.begin(), channels_.end(),
                     [&name](const VoiceChatChannel &ch) { return ch.channelName == name; }),
      channels_.end());
  session_rosters_.erase(name);
  session_speakers_.erase(name);
  if (current_channel_ == name) {
    current_channel_.clear();
  }
  if (channels_.empty() && enabled_) {
    state_ = VoiceChatState::kConnected;
  }
}

std::vector<VoiceChatChannel> VoiceChat::GetChannels() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return channels_;
}

bool VoiceChat::IsInChannel(const std::string &name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return FindChannel(name) != nullptr;
}

bool VoiceChat::HasChannelType(const VoiceChatChannelType type) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return FindChannelByType(type) != nullptr;
}

void VoiceChat::MuteChannel(const std::string &name, bool muted) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto *ch = FindChannel(name);
  if (ch)
    ch->isMuted = muted;
}

bool VoiceChat::IsChannelMuted(const std::string &name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto *ch = FindChannel(name);
  return ch ? ch->isMuted : false;
}

void VoiceChat::SetChannelVolume(const std::string &name, float volume) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto *ch = FindChannel(name);
  if (ch)
    ch->volume = std::clamp(volume, 0.0f, 1.0f);
}

void VoiceChat::UpsertChannelSession(const std::uint64_t session_id, const std::string_view name,
                                     const VoiceChatChannelType type) {
  if (session_id == 0 || name.empty()) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  std::optional<std::size_t> session_slot = FindSessionSlotByIdLocked(session_id);
  if (const auto existing_session = channel_sessions_.find(session_id);
      existing_session != channel_sessions_.end()) {
    const auto old_channel_name = existing_session->second.channel_name;
    const auto old_channel_type = existing_session->second.channel_type;
    if (old_channel_name == name && old_channel_type == type) {
      if (auto* channel = FindChannel(std::string(name)); channel != nullptr) {
        channel->channelType = type;
      }
      if (session_slot.has_value()) {
        session_slots_[*session_slot]->session_name = std::string(name);
        session_slots_[*session_slot]->channel_type = type;
      }
      return;
    }

    channel_sessions_.erase(existing_session);
    RemoveChannelIfUnused(&channels_, channel_sessions_, old_channel_name);
  }

  if (!session_slot.has_value()) {
    session_slot = FindFirstFreeSessionSlotLocked();
    if (!session_slot.has_value()) {
      return;
    }
  }

  channel_sessions_[session_id] = {std::string(name), type};
  session_slots_[*session_slot] = VoiceSessionSlot{session_id, std::string(name), type};
  if (current_session_slot_ == *session_slot) {
    current_channel_ = std::string(name);
  }
  if (auto* channel = FindChannel(std::string(name)); channel != nullptr) {
    channel->channelType = type;
  } else {
    VoiceChatChannel new_channel;
    new_channel.channelName = std::string(name);
    new_channel.channelType = type;
    channels_.push_back(std::move(new_channel));
  }
}

void VoiceChat::RemoveChannelSession(const std::uint64_t session_id) {
  if (session_id == 0) {
    return;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto session_slot = FindSessionSlotByIdLocked(session_id);
    const auto existing_session = channel_sessions_.find(session_id);
    if (existing_session == channel_sessions_.end()) {
      return;
    }

    const auto channel_name = existing_session->second.channel_name;
    channel_sessions_.erase(existing_session);
    RemoveChannelIfUnused(&channels_, channel_sessions_, channel_name);
    const bool channel_still_tracked = std::any_of(
        channel_sessions_.begin(), channel_sessions_.end(),
        [&channel_name](const auto &entry) { return entry.second.channel_name == channel_name; });
    if (!channel_still_tracked) {
      session_rosters_.erase(channel_name);
      session_speakers_.erase(channel_name);
    }
    if (session_slot.has_value()) {
      session_slots_[*session_slot].reset();
    }
    if (current_channel_ == channel_name && FindChannel(channel_name) == nullptr) {
      current_channel_.clear();
    }
  }
}

std::optional<std::uint32_t> VoiceChat::GetCurrentSessionOrdinal() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return GetSessionOrdinalForSlotLocked(current_session_slot_);
}

std::optional<std::uint32_t> VoiceChat::GetActiveVoiceDisplaySlot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return active_voice_display_slot_;
}

std::uint32_t VoiceChat::GetSessionCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return static_cast<std::uint32_t>(std::count_if(
      session_slots_.begin(), session_slots_.end(),
      [](const auto& slot) { return slot.has_value(); }));
}

std::optional<VoiceSessionOrdinalInfo> VoiceChat::GetSessionByOrdinal(
    const std::uint32_t ordinal) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto slot = FindSessionSlotByOrdinalLocked(ordinal);
  if (!slot.has_value()) {
    return std::nullopt;
  }

  const auto& session = session_slots_[*slot];
  VoiceSessionOrdinalInfo info;
  info.session_name = session->session_name;
  info.channel_type = session->channel_type;
  info.active = current_session_slot_ == *slot;
  return info;
}

std::optional<std::uint32_t> VoiceChat::GetSessionMemberCountByOrdinal(
    const std::uint32_t ordinal) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto slot = FindSessionSlotByOrdinalLocked(ordinal);
  if (!slot.has_value()) {
    return std::nullopt;
  }

  const auto& session = session_slots_[*slot];
  if (!session.has_value()) {
    return std::nullopt;
  }

  const auto roster = session_rosters_.find(session->session_name);
  if (roster == session_rosters_.end()) {
    return 0u;
  }

  return static_cast<std::uint32_t>(roster->second.size());
}

std::optional<VoiceSessionRosterMember> VoiceChat::GetSessionMemberByOrdinal(
    const std::uint32_t session_ordinal, const std::uint32_t member_ordinal) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto slot = FindSessionSlotByOrdinalLocked(session_ordinal);
  if (!slot.has_value()) {
    return std::nullopt;
  }

  const auto& session = session_slots_[*slot];
  if (!session.has_value()) {
    return std::nullopt;
  }

  const auto roster = session_rosters_.find(session->session_name);
  if (roster == session_rosters_.end() || member_ordinal == 0 ||
      member_ordinal > roster->second.size()) {
    return std::nullopt;
  }

  return roster->second[member_ordinal - 1];
}

std::optional<std::uint8_t> VoiceChat::GetSessionMemberStatusFlagsByOrdinal(
    const std::uint32_t session_ordinal, const ObjectGuid guid) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (guid.IsEmpty()) {
    return std::nullopt;
  }

  const auto slot = FindSessionSlotByOrdinalLocked(session_ordinal);
  if (!slot.has_value()) {
    return std::nullopt;
  }

  const auto& session = session_slots_[*slot];
  if (!session.has_value()) {
    return std::nullopt;
  }

  const auto roster = session_rosters_.find(session->session_name);
  if (roster == session_rosters_.end()) {
    return std::nullopt;
  }

  const auto member_it =
      std::find_if(roster->second.begin(), roster->second.end(),
                   [guid](const VoiceSessionRosterMember& member) { return member.guid == guid; });
  if (member_it == roster->second.end()) {
    return std::nullopt;
  }

  return member_it->status_flags;
}

std::optional<std::uint32_t> VoiceChat::GetSessionOrdinalByChannel(
    const VoiceChatChannelType type, const std::string_view session_name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string requested_session_name(session_name);
  const auto matches_channel = [&](const VoiceSessionSlot& session_slot) {
    if (session_slot.channel_type != type) {
      return false;
    }

    if (type != VoiceChatChannelType::kCustom) {
      return true;
    }

    return openwow::core::SStrCmpUTF8NoCase(session_slot.session_name.c_str(),
                                            requested_session_name.c_str(),
                                            0x7FFFFFFF) == 0;
  };

  if (current_session_slot_ < session_slots_.size() &&
      session_slots_[current_session_slot_].has_value() &&
      matches_channel(*session_slots_[current_session_slot_])) {
    return GetSessionOrdinalForSlotLocked(current_session_slot_);
  }

  for (std::size_t index = 0; index < session_slots_.size(); ++index) {
    if (!session_slots_[index].has_value() || !matches_channel(*session_slots_[index])) {
      continue;
    }

    return GetSessionOrdinalForSlotLocked(index);
  }

  return std::nullopt;
}

std::uint8_t VoiceChat::DecorateChannelRosterMemberFlags(
    const VoiceChatChannelType type, const std::string_view session_name, const ObjectGuid guid,
    const std::uint8_t base_flags, const bool include_silenced_bit) const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::uint8_t decorated_flags = base_flags & 0x0Fu;
  if (guid.IsEmpty()) {
    return decorated_flags;
  }

  const std::vector<VoiceSessionRosterMember>* session_roster = nullptr;
  if (type == VoiceChatChannelType::kCustom) {
    if (!session_name.empty()) {
      if (const auto session_it = session_rosters_.find(std::string(session_name));
          session_it != session_rosters_.end()) {
        session_roster = &session_it->second;
      }
    }
  } else if (const auto* channel = FindChannelByType(type); channel != nullptr) {
    if (const auto session_it = session_rosters_.find(channel->channelName);
        session_it != session_rosters_.end()) {
      session_roster = &session_it->second;
    }
  }

  const VoiceSessionRosterMember* session_member = nullptr;
  if (session_roster != nullptr) {
    const auto member_it = std::find_if(
        session_roster->begin(), session_roster->end(),
        [guid](const VoiceSessionRosterMember& member) { return member.guid == guid; });
    if (member_it != session_roster->end()) {
      session_member = &(*member_it);
    }
  }

  const bool session_muted =
      session_member != nullptr && (session_member->status_flags & 0x08u) != 0u;
  if (include_silenced_bit && !session_muted) {
    decorated_flags |= 0x10u;
  }
  if (session_member != nullptr && (session_member->status_flags & 0x40u) != 0u) {
    decorated_flags |= 0x40u;
  }
  if (session_member != nullptr && (session_member->status_flags & 0x04u) != 0u) {
    decorated_flags |= 0x80u;
  }
  if (muted_players_.contains(guid.GetRawValue()) || session_muted) {
    decorated_flags |= 0x20u;
  }

  return decorated_flags;
}

VoiceSessionSelectionPacket VoiceChat::SetCurrentSessionByOrdinal(
    openwow::audio::SoundRuntime& sound_runtime,
    const std::optional<std::uint32_t> ordinal) {
  std::optional<std::uint32_t> previous_display_slot;
  std::optional<std::uint32_t> target_display_slot;
  std::string target_session_name;
  VoiceChatChannelType target_session_type = VoiceChatChannelType::kCustom;
  VoiceSessionSelectionPacket selection;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    previous_display_slot = active_voice_display_slot_;
    if (ordinal.has_value()) {
      const auto target_slot = FindSessionSlotByOrdinalLocked(*ordinal);
      if (target_slot.has_value()) {
        const auto& session = session_slots_[*target_slot];
        current_session_slot_ = *target_slot;
        current_channel_ = session->session_name;
        target_session_name = session->session_name;
        target_session_type = session->channel_type;
        selection.channel_type = ResolveVoiceSessionCode(session->channel_type);
        if (session->channel_type == VoiceChatChannelType::kCustom) {
          selection.channel_name = session->session_name;
        }
      } else {
        current_session_slot_ = kInvalidSessionSlot;
        current_channel_.clear();
      }
    } else {
      current_session_slot_ = kInvalidSessionSlot;
      current_channel_.clear();
    }
  }

  if (!target_session_name.empty()) {
    target_display_slot = FindDisplaySlotForSession(target_session_name, target_session_type);
  }

  if (previous_display_slot != target_display_slot) {
    if (previous_display_slot.has_value()) {
      DispatchChannelVoiceUpdateForDisplaySlot(*previous_display_slot,
                                               ResolveDisplaySlotVoiceEnabled(*previous_display_slot),
                                               false);
    }
    if (target_display_slot.has_value()) {
      DispatchChannelVoiceUpdateForDisplaySlot(*target_display_slot,
                                               ResolveDisplaySlotVoiceEnabled(*target_display_slot),
                                               true);
    }
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    active_voice_display_slot_ = target_display_slot;
  }

  auto& sound = sound_runtime;
  if (target_display_slot.has_value()) {
    sound.SetBackgroundSoundState(*target_display_slot);
  } else {
    sound.ClearBackgroundSoundState();
  }

  return selection;
}

void VoiceChat::ClearCurrentSessionSelection(
    openwow::audio::SoundRuntime& sound_runtime) {
  std::optional<std::uint32_t> previous_display_slot;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    current_session_slot_ = kInvalidSessionSlot;
    current_channel_.clear();
    previous_display_slot = active_voice_display_slot_;
    active_voice_display_slot_.reset();
  }

  if (previous_display_slot.has_value()) {
    DispatchChannelVoiceUpdateForDisplaySlot(*previous_display_slot,
                                             ResolveDisplaySlotVoiceEnabled(*previous_display_slot),
                                             false);
  }

  sound_runtime.ClearBackgroundSoundState();
}

bool VoiceChat_ApplyActiveSessionSelection(WorldSession& session,
                                           const std::optional<std::uint32_t> ordinal) {
  if (ordinal.has_value() && !VoiceChat::Get().GetSessionByOrdinal(*ordinal).has_value()) {
    return false;
  }

  const auto selection = VoiceChat::Get().SetCurrentSessionByOrdinal(
      session.sound_runtime(), ordinal);
  if (!ordinal.has_value()) {
    (void)VoiceChat_StopTrackedLocalSpeaker(session);
  }

  session.interaction().SendSetActiveVoiceChannel(selection.channel_type,
                                                  selection.channel_name);
  if (ui::game::ScriptEventDispatch::Get().IsInitialized()) {
    ui::game::ScriptEventDispatch::Get().FireVoiceSessionsUpdate();
  }
  return true;
}

bool VoiceChat_SelectActiveSessionByChannel(WorldSession& session,
                                            const VoiceChatChannelType type,
                                            const std::string_view session_name) {
  const auto ordinal = VoiceChat::Get().GetSessionOrdinalByChannel(type, session_name);
  if (!ordinal.has_value()) {
    return false;
  }

  return VoiceChat_ApplyActiveSessionSelection(session, *ordinal);
}

VoiceSessionUpdateResult VoiceChat::ApplySessionRosterUpdate(
    WorldSession& session, const VoiceSessionRosterUpdate &update) {
  VoiceSessionUpdateResult result;
  if (update.session_id == 0 || update.session_name.empty()) {
    return result;
  }

  UpsertChannelSession(update.session_id, update.session_name, update.channel_type);

  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& session_roster = session_rosters_[update.session_name];
    std::unordered_set<std::uint64_t> previous_members;
    previous_members.reserve(session_roster.size());
    for (const auto& member : session_roster) {
      if (!member.guid.IsEmpty()) {
        previous_members.insert(member.guid.GetRawValue());
      }
    }

    std::unordered_set<std::uint64_t> incoming_members;
    incoming_members.reserve(update.members.size());
    for (const auto &member : update.members) {
      if (!member.guid.IsEmpty()) {
        incoming_members.insert(member.guid.GetRawValue());
      }
    }

    for (const auto raw_guid : previous_members) {
      if (!incoming_members.contains(raw_guid)) {
        result.removed_members.emplace_back(ObjectGuid(raw_guid));
      }
    }

    session_roster = update.members;
    auto &session_state = session_speakers_[update.session_name];
    for (const auto &member : update.members) {
      if (member.guid.IsEmpty()) {
        continue;
      }

      auto &speaker_state = session_state[member.guid.GetRawValue()];
      speaker_state.speaking = (member.status_flags & 0x04u) != 0;
      speaker_state.muted = (member.status_flags & 0x08u) != 0;
      if (!speaker_state.muted && !speaker_state.speaking && speaker_state.volume == 1.0f) {
        session_state.erase(member.guid.GetRawValue());
      }
    }

    for (const auto &removed_guid : result.removed_members) {
      session_state.erase(removed_guid.GetRawValue());
    }

    if (session_state.empty()) {
      session_speakers_.erase(update.session_name);
    }
  }

  if (update.channel_type == VoiceChatChannelType::kCustom) {
    SetJoinedChannelVoiceEnabled(update.session_name, true);
  } else {
    VoiceChat_SyncDisplaySelectionForSessionType(session, update.channel_type);
  }

  result.display_slot = FindDisplaySlotForSession(update.session_name, update.channel_type);
  return result;
}

VoiceSessionUpdateResult VoiceChat::RemoveSessionById(
    WorldSession& session, const std::uint64_t session_id) {
  VoiceSessionUpdateResult result;
  if (session_id == 0) {
    return result;
  }

  std::string session_name;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto existing_session = channel_sessions_.find(session_id);
    if (existing_session == channel_sessions_.end()) {
      return result;
    }

    session_name = existing_session->second.channel_name;
    const bool has_sibling_session = std::any_of(
        channel_sessions_.begin(), channel_sessions_.end(),
        [session_id, &session_name](const auto &entry) {
          return entry.first != session_id && entry.second.channel_name == session_name;
        });
    if (!has_sibling_session) {
      result.removed_current_session = current_channel_ == session_name;
      if (const auto session_roster = session_rosters_.find(session_name);
          session_roster != session_rosters_.end()) {
        result.removed_members.reserve(session_roster->second.size());
        for (const auto& member : session_roster->second) {
          if (!member.guid.IsEmpty()) {
            result.removed_members.emplace_back(member.guid);
          }
        }
      }
    }
  }

  RemoveChannelSession(session_id);
  if (result.removed_current_session) {
    ClearCurrentSessionSelection(session.sound_runtime());
    (void)VoiceChat_StopTrackedLocalSpeaker(session);
    ui::game::ScriptEventDispatch::Get().FireVoiceLeftSession();
  }
  ui::game::ScriptEventDispatch::Get().FireVoiceSessionsUpdate();
  return result;
}

void VoiceChat::SetSessionPlayerMuted(const std::string &session_name, const ObjectGuid guid,
                                      const bool muted) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (session_name.empty() || guid.IsEmpty()) {
    return;
  }

  auto &speaker_state = session_speakers_[session_name][guid.GetRawValue()];
  speaker_state.muted = muted;
  if (!speaker_state.muted && !speaker_state.speaking && speaker_state.volume == 1.0f) {
    session_speakers_[session_name].erase(guid.GetRawValue());
    if (session_speakers_[session_name].empty()) {
      session_speakers_.erase(session_name);
    }
  }
}

bool VoiceChat::IsSessionPlayerMuted(const std::string_view session_name,
                                     const ObjectGuid guid) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (session_name.empty() || guid.IsEmpty()) {
    return false;
  }

  const auto session_it = FindSessionEntryByName(session_speakers_, session_name);
  if (session_it == session_speakers_.end()) {
    return false;
  }

  const auto speaker_it = session_it->second.find(guid.GetRawValue());
  return speaker_it != session_it->second.end() && speaker_it->second.muted;
}

bool VoiceChat::IsPlayerMutedInChannelType(const VoiceChatChannelType type,
                                           const ObjectGuid guid) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (guid.IsEmpty()) {
    return false;
  }

  const auto *channel = FindChannelByType(type);
  if (!channel) {
    return false;
  }

  const auto session_it = FindSessionEntryByName(session_speakers_, channel->channelName);
  if (session_it == session_speakers_.end()) {
    return false;
  }

  const auto speaker_it = session_it->second.find(guid.GetRawValue());
  return speaker_it != session_it->second.end() && speaker_it->second.muted;
}

bool VoiceChat::IsPlayerMutedInCurrentSession(const ObjectGuid guid) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (current_channel_.empty() || guid.IsEmpty()) {
    return false;
  }

  const auto session_it = FindSessionEntryByName(session_speakers_, current_channel_);
  if (session_it == session_speakers_.end()) {
    return false;
  }

  const auto speaker_it = session_it->second.find(guid.GetRawValue());
  return speaker_it != session_it->second.end() && speaker_it->second.muted;
}

void VoiceChat::SetSessionPlayerSpeaking(const std::string &session_name, const ObjectGuid guid,
                                         const bool speaking) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (session_name.empty() || guid.IsEmpty()) {
    return;
  }

  auto &speaker_state = session_speakers_[session_name][guid.GetRawValue()];
  speaker_state.speaking = speaking;
  if (!speaker_state.muted && !speaker_state.speaking && speaker_state.volume == 1.0f) {
    session_speakers_[session_name].erase(guid.GetRawValue());
    if (session_speakers_[session_name].empty()) {
      session_speakers_.erase(session_name);
    }
  }
}

bool VoiceChat::IsSessionPlayerSpeaking(const std::string_view session_name,
                                        const ObjectGuid guid) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (session_name.empty() || guid.IsEmpty()) {
    return false;
  }

  const auto session_it = FindSessionEntryByName(session_speakers_, session_name);
  if (session_it == session_speakers_.end()) {
    return false;
  }

  const auto speaker_it = session_it->second.find(guid.GetRawValue());
  return speaker_it != session_it->second.end() && speaker_it->second.speaking;
}

bool VoiceChat::IsPlayerSpeakingInChannelType(const VoiceChatChannelType type,
                                              const ObjectGuid guid) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (guid.IsEmpty()) {
    return false;
  }

  const auto *channel = FindChannelByType(type);
  if (!channel) {
    return false;
  }

  const auto session_it = FindSessionEntryByName(session_speakers_, channel->channelName);
  if (session_it == session_speakers_.end()) {
    return false;
  }

  const auto speaker_it = session_it->second.find(guid.GetRawValue());
  return speaker_it != session_it->second.end() && speaker_it->second.speaking;
}

bool VoiceChat::IsPlayerSpeakingInCurrentSession(const ObjectGuid guid) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (current_channel_.empty() || guid.IsEmpty()) {
    return false;
  }

  const auto session_it = FindSessionEntryByName(session_speakers_, current_channel_);
  if (session_it == session_speakers_.end()) {
    return false;
  }

  const auto speaker_it = session_it->second.find(guid.GetRawValue());
  return speaker_it != session_it->second.end() && speaker_it->second.speaking;
}

void VoiceChat::MutePlayer(ObjectGuid guid, bool muted) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (muted) {
    muted_players_.insert(guid.GetRawValue());
  } else {
    muted_players_.erase(guid.GetRawValue());
  }

  if (!current_channel_.empty() && !guid.IsEmpty()) {
    auto &speaker_state = session_speakers_[current_channel_][guid.GetRawValue()];
    speaker_state.muted = muted;
    if (!speaker_state.muted && !speaker_state.speaking && speaker_state.volume == 1.0f) {
      session_speakers_[current_channel_].erase(guid.GetRawValue());
      if (session_speakers_[current_channel_].empty()) {
        session_speakers_.erase(current_channel_);
      }
    }
  }
}

bool VoiceChat::IsPlayerMuted(ObjectGuid guid) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return muted_players_.count(guid.GetRawValue()) > 0;
}

void VoiceChat::SetSpeaking(ObjectGuid guid, bool speaking) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (speaking) {
    speaking_players_.insert(guid.GetRawValue());
  } else {
    speaking_players_.erase(guid.GetRawValue());
  }

  if (!current_channel_.empty() && !guid.IsEmpty()) {
    auto &speaker_state = session_speakers_[current_channel_][guid.GetRawValue()];
    speaker_state.speaking = speaking;
    if (!speaker_state.muted && !speaker_state.speaking && speaker_state.volume == 1.0f) {
      session_speakers_[current_channel_].erase(guid.GetRawValue());
      if (session_speakers_[current_channel_].empty()) {
        session_speakers_.erase(current_channel_);
      }
    }
  }
}

bool VoiceChat::IsSpeaking(ObjectGuid guid) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return speaking_players_.count(guid.GetRawValue()) > 0;
}

bool VoiceChat::HasSpeakingPlayers() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return !speaking_players_.empty();
}

std::vector<ObjectGuid> VoiceChat::GetSpeakingPlayers() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<ObjectGuid> result;
  result.reserve(speaking_players_.size());
  for (auto raw : speaking_players_) {
    result.emplace_back(ObjectGuid{raw});
  }
  return result;
}

void VoiceChat::JoinVoiceChannel(const std::string &channel_name) {
  std::lock_guard<std::mutex> lock(mutex_);
  current_channel_ = channel_name;
  current_session_slot_ = kInvalidSessionSlot;
  state_ = VoiceChatState::kInChannel;
}

void VoiceChat::LeaveVoiceChannel(openwow::audio::SoundRuntime& sound_runtime) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    current_channel_.clear();
    current_session_slot_ = kInvalidSessionSlot;
    active_voice_display_slot_.reset();
    speakers_.clear();
    if (ReadEnableVoiceChatSetting(enabled_)) {
      state_ = VoiceChatState::kConnected;
    } else {
      state_ = VoiceChatState::kDisconnected;
    }
  }

  sound_runtime.ClearBackgroundSoundState();
}

std::string VoiceChat::GetCurrentChannel() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return current_channel_;
}

bool VoiceChat::IsInVoiceChannel() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return !current_channel_.empty();
}

void VoiceChat::AddSpeaker(const VoiceSpeaker &speaker) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto &s : speakers_) {
    if (s.guid == speaker.guid)
      return;
  }
  speakers_.push_back(speaker);
}

void VoiceChat::RemoveSpeaker(std::uint64_t guid) {
  std::lock_guard<std::mutex> lock(mutex_);
  speakers_.erase(std::remove_if(speakers_.begin(), speakers_.end(),
                                 [guid](const VoiceSpeaker &s) { return s.guid == guid; }),
                  speakers_.end());
}

std::vector<VoiceSpeaker> VoiceChat::GetSpeakers() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return speakers_;
}

std::uint32_t VoiceChat::GetSpeakerCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return static_cast<std::uint32_t>(speakers_.size());
}

void VoiceChat::SetSpeaking(std::uint64_t guid, bool speaking) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto *s = FindSpeaker(guid);
  if (s)
    s->speaking = speaking;
}

bool VoiceChat::IsSpeaking(std::uint64_t guid) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto *s = FindSpeaker(guid);
  return s ? s->speaking : false;
}

void VoiceChat::SetMuted(std::uint64_t guid, bool muted) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto *s = FindSpeaker(guid);
  if (s)
    s->muted = muted;
}

bool VoiceChat::IsMuted(std::uint64_t guid) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto *s = FindSpeaker(guid);
  return s ? s->muted : false;
}

void VoiceChat::SetVolume(std::uint64_t guid, float volume) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto *s = FindSpeaker(guid);
  if (s)
    s->volume = std::clamp(volume, 0.0f, 1.0f);
}

void VoiceChat::SetMasterVolume(float volume) {
  std::lock_guard<std::mutex> lock(mutex_);
  master_volume_ = std::clamp(volume, 0.0f, 1.0f);
}

float VoiceChat::GetMasterVolume() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return master_volume_;
}

void VoiceChat::SetMicrophoneVolume(float volume) {
  std::lock_guard<std::mutex> lock(mutex_);
  microphone_volume_ = std::clamp(volume, 0.0f, 1.0f);
}

float VoiceChat::GetMicrophoneVolume() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return microphone_volume_;
}

void VoiceChat::SetPushToTalk(bool enabled) {
  std::lock_guard<std::mutex> lock(mutex_);
  push_to_talk_ = enabled;
}

bool VoiceChat::IsPushToTalk() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return push_to_talk_;
}

void VoiceChat::Reset(openwow::audio::SoundRuntime& sound_runtime) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    enabled_ = false;
    server_allowed_ = false;
    state_ = VoiceChatState::kDisabled;
    ClearRuntimeStateLocked(true);
    master_volume_ = 1.0f;
    microphone_volume_ = 1.0f;
    push_to_talk_ = false;
    microphone_muted_ = false;
    voice_activated_ = false;
    push_to_talk_key_.clear();
  }

  VoiceChat_ResetComSatRuntimeState(sound_runtime.voice_loopback());
  sound_runtime.ClearBackgroundSoundState();
  WriteEnableVoiceChatSetting(false);
}

void VoiceChat::ShutdownRuntime(openwow::audio::SoundRuntime& sound_runtime) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ClearRuntimeStateLocked(true);
    if (state_ != VoiceChatState::kDisabled) {
      state_ = VoiceChatState::kDisconnected;
    }
  }

  sound_runtime.ClearBackgroundSoundState();
}

void DispatchChannelVoiceUpdateForDisplaySlot(const std::uint32_t slot, const bool voice_enabled,
                                              const bool selected) {
  std::vector<std::string> args;
  args.emplace_back(std::to_string(slot + 1));
  if (voice_enabled) {
    args.emplace_back("1");
    if (selected) {
      args.emplace_back("1");
    }
  }

  ui::game::ScriptEventDispatch::Get().FireGlobalEventWithArgs(
      ui::game::events::CHANNEL_VOICE_UPDATE, args);
}

VoiceDisplaySelectionSnapshot CaptureVoiceDisplaySelectionSnapshot() {
  VoiceDisplaySelectionSnapshot snapshot;

  auto& chat_system = ChatSystem::Get();
  const auto selected_index = chat_system.GetSelectedDisplayChannelIndex();
  if (!selected_index.has_value()) {
    return snapshot;
  }

  snapshot.has_selection = true;
  snapshot.slot = static_cast<std::uint32_t>(*selected_index);
  const auto resolved = chat_system.ResolveDisplayChannel(*selected_index);
  if (!resolved.has_value()) {
    return snapshot;
  }

  snapshot.joined_channel_or_none =
      resolved->kind == DisplayChannelKind::kJoinedChannel ||
      resolved->kind == DisplayChannelKind::kInvalid;
  snapshot.voice_enabled = resolved->kind == DisplayChannelKind::kJoinedChannel &&
                           resolved->channel.has_value() &&
                           resolved->channel->voice_enabled;
  return snapshot;
}

void VoiceChat_NotifyDisplayChannelVoiceAvailable(
    openwow::audio::SoundRuntime& sound_runtime,
    const std::string_view session_name,
    const VoiceChatChannelType type,
    const VoiceDisplaySelectionSnapshot* const previous_selection) {
  if (type == VoiceChatChannelType::kCustom) {
    SetJoinedChannelVoiceEnabled(session_name, true);
  }

  const auto display_slot = FindDisplaySlotForSession(session_name, type);
  if (!display_slot.has_value()) {
    return;
  }

  if (previous_selection != nullptr && type != VoiceChatChannelType::kCustom &&
      IsBackgroundVoiceDisplaySlotSelected(sound_runtime, *display_slot) &&
      (!previous_selection->has_selection || previous_selection->slot != *display_slot)) {
    return;
  }

  DispatchChannelVoiceUpdateForDisplaySlot(*display_slot, true,
                                            IsBackgroundVoiceDisplaySlotSelected(
                                                sound_runtime, *display_slot));
}

void VoiceChat_SyncDisplaySelectionForSessionType(
    WorldSession& session, const VoiceChatChannelType type,
    const VoiceDisplaySelectionSnapshot* const previous_selection) {
  auto& voice_chat = VoiceChat::Get();
  if (!voice_chat.IsEnabledAndActive() || !ReadVoiceAutoJoinSetting(type)) {
    return;
  }

  const auto target_kind = ResolveDisplayChannelKindForSessionType(type);
  if (!target_kind.has_value()) {
    return;
  }

  const VoiceDisplaySelectionSnapshot selection_snapshot =
      previous_selection != nullptr ? *previous_selection : CaptureVoiceDisplaySelectionSnapshot();
  if (selection_snapshot.has_selection && !selection_snapshot.joined_channel_or_none) {
    return;
  }

  auto& chat_system = ChatSystem::Get();
  const auto display_count = chat_system.GetNumDisplayChannels();
  for (std::size_t index = 0; index < display_count; ++index) {
    const auto info = chat_system.GetDisplayChannelInfo(index);
    if (!info.has_value() || info->is_header || info->kind != *target_kind) {
      continue;
    }

    (void)VoiceChat_SelectActiveSessionByChannel(session, type,
                                                 std::string_view{});
    return;
  }
}

}

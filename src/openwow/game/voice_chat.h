#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "openwow/game/object_guid.h"

namespace openwow::game {

enum class DisplayChannelKind : std::uint32_t;
class WorldSession;
}
namespace openwow::audio { class SoundRuntime; }
namespace openwow::game {

enum class VoiceChatState : std::uint8_t {
  kDisabled = 0,
  kDisconnected = 1,
  kConnecting = 2,
  kConnected = 3,
  kInChannel = 4,
};

enum class VoiceChatChannelType : std::uint32_t {
  kParty = 0,
  kRaid = 1,
  kCustom = 2,
  kBattleground = 3,
};

struct VoiceChatChannel {
  std::string channelName;
  VoiceChatChannelType channelType{VoiceChatChannelType::kCustom};
  bool isMuted{false};
  float volume{1.0f};
};

struct VoiceSpeaker {
  std::uint64_t guid{0};
  std::string name;
  float volume{1.0f};
  bool muted{false};
  bool speaking{false};
};

struct VoiceDisplaySelectionSnapshot {
  bool has_selection{false};
  bool joined_channel_or_none{true};
  std::uint32_t slot{0};
  bool voice_enabled{false};
};

struct VoiceSessionRosterMember {
  ObjectGuid guid;
  std::uint8_t status_flags{0};
};

struct VoiceSessionRosterUpdate {
  std::uint64_t session_id{0};
  std::string session_name;
  VoiceChatChannelType channel_type{VoiceChatChannelType::kCustom};
  std::uint32_t member_count{0};
  std::vector<VoiceSessionRosterMember> members;
};

struct VoiceSessionUpdateResult {
  std::vector<ObjectGuid> removed_members;
  bool removed_current_session{false};
  std::optional<std::uint32_t> display_slot;
};

struct VoiceSessionOrdinalInfo {
  std::string session_name;
  VoiceChatChannelType channel_type{VoiceChatChannelType::kCustom};
  bool active{false};
};

struct VoiceSessionSelectionPacket {
  std::uint32_t channel_type{4};
  std::string channel_name;
};

class VoiceChat {
public:
  static VoiceChat &Get();

  void SetEnabled(openwow::audio::SoundRuntime& sound_runtime, bool enabled);
  [[nodiscard]] bool IsEnabled() const;

  void SetServerAllowed(openwow::audio::SoundRuntime& sound_runtime, bool allowed);
  [[nodiscard]] bool IsServerAllowed() const;

  [[nodiscard]] bool IsEnabledAndActive() const;

  [[nodiscard]] bool IsAllowedAndEnabled() const;

  [[nodiscard]] VoiceChatState GetState() const;
  void SetState(VoiceChatState state);

  void SetMicrophoneMuted(bool muted);
  [[nodiscard]] bool IsMicrophoneMuted() const;

  void SetOutputVolume(float vol);
  [[nodiscard]] float GetOutputVolume() const;

  void SetInputVolume(float vol);
  [[nodiscard]] float GetInputVolume() const;

  void SetVoiceActivated(bool activated);
  [[nodiscard]] bool IsVoiceActivated() const;

  void SetPushToTalkKey(const std::string &keyName);
  [[nodiscard]] std::string GetPushToTalkKey() const;

  void JoinChannel(const std::string &name, VoiceChatChannelType type);
  void LeaveChannel(const std::string &name);
  [[nodiscard]] std::vector<VoiceChatChannel> GetChannels() const;
  [[nodiscard]] bool IsInChannel(const std::string &name) const;
  [[nodiscard]] bool HasChannelType(VoiceChatChannelType type) const;
  void MuteChannel(const std::string &name, bool muted);
  [[nodiscard]] bool IsChannelMuted(const std::string &name) const;
  void SetChannelVolume(const std::string &name, float volume);
  void UpsertChannelSession(std::uint64_t session_id, std::string_view name,
                            VoiceChatChannelType type);
  void RemoveChannelSession(std::uint64_t session_id);
  [[nodiscard]] std::uint32_t GetSessionCount() const;
  [[nodiscard]] std::optional<std::uint32_t> GetCurrentSessionOrdinal() const;
  [[nodiscard]] std::optional<std::uint32_t> GetActiveVoiceDisplaySlot() const;
  [[nodiscard]] std::optional<VoiceSessionOrdinalInfo> GetSessionByOrdinal(
      std::uint32_t ordinal) const;
  [[nodiscard]] std::optional<std::uint32_t> GetSessionMemberCountByOrdinal(
      std::uint32_t ordinal) const;
  [[nodiscard]] std::optional<VoiceSessionRosterMember> GetSessionMemberByOrdinal(
      std::uint32_t session_ordinal, std::uint32_t member_ordinal) const;
  [[nodiscard]] std::optional<std::uint8_t> GetSessionMemberStatusFlagsByOrdinal(
      std::uint32_t session_ordinal, ObjectGuid guid) const;
  [[nodiscard]] std::optional<std::uint32_t> GetSessionOrdinalByChannel(
      VoiceChatChannelType type, std::string_view session_name) const;
  [[nodiscard]] std::uint8_t DecorateChannelRosterMemberFlags(
      VoiceChatChannelType type, std::string_view session_name, ObjectGuid guid,
      std::uint8_t base_flags, bool include_silenced_bit) const;
  [[nodiscard]] VoiceSessionSelectionPacket SetCurrentSessionByOrdinal(
      openwow::audio::SoundRuntime& sound_runtime,
      std::optional<std::uint32_t> ordinal);
  void ClearCurrentSessionSelection(openwow::audio::SoundRuntime& sound_runtime);
  VoiceSessionUpdateResult ApplySessionRosterUpdate(
      WorldSession& session, const VoiceSessionRosterUpdate &update);
  VoiceSessionUpdateResult RemoveSessionById(WorldSession& session,
                                             std::uint64_t session_id);
  void SetSessionPlayerMuted(const std::string &session_name, ObjectGuid guid, bool muted);
  [[nodiscard]] bool IsSessionPlayerMuted(std::string_view session_name, ObjectGuid guid) const;
  [[nodiscard]] bool IsPlayerMutedInChannelType(VoiceChatChannelType type, ObjectGuid guid) const;
  [[nodiscard]] bool IsPlayerMutedInCurrentSession(ObjectGuid guid) const;
  void SetSessionPlayerSpeaking(const std::string &session_name, ObjectGuid guid, bool speaking);
  [[nodiscard]] bool IsSessionPlayerSpeaking(std::string_view session_name, ObjectGuid guid) const;
  [[nodiscard]] bool IsPlayerSpeakingInChannelType(VoiceChatChannelType type,
                                                   ObjectGuid guid) const;
  [[nodiscard]] bool IsPlayerSpeakingInCurrentSession(ObjectGuid guid) const;

  void MutePlayer(ObjectGuid guid, bool muted);
  [[nodiscard]] bool IsPlayerMuted(ObjectGuid guid) const;

  void SetSpeaking(ObjectGuid guid, bool speaking);
  [[nodiscard]] bool IsSpeaking(ObjectGuid guid) const;
  [[nodiscard]] bool HasSpeakingPlayers() const;
  [[nodiscard]] std::vector<ObjectGuid> GetSpeakingPlayers() const;

  void JoinVoiceChannel(const std::string &channel_name);
  void LeaveVoiceChannel(openwow::audio::SoundRuntime& sound_runtime);
  [[nodiscard]] std::string GetCurrentChannel() const;
  [[nodiscard]] bool IsInVoiceChannel() const;

  void AddSpeaker(const VoiceSpeaker &speaker);
  void RemoveSpeaker(std::uint64_t guid);
  [[nodiscard]] std::vector<VoiceSpeaker> GetSpeakers() const;
  [[nodiscard]] std::uint32_t GetSpeakerCount() const;

  void SetSpeaking(std::uint64_t guid, bool speaking);
  [[nodiscard]] bool IsSpeaking(std::uint64_t guid) const;

  void SetMuted(std::uint64_t guid, bool muted);
  [[nodiscard]] bool IsMuted(std::uint64_t guid) const;

  void SetVolume(std::uint64_t guid, float volume);

  void SetMasterVolume(float volume);
  [[nodiscard]] float GetMasterVolume() const;

  void SetMicrophoneVolume(float volume);
  [[nodiscard]] float GetMicrophoneVolume() const;

  void SetPushToTalk(bool enabled);
  [[nodiscard]] bool IsPushToTalk() const;

  void Reset(openwow::audio::SoundRuntime& sound_runtime);

  void ShutdownRuntime(openwow::audio::SoundRuntime& sound_runtime);

private:
  VoiceChat() = default;

  void ClearRuntimeStateLocked(bool clear_muted_players);

  VoiceSpeaker *FindSpeaker(std::uint64_t guid);
  const VoiceSpeaker *FindSpeaker(std::uint64_t guid) const;

  VoiceChatChannel *FindChannel(const std::string &name);
  const VoiceChatChannel *FindChannel(const std::string &name) const;
  const VoiceChatChannel *FindChannelByType(VoiceChatChannelType type) const;

  struct VoiceSessionSpeakerState {
    bool muted{false};
    bool speaking{false};
    float volume{1.0f};
  };

  struct ChannelSessionIdentity {
    std::string channel_name;
    VoiceChatChannelType channel_type{VoiceChatChannelType::kCustom};
  };

  struct VoiceSessionSlot {
    std::uint64_t session_id{0};
    std::string session_name;
    VoiceChatChannelType channel_type{VoiceChatChannelType::kCustom};
  };

  using VoiceSessionSpeakerMap = std::unordered_map<std::uint64_t, VoiceSessionSpeakerState>;
  static constexpr std::size_t kMaxVoiceSessionSlots = 32;
  static constexpr std::size_t kInvalidSessionSlot = std::numeric_limits<std::size_t>::max();

  [[nodiscard]] std::optional<std::size_t> FindSessionSlotByIdLocked(std::uint64_t session_id) const;
  [[nodiscard]] std::optional<std::size_t> FindFirstFreeSessionSlotLocked() const;
  [[nodiscard]] std::optional<std::size_t> FindSessionSlotByOrdinalLocked(
      std::uint32_t ordinal) const;
  [[nodiscard]] std::optional<std::uint32_t> GetSessionOrdinalForSlotLocked(
      std::size_t slot_index) const;

  bool enabled_{false};
  bool server_allowed_{false};

  VoiceChatState state_{VoiceChatState::kDisabled};
  std::string current_channel_;
  std::vector<VoiceSpeaker> speakers_;
  float master_volume_{1.0f};
  float microphone_volume_{1.0f};
  bool push_to_talk_{false};

  bool microphone_muted_{false};
  bool voice_activated_{false};
  std::string push_to_talk_key_;
  std::vector<VoiceChatChannel> channels_;
  std::array<std::optional<VoiceSessionSlot>, kMaxVoiceSessionSlots> session_slots_{};
  std::size_t current_session_slot_{kInvalidSessionSlot};
  std::optional<std::uint32_t> active_voice_display_slot_;
  std::unordered_map<std::uint64_t, ChannelSessionIdentity> channel_sessions_;
  std::unordered_map<std::string, std::vector<VoiceSessionRosterMember>> session_rosters_;
  std::unordered_map<std::string, VoiceSessionSpeakerMap> session_speakers_;
  std::unordered_set<std::uint64_t> muted_players_;
  std::unordered_set<std::uint64_t> speaking_players_;

  mutable std::mutex mutex_;
};

void DispatchChannelVoiceUpdateForDisplaySlot(std::uint32_t slot, bool voice_enabled,
                                              bool selected);
[[nodiscard]] VoiceDisplaySelectionSnapshot CaptureVoiceDisplaySelectionSnapshot();
bool VoiceChat_ApplyActiveSessionSelection(
    WorldSession& session, std::optional<std::uint32_t> ordinal);
bool VoiceChat_SelectActiveSessionByChannel(
    WorldSession& session, VoiceChatChannelType type,
    std::string_view session_name);
void VoiceChat_NotifyDisplayChannelVoiceAvailable(
    openwow::audio::SoundRuntime& sound_runtime, std::string_view session_name,
    VoiceChatChannelType type,
    const VoiceDisplaySelectionSnapshot* previous_selection = nullptr);
void VoiceChat_SyncDisplaySelectionForSessionType(
    WorldSession& session, VoiceChatChannelType type,
    const VoiceDisplaySelectionSnapshot* previous_selection = nullptr);

}

#pragma once

#include "openwow/ui/runtime/lua/lua_binding.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <variant>

namespace openwow::audio {
class SoundRuntime;
}

namespace openwow::game {
class WorldSession;
}

namespace openwow::ui::game::runtime {
class MovieRecordingRuntime;
}

namespace openwow::ui::game::detail {

enum class ExternalMusicPlayerCommand : std::uint8_t {
  kPlayPause,
  kVolumeUp,
  kVolumeDown,
  kBackTrack,
  kNextTrack,
};

using ExternalMusicPlayerControlCallback =
    std::function<void(ExternalMusicPlayerCommand command)>;

struct SoundLuaNumber final {
  std::optional<double> value;
};

struct SoundLuaString final {
  std::optional<std::string> value;
};

struct SoundLuaValue final {
  std::optional<double> number;
  std::optional<std::string> string;
};

struct SoundLuaContext final {
  SoundLuaContext(openwow::audio::SoundRuntime& sound,
                  openwow::game::WorldSession* session,
                  const runtime::MovieRecordingRuntime* movie_recording = nullptr) noexcept
      : sound_runtime(sound), world_session(session), movie_recording(movie_recording) {}

  [[nodiscard]] bool CanDispatchScriptAudio() const noexcept;

  openwow::audio::SoundRuntime& sound_runtime;
  openwow::game::WorldSession* world_session;
  const runtime::MovieRecordingRuntime* movie_recording;
};

enum class VoiceSelectorArgumentKind : std::uint8_t {
  kAbsent,
  kString,
  kInvalid,
};

struct VoiceSelectorArgument final {
  VoiceSelectorArgumentKind kind{VoiceSelectorArgumentKind::kAbsent};
  std::string value;
};

using SoundVoidResult = std::variant<openwow::ui::lua::NoLuaResults,
                                     openwow::ui::lua::LuaUsageError>;
using SoundNumberResult =
    std::variant<openwow::ui::lua::NoLuaResults, double,
                 openwow::ui::lua::LuaUsageError>;
using SoundStringResult =
    std::variant<std::string, openwow::ui::lua::LuaUsageError>;
using OptionalNumberResult =
    std::variant<openwow::ui::lua::NoLuaResults, double>;
using OptionalStringResult =
    std::variant<openwow::ui::lua::NoLuaResults, openwow::ui::lua::LuaNil,
                 std::string>;
using OptionalTruthyResult =
    std::variant<openwow::ui::lua::NoLuaResults,
                 openwow::ui::lua::LuaTruthy>;
using VoiceSessionInfoResult = std::variant<
    openwow::ui::lua::LuaNil,
    openwow::ui::lua::LuaReturns<std::string, openwow::ui::lua::LuaTruthy>>;
using VoiceSessionMemberInfoResult = std::variant<
    openwow::ui::lua::NoLuaResults,
    openwow::ui::lua::LuaReturns<std::string, openwow::ui::lua::LuaTruthy,
                                 openwow::ui::lua::LuaTruthy,
                                 openwow::ui::lua::LuaTruthy,
                                 openwow::ui::lua::LuaTruthy>>;

void SetExternalMusicPlayerControlCallback(ExternalMusicPlayerControlCallback callback);

void BindSoundLuaContext(lua_State& state, SoundLuaContext& context);
SoundVoidResult PlaySound(SoundLuaContext& context, SoundLuaValue sound);
SoundNumberResult PlaySoundFile(SoundLuaContext& context, SoundLuaString path);
SoundNumberResult PlayMusic(SoundLuaContext& context, SoundLuaString path);
void StopMusic(SoundLuaContext& context);
void MusicPlayerPlayPause();
void MusicPlayerVolumeUp();
void MusicPlayerVolumeDown();
void MusicPlayerBackTrack();
void MusicPlayerNextTrack();
int SoundGameSystemGetNumOutputDrivers(SoundLuaContext& context);
SoundStringResult SoundGameSystemGetOutputDriverNameByIndex(
    SoundLuaContext& context, SoundLuaNumber index);
void SoundGameSystemRestartSoundSystem(SoundLuaContext& context);
SoundStringResult SoundChatSystemGetInputDriverNameByIndex(
    SoundLuaContext& context, SoundLuaNumber index);
int SoundChatSystemGetNumInputDrivers(SoundLuaContext& context);
int SoundChatSystemGetNumOutputDrivers(SoundLuaContext& context);
SoundStringResult SoundChatSystemGetOutputDriverNameByIndex(
    SoundLuaContext& context, SoundLuaNumber index);
SoundStringResult SoundGameSystemGetInputDriverNameByIndex(
    SoundLuaContext& context, SoundLuaNumber index);
int SoundGameSystemGetNumInputDrivers(SoundLuaContext& context);
std::optional<double> GetNumChannelMembers(SoundLuaContext& context,
                                           SoundLuaNumber display_index);
int GetNumVoiceSessions();
std::optional<double> GetActiveVoiceChannel();
void SetActiveVoiceChannel(SoundLuaContext& context,
                           SoundLuaNumber display_index);
std::optional<double> GetVoiceCurrentSessionID();
VoiceSessionInfoResult GetVoiceSessionInfo(SoundLuaNumber session_id);
OptionalNumberResult GetNumVoiceSessionMembersBySessionID(
    SoundLuaNumber session_id);
VoiceSessionMemberInfoResult GetVoiceSessionMemberInfoBySessionID(
    SoundLuaContext& context, SoundLuaNumber session_id,
    SoundLuaNumber member_id);
openwow::ui::lua::LuaTruthy SetActiveVoiceChannelBySessionID(
    SoundLuaContext& context, SoundLuaNumber session_id);
OptionalTruthyResult UnitIsSilenced(SoundLuaContext& context,
                                    SoundLuaString target,
                                    VoiceSelectorArgument selector);
OptionalTruthyResult GetMuteStatus(SoundLuaContext& context,
                                   SoundLuaString target,
                                   SoundLuaString selector);
OptionalTruthyResult GetVoiceStatus(SoundLuaContext& context,
                                    SoundLuaString target,
                                    SoundLuaString selector);
openwow::ui::lua::LuaTruthy IsVoiceChatEnabled();
void VoiceChatStopPlayingLoopbackSound(SoundLuaContext& context);
void VoiceChatStartCapture(SoundLuaContext& context);
void VoiceChatStopCapture(SoundLuaContext& context);
void VoiceChatStopRecordingLoopbackSound(SoundLuaContext& context);
SoundVoidResult VoiceChatRecordLoopbackSound(
    SoundLuaContext& context, SoundLuaNumber max_seconds);
void VoiceChatPlayLoopbackSound(SoundLuaContext& context);
double VoiceChatIsRecordingLoopbackSound(SoundLuaContext& context);
double VoiceChatIsPlayingLoopbackSound(SoundLuaContext& context);
double VoiceChatGetCurrentMicrophoneSignalLevel(SoundLuaContext& context);
void VoiceChatActivatePrimaryCaptureCallback(SoundLuaContext& context);
OptionalStringResult VoiceEnumerateOutputDevices(
    SoundLuaContext& context, SoundLuaNumber index);
OptionalStringResult VoiceEnumerateCaptureDevices(
    SoundLuaContext& context, SoundLuaNumber index);
void VoiceSelectOutputDevice(SoundLuaNumber index);
void VoiceSelectCaptureDevice(SoundLuaNumber index);
std::optional<std::string> VoiceGetCurrentOutputDevice(
    SoundLuaContext& context);
std::optional<std::string> VoiceGetCurrentCaptureDevice(
    SoundLuaContext& context);
void VoicePushToTalkStart(SoundLuaContext& context);
void VoicePushToTalkStop(SoundLuaContext& context);
openwow::ui::lua::LuaTruthy IsVoiceChatAllowed();
openwow::ui::lua::LuaTruthy IsVoiceChatAllowedByServer();
openwow::ui::lua::LuaTruthy VoiceIsDisabledByClient();
std::optional<openwow::ui::lua::LuaTruthy> UnitIsTalking(
    SoundLuaContext& context, SoundLuaString target);

}

namespace openwow::ui::lua {

template <>
struct LuaRegistryContext<openwow::ui::game::detail::SoundLuaContext> {
  static constexpr std::string_view key = "openwow.sound_lua_context";
};

template <ConversionPolicy Policy>
struct LuaConverter<openwow::ui::game::detail::SoundLuaNumber, Policy> {
  using Storage = openwow::ui::game::detail::SoundLuaNumber;
  static bool Valid(lua_State*, int) noexcept;
  static Storage Read(lua_State* state, int index) noexcept;
  static Storage Argument(Storage value) noexcept;
};

template <ConversionPolicy Policy>
struct LuaConverter<openwow::ui::game::detail::SoundLuaString, Policy> {
  using Storage = openwow::ui::game::detail::SoundLuaString;
  static bool Valid(lua_State*, int) noexcept;
  static Storage Read(lua_State* state, int index);
  static Storage Argument(Storage value) noexcept;
};

template <ConversionPolicy Policy>
struct LuaConverter<openwow::ui::game::detail::SoundLuaValue, Policy> {
  using Storage = openwow::ui::game::detail::SoundLuaValue;
  static bool Valid(lua_State*, int) noexcept;
  static Storage Read(lua_State* state, int index);
  static Storage Argument(Storage value) noexcept;
};

template <ConversionPolicy Policy>
struct LuaConverter<openwow::ui::game::detail::VoiceSelectorArgument, Policy> {
  using Storage = openwow::ui::game::detail::VoiceSelectorArgument;
  static bool Valid(lua_State*, int) noexcept;
  static Storage Read(lua_State* state, int index);
  static Storage Argument(Storage value) noexcept;
};

}

namespace openwow::ui::lua::detail {

template <>
struct IsOptional<openwow::ui::game::detail::SoundLuaNumber> : std::true_type {};
template <>
struct IsOptional<openwow::ui::game::detail::SoundLuaString> : std::true_type {};
template <>
struct IsOptional<openwow::ui::game::detail::SoundLuaValue> : std::true_type {};
template <>
struct IsOptional<openwow::ui::game::detail::VoiceSelectorArgument>
    : std::true_type {};

}

namespace openwow::ui::game::detail {

inline constexpr openwow::ui::lua::ConversionPolicy kSoundLuaConversion{
    openwow::ui::lua::IntegralConversion::kTruncate,
    true, true, true, true, true};

inline constexpr auto kPlaySound =
    openwow::ui::lua::bind<&PlaySound, kSoundLuaConversion>("PlaySound");
inline constexpr auto kPlaySoundFile =
    openwow::ui::lua::bind<&PlaySoundFile, kSoundLuaConversion>("PlaySoundFile");
inline constexpr auto kPlayMusic =
    openwow::ui::lua::bind<&PlayMusic, kSoundLuaConversion>("PlayMusic");
inline constexpr auto kStopMusic =
    openwow::ui::lua::bind<&StopMusic, kSoundLuaConversion>("StopMusic");
inline constexpr auto kMusicPlayer_PlayPause =
    openwow::ui::lua::bind<&MusicPlayerPlayPause, kSoundLuaConversion>(
        "MusicPlayer_PlayPause");
inline constexpr auto kMusicPlayer_VolumeUp =
    openwow::ui::lua::bind<&MusicPlayerVolumeUp, kSoundLuaConversion>(
        "MusicPlayer_VolumeUp");
inline constexpr auto kMusicPlayer_VolumeDown =
    openwow::ui::lua::bind<&MusicPlayerVolumeDown, kSoundLuaConversion>(
        "MusicPlayer_VolumeDown");
inline constexpr auto kMusicPlayer_BackTrack =
    openwow::ui::lua::bind<&MusicPlayerBackTrack, kSoundLuaConversion>(
        "MusicPlayer_BackTrack");
inline constexpr auto kMusicPlayer_NextTrack =
    openwow::ui::lua::bind<&MusicPlayerNextTrack, kSoundLuaConversion>(
        "MusicPlayer_NextTrack");
inline constexpr auto kSound_GameSystem_GetNumOutputDrivers =
    openwow::ui::lua::bind<&SoundGameSystemGetNumOutputDrivers,
                           kSoundLuaConversion>(
        "Sound_GameSystem_GetNumOutputDrivers");
inline constexpr auto kSound_GameSystem_GetOutputDriverNameByIndex =
    openwow::ui::lua::bind<&SoundGameSystemGetOutputDriverNameByIndex,
                           kSoundLuaConversion>(
        "Sound_GameSystem_GetOutputDriverNameByIndex");
inline constexpr auto kSound_GameSystem_RestartSoundSystem =
    openwow::ui::lua::bind<&SoundGameSystemRestartSoundSystem,
                           kSoundLuaConversion>(
        "Sound_GameSystem_RestartSoundSystem");
inline constexpr auto kSound_ChatSystem_GetInputDriverNameByIndex =
    openwow::ui::lua::bind<&SoundChatSystemGetInputDriverNameByIndex,
                           kSoundLuaConversion>(
        "Sound_ChatSystem_GetInputDriverNameByIndex");
inline constexpr auto kSound_ChatSystem_GetNumInputDrivers =
    openwow::ui::lua::bind<&SoundChatSystemGetNumInputDrivers,
                           kSoundLuaConversion>(
        "Sound_ChatSystem_GetNumInputDrivers");
inline constexpr auto kSound_ChatSystem_GetNumOutputDrivers =
    openwow::ui::lua::bind<&SoundChatSystemGetNumOutputDrivers,
                           kSoundLuaConversion>(
        "Sound_ChatSystem_GetNumOutputDrivers");
inline constexpr auto kSound_ChatSystem_GetOutputDriverNameByIndex =
    openwow::ui::lua::bind<&SoundChatSystemGetOutputDriverNameByIndex,
                           kSoundLuaConversion>(
        "Sound_ChatSystem_GetOutputDriverNameByIndex");
inline constexpr auto kSound_GameSystem_GetInputDriverNameByIndex =
    openwow::ui::lua::bind<&SoundGameSystemGetInputDriverNameByIndex,
                           kSoundLuaConversion>(
        "Sound_GameSystem_GetInputDriverNameByIndex");
inline constexpr auto kSound_GameSystem_GetNumInputDrivers =
    openwow::ui::lua::bind<&SoundGameSystemGetNumInputDrivers,
                           kSoundLuaConversion>(
        "Sound_GameSystem_GetNumInputDrivers");
inline constexpr auto kGetNumChannelMembers =
    openwow::ui::lua::bind<&GetNumChannelMembers, kSoundLuaConversion>(
        "GetNumChannelMembers");
inline constexpr auto kGetNumVoiceSessions =
    openwow::ui::lua::bind<&GetNumVoiceSessions, kSoundLuaConversion>(
        "GetNumVoiceSessions");
inline constexpr auto kGetActiveVoiceChannel =
    openwow::ui::lua::bind<&GetActiveVoiceChannel, kSoundLuaConversion>(
        "GetActiveVoiceChannel");
inline constexpr auto kSetActiveVoiceChannel =
    openwow::ui::lua::bind<&SetActiveVoiceChannel, kSoundLuaConversion>(
        "SetActiveVoiceChannel");
inline constexpr auto kGetVoiceCurrentSessionID =
    openwow::ui::lua::bind<&GetVoiceCurrentSessionID, kSoundLuaConversion>(
        "GetVoiceCurrentSessionID");
inline constexpr auto kGetVoiceSessionInfo =
    openwow::ui::lua::bind<&GetVoiceSessionInfo, kSoundLuaConversion>(
        "GetVoiceSessionInfo");
inline constexpr auto kGetNumVoiceSessionMembersBySessionID =
    openwow::ui::lua::bind<&GetNumVoiceSessionMembersBySessionID,
                           kSoundLuaConversion>(
        "GetNumVoiceSessionMembersBySessionID");
inline constexpr auto kGetVoiceSessionMemberInfoBySessionID =
    openwow::ui::lua::bind<&GetVoiceSessionMemberInfoBySessionID,
                           kSoundLuaConversion>(
        "GetVoiceSessionMemberInfoBySessionID");
inline constexpr auto kSetActiveVoiceChannelBySessionID =
    openwow::ui::lua::bind<&SetActiveVoiceChannelBySessionID,
                           kSoundLuaConversion>(
        "SetActiveVoiceChannelBySessionID");
inline constexpr auto kUnitIsSilenced =
    openwow::ui::lua::bind<&UnitIsSilenced, kSoundLuaConversion>(
        "UnitIsSilenced");
inline constexpr auto kGetMuteStatus =
    openwow::ui::lua::bind<&GetMuteStatus, kSoundLuaConversion>(
        "GetMuteStatus");
inline constexpr auto kGetVoiceStatus =
    openwow::ui::lua::bind<&GetVoiceStatus, kSoundLuaConversion>(
        "GetVoiceStatus");
inline constexpr auto kIsVoiceChatEnabled =
    openwow::ui::lua::bind<&IsVoiceChatEnabled, kSoundLuaConversion>(
        "IsVoiceChatEnabled");
inline constexpr auto kVoiceChat_StopPlayingLoopbackSound =
    openwow::ui::lua::bind<&VoiceChatStopPlayingLoopbackSound,
                           kSoundLuaConversion>(
        "VoiceChat_StopPlayingLoopbackSound");
inline constexpr auto kVoiceChat_StartCapture =
    openwow::ui::lua::bind<&VoiceChatStartCapture, kSoundLuaConversion>(
        "VoiceChat_StartCapture");
inline constexpr auto kVoiceChat_StopCapture =
    openwow::ui::lua::bind<&VoiceChatStopCapture, kSoundLuaConversion>(
        "VoiceChat_StopCapture");
inline constexpr auto kVoiceChat_StopRecordingLoopbackSound =
    openwow::ui::lua::bind<&VoiceChatStopRecordingLoopbackSound,
                           kSoundLuaConversion>(
        "VoiceChat_StopRecordingLoopbackSound");
inline constexpr auto kVoiceChat_RecordLoopbackSound =
    openwow::ui::lua::bind<&VoiceChatRecordLoopbackSound,
                           kSoundLuaConversion>(
        "VoiceChat_RecordLoopbackSound");
inline constexpr auto kVoiceChat_PlayLoopbackSound =
    openwow::ui::lua::bind<&VoiceChatPlayLoopbackSound,
                           kSoundLuaConversion>(
        "VoiceChat_PlayLoopbackSound");
inline constexpr auto kVoiceChat_IsRecordingLoopbackSound =
    openwow::ui::lua::bind<&VoiceChatIsRecordingLoopbackSound,
                           kSoundLuaConversion>(
        "VoiceChat_IsRecordingLoopbackSound");
inline constexpr auto kVoiceChat_IsPlayingLoopbackSound =
    openwow::ui::lua::bind<&VoiceChatIsPlayingLoopbackSound,
                           kSoundLuaConversion>(
        "VoiceChat_IsPlayingLoopbackSound");
inline constexpr auto kVoiceChat_GetCurrentMicrophoneSignalLevel =
    openwow::ui::lua::bind<&VoiceChatGetCurrentMicrophoneSignalLevel,
                           kSoundLuaConversion>(
        "VoiceChat_GetCurrentMicrophoneSignalLevel");
inline constexpr auto kVoiceChat_ActivatePrimaryCaptureCallback =
    openwow::ui::lua::bind<&VoiceChatActivatePrimaryCaptureCallback,
                           kSoundLuaConversion>(
        "VoiceChat_ActivatePrimaryCaptureCallback");
inline constexpr auto kVoiceEnumerateOutputDevices =
    openwow::ui::lua::bind<&VoiceEnumerateOutputDevices,
                           kSoundLuaConversion>(
        "VoiceEnumerateOutputDevices");
inline constexpr auto kVoiceEnumerateCaptureDevices =
    openwow::ui::lua::bind<&VoiceEnumerateCaptureDevices,
                           kSoundLuaConversion>(
        "VoiceEnumerateCaptureDevices");
inline constexpr auto kVoiceSelectOutputDevice =
    openwow::ui::lua::bind<&VoiceSelectOutputDevice, kSoundLuaConversion>(
        "VoiceSelectOutputDevice");
inline constexpr auto kVoiceSelectCaptureDevice =
    openwow::ui::lua::bind<&VoiceSelectCaptureDevice, kSoundLuaConversion>(
        "VoiceSelectCaptureDevice");
inline constexpr auto kVoiceGetCurrentOutputDevice =
    openwow::ui::lua::bind<&VoiceGetCurrentOutputDevice,
                           kSoundLuaConversion>(
        "VoiceGetCurrentOutputDevice");
inline constexpr auto kVoiceGetCurrentCaptureDevice =
    openwow::ui::lua::bind<&VoiceGetCurrentCaptureDevice,
                           kSoundLuaConversion>(
        "VoiceGetCurrentCaptureDevice");
inline constexpr auto kVoicePushToTalkStart =
    openwow::ui::lua::bind<&VoicePushToTalkStart, kSoundLuaConversion>(
        "VoicePushToTalkStart");
inline constexpr auto kVoicePushToTalkStop =
    openwow::ui::lua::bind<&VoicePushToTalkStop, kSoundLuaConversion>(
        "VoicePushToTalkStop");
inline constexpr auto kIsVoiceChatAllowed =
    openwow::ui::lua::bind<&IsVoiceChatAllowed, kSoundLuaConversion>(
        "IsVoiceChatAllowed");
inline constexpr auto kIsVoiceChatAllowedByServer =
    openwow::ui::lua::bind<&IsVoiceChatAllowedByServer,
                           kSoundLuaConversion>(
        "IsVoiceChatAllowedByServer");
inline constexpr auto kVoiceIsDisabledByClient =
    openwow::ui::lua::bind<&VoiceIsDisabledByClient, kSoundLuaConversion>(
        "VoiceIsDisabledByClient");
inline constexpr auto kUnitIsTalking =
    openwow::ui::lua::bind<&UnitIsTalking, kSoundLuaConversion>(
        "UnitIsTalking");

}

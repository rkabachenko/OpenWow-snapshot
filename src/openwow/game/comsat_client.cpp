
#include "openwow/game/comsat_client.h"
#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/audio/playback/sound_engine.h"
#include "openwow/audio/voice/voice_chat_loopback.h"
#include "openwow/audio/voice/voice_chat_audio_setup.h"
#include "openwow/core/decimal_parse.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/core/storm_sync.h"
#include "openwow/game/comsat_sound_io.h"
#include "openwow/game/comsat_sound_output_channel.h"
#include "openwow/game/actions/bindings/adapters/retail/modified_click_adapter.h"
#include "openwow/game/localization.h"
#include "openwow/game/voice_chat.h"
#include "openwow/game/world_session.h"
#include "openwow/input/input_manager.h"
#include "openwow/net/client_services_packet_sender.h"
#include "openwow/network/protocol/wotlk/world_packet.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#include <intrin.h>
#elif defined(__i386__) || defined(__x86_64__)
#include <cpuid.h>
#endif

namespace openwow::game {

namespace {

constexpr float kMillisecondsToSeconds = 0.001f;
constexpr std::uint32_t kComSatShutdownGracePeriodMs = 3000u;

constexpr std::uint32_t kComSatDriverSlotTableCapacity = 4u;
constexpr float kComSatDriverInitialMasterVolume = 1.0f;
constexpr std::uint16_t kComSatDriverLocalBindPort = 0u;
constexpr char kEnableVoiceChatCVarName[] = "EnableVoiceChat";
constexpr char kEnableMicrophoneCVarName[] = "EnableMicrophone";
constexpr char kVoiceChatModeCVarName[] = "VoiceChatMode";
constexpr char kPushToTalkButtonCVarName[] = "PushToTalkButton";
constexpr char kVoiceActivationSensitivityCVarName[] = "VoiceActivationSensitivity";
constexpr char kVoiceChatOutputDriverIndexCVarName[] = "Sound_VoiceChatOutputDriverIndex";
constexpr char kVoiceChatInputDriverIndexCVarName[] = "Sound_VoiceChatInputDriverIndex";

class ScopedComSatLock {
public:
  explicit ScopedComSatLock(openwow::core::SCritSect &crit_sect)
      : crit_sect_(crit_sect) {
    crit_sect_.Enter();
  }

  ~ScopedComSatLock() { crit_sect_.Leave(); }

  ScopedComSatLock(const ScopedComSatLock &) = delete;
  ScopedComSatLock &operator=(const ScopedComSatLock &) = delete;

private:
  openwow::core::SCritSect &crit_sect_;
};

enum class ComSatEventType : std::uint32_t {
  kLocalTalkerStart = 0,
  kLocalTalkerStop = 1,
  kRemoteTalkerStart = 2,
  kRemoteTalkerStop = 3,
};

struct VoiceDuckingCategory {
  const char *group_name;
  const char *base_volume_cvar;
  const char *duck_volume_cvar;
};

constexpr std::array<VoiceDuckingCategory, 3> kVoiceDuckingCategories{{
    {"SFX", "Sound_SFXVolume", "ChatSoundVolume"},
    {"MUSIC", "Sound_MusicVolume", "ChatMusicVolume"},
    {"AMBIENCE", "Sound_AmbienceVolume", "ChatAmbienceVolume"},
}};

struct VoiceDuckingChannelBinding {
  float VolumeDuckingState::*start_volume;
  float VolumeDuckingState::*delta;
};

constexpr std::array<VoiceDuckingChannelBinding, 3> kVoiceDuckingChannelBindings{{
    {&VolumeDuckingState::sound_vol, &VolumeDuckingState::sound_delta},
    {&VolumeDuckingState::music_vol, &VolumeDuckingState::music_delta},
    {&VolumeDuckingState::ambience_vol, &VolumeDuckingState::ambience_delta},
}};

struct ComSatSoundRuntime {
  void Reset() {
    sound_io = ComSatSoundIOState{};
    output_channel.reset();
    datagram_socket.reset();
    driver_created = false;
    datagram_socket_bound = false;
  }

  void Initialize(openwow::audio::SoundEngine &engine) {
    Reset();
    ComSatSoundIO_Initialize(sound_io, kComSatDriverSlotTableCapacity,
                             kComSatDriverInitialMasterVolume);
    output_channel = ComSatSoundOutputChannel_Create(
        engine, kComSatDriverSlotTableCapacity);
    if (output_channel != nullptr && output_channel->Initialize(
          0, 0,
          static_cast<float>(openwow::audio::VoiceChatLoopback::kSampleRateHz),
          16u, 1u)) {
      ComSatSoundIO_SetOutputChannel(sound_io, output_channel.get());
    } else {
      output_channel.reset();
    }
    datagram_socket = ComSatSoundIO_CreateSocketWrapper();
    if (datagram_socket != nullptr) {
      datagram_socket_bound = datagram_socket->Bind(kComSatDriverLocalBindPort);
    }
    driver_created = output_channel != nullptr;
  }

  [[nodiscard]] bool datagram_socket_open() const {
    return datagram_socket != nullptr && datagram_socket->IsOpen();
  }

  bool driver_created{false};
  bool datagram_socket_bound{false};
  std::unique_ptr<ComSatDatagramSocket> datagram_socket;
  std::unique_ptr<ComSatSoundOutputChannel> output_channel;
  ComSatSoundIOState sound_io;
};

bool ParseVoiceToggleValue(const std::string_view value) {
  return openwow::core::ParseSignedDecimalLikeSub76F0D0(value) != 0u;
}

bool ReadVoiceChatBoolCVar(const char *name, const bool fallback = false) {
  auto &cvars = openwow::ui::game::CVarSystem::Instance();
  if (!cvars.Exists(name)) {
    return fallback;
  }

  return ParseVoiceToggleValue(cvars.GetCVar(name));
}

void SyncVoiceChatModeFromCVarValue(const std::string_view value) {
  auto &voice_chat = VoiceChat::Get();
  const bool voice_activated = ParseVoiceToggleValue(value);
  voice_chat.SetVoiceActivated(voice_activated);
  voice_chat.SetPushToTalk(!voice_activated);
}

void SyncPushToTalkBindingFromCVarValue(const std::string_view value) {
  VoiceChat::Get().SetPushToTalkKey(std::string(value));
}

void SyncVoiceChatSettingsFromCVars() {
  auto &cvars = openwow::ui::game::CVarSystem::Instance();
  if (cvars.Exists(kVoiceChatModeCVarName)) {
    SyncVoiceChatModeFromCVarValue(cvars.GetCVar(kVoiceChatModeCVarName));
  }
  if (cvars.Exists(kPushToTalkButtonCVarName)) {
    SyncPushToTalkBindingFromCVarValue(cvars.GetCVar(kPushToTalkButtonCVarName));
  }
}

int ParseVoiceDriverIndexValue(const std::string_view value) {
  return static_cast<int>(openwow::core::ParseSignedDecimalLikeSub76F0D0(value));
}

bool IsVoiceChatSoundRuntimeActive(openwow::audio::SoundEngine& engine) {
  return engine.IsVoiceChatEnabled();
}

std::string ResolveSelectedVoiceOutputDeviceName(
    openwow::audio::SoundEngine& engine, const int driver_index) {
  if (driver_index == 0) {
    return engine.GetEnumeratedDefaultVoiceOutputDeviceName();
  }

  if (engine.GetVoiceOutputDeviceCount() == 0) {
    engine.EnumerateDevices();
  }
  return driver_index >= 0 && driver_index < engine.GetVoiceOutputDeviceCount()
             ? engine.GetVoiceOutputDeviceName(driver_index)
             : std::string{};
}

std::string ResolveSelectedVoiceInputDeviceName(
    openwow::audio::SoundEngine& engine, const int driver_index) {
  if (driver_index == 0) {
    return engine.GetEnumeratedDefaultInputDeviceName();
  }

  if (engine.GetInputDeviceCount() == 0) {
    engine.EnumerateDevices();
  }
  return driver_index >= 0 && driver_index < engine.GetInputDeviceCount()
             ? engine.GetInputDeviceName(driver_index)
             : std::string{};
}

void RestartActiveVoiceChatSoundRuntime(openwow::audio::SoundEngine& engine) {
  if (!IsVoiceChatSoundRuntimeActive(engine)) {
    return;
  }

  const bool voice_enabled = ReadVoiceChatBoolCVar(kEnableVoiceChatCVarName);
  const bool microphone_enabled = ReadVoiceChatBoolCVar(kEnableMicrophoneCVarName, true);

  engine.ShutdownVoiceChat();
  VoiceChat_InitComSatDriver(engine);

  if (voice_enabled && microphone_enabled && !VoiceChat_IsDisabled()) {
    openwow::audio::VoiceChat_SetCaptureEnabled(engine, true);
  }
}

void OnVoiceChatOutputDriverIndexChanged(openwow::audio::SoundEngine& engine,
                                         const std::string &old_value,
                                         const std::string &new_value) {
  const int old_index = ParseVoiceDriverIndexValue(old_value);
  const int new_index = ParseVoiceDriverIndexValue(new_value);
  if (old_index == new_index || !IsVoiceChatSoundRuntimeActive(engine)) {
    return;
  }

  const std::string selected_device_name =
      ResolveSelectedVoiceOutputDeviceName(engine, new_index);
  if (new_index == 0 || !selected_device_name.empty()) {
    engine.SetCurrentVoiceOutputDeviceName(selected_device_name);
  }

  RestartActiveVoiceChatSoundRuntime(engine);
}

void OnVoiceChatInputDriverIndexChanged(openwow::audio::SoundEngine& engine,
                                        const std::string &old_value,
                                        const std::string &new_value) {
  const int old_index = ParseVoiceDriverIndexValue(old_value);
  const int new_index = ParseVoiceDriverIndexValue(new_value);
  if (old_index == new_index || !IsVoiceChatSoundRuntimeActive(engine)) {
    return;
  }

  const std::string selected_device_name =
      ResolveSelectedVoiceInputDeviceName(engine, new_index);
  if (new_index == 0 || !selected_device_name.empty()) {
    engine.SetCurrentInputDeviceName(selected_device_name);
  }

  RestartActiveVoiceChatSoundRuntime(engine);
}

bool SupportsVoiceChatCpuRequirements() {
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
  int cpu_info[4]{};
  __cpuid(cpu_info, 1);
  return (cpu_info[3] & (1 << 25)) != 0;
#elif defined(__i386__) || defined(__x86_64__)
  unsigned int eax = 0;
  unsigned int ebx = 0;
  unsigned int ecx = 0;
  unsigned int edx = 0;
  if (__get_cpuid(1, &eax, &ebx, &ecx, &edx) == 0) {
    return false;
  }
  return (edx & bit_SSE) != 0;
#else
  return true;
#endif
}

void LogVoiceChatDisabledBanner(openwow::audio::SoundEngine& engine,
                                const char *message) {
  openwow::audio::SoundEngine_LogError(
      engine, 0, " -###########################################################################################");
  openwow::audio::SoundEngine_LogError(engine, 0, "%s", message);
}

void LogVoiceChatDisabledFooter(openwow::audio::SoundEngine& engine,
                                const char *message) {
  openwow::audio::SoundEngine_LogError(engine, 0, "%s", message);
  openwow::audio::SoundEngine_LogError(
      engine, 0, " -###########################################################################################");
}

bool SendVoiceChatEnablePacket(const bool voice_enabled,
                               const bool microphone_enabled) {
  std::vector<std::uint8_t> payload{
      static_cast<std::uint8_t>(voice_enabled ? 1 : 0),
      static_cast<std::uint8_t>(microphone_enabled ? 1 : 0),
  };
  return openwow::net::ClientServices__SendPacket(
      openwow::net::wotlk::WorldPacket(
          openwow::net::wotlk::Opcode::CMSG_VOICE_SESSION_ENABLE,
          std::move(payload)));
}

}

bool ReadVoiceChatCVarBool(const char *name, const bool fallback) {
  auto &cvars = openwow::ui::game::CVarSystem::Instance();
  if (!cvars.Exists(name)) {
    return fallback;
  }

  return cvars.GetCVarFloat(name) != 0.0f;
}

namespace {

const VoiceDuckingCategory *ResolveVoiceDuckingCategory(const int index) {
  if (index < 0 || index >= static_cast<int>(kVoiceDuckingCategories.size())) {
    return nullptr;
  }

  return &kVoiceDuckingCategories[static_cast<std::size_t>(index)];
}

float ReadCVarFloatOrFallback(const char *name, const float fallback, bool *exists = nullptr) {
  auto &cvars = openwow::ui::game::CVarSystem::Instance();
  const bool has_value = cvars.Exists(name);
  if (exists) {
    *exists = has_value;
  }

  return has_value ? cvars.GetCVarFloat(name) : fallback;
}

float GetVoiceDuckingCategoryVolume(openwow::audio::SoundEngine& engine,
                                    const int index) {
  const auto *category = ResolveVoiceDuckingCategory(index);
  if (!category) {
    return 1.0f;
  }

  return engine.GetChannelGroupCompositeVolume(category->group_name);
}

void SetVoiceDuckingCategoryVolume(openwow::audio::SoundEngine& engine,
                                   const int index, const float volume) {
  const auto *category = ResolveVoiceDuckingCategory(index);
  if (!category) {
    return;
  }

  engine.SetChannelGroupVolume(category->group_name, volume);
}

float ReadVoiceDuckingTargetVolume(const int index, bool *exists = nullptr) {
  const auto *category = ResolveVoiceDuckingCategory(index);
  if (!category) {
    if (exists) {
      *exists = false;
    }
    return 0.0f;
  }

  bool has_base = false;
  const float base_volume = ReadCVarFloatOrFallback(category->base_volume_cvar, 0.0f, &has_base);
  bool has_duck = false;
  const float duck_volume = ReadCVarFloatOrFallback(category->duck_volume_cvar, 0.0f, &has_duck);
  if (exists) {
    *exists = has_base && has_duck;
  }

  return base_volume * duck_volume;
}

float ReadVoiceDuckingBaseVolume(const int index, bool *exists = nullptr) {
  const auto *category = ResolveVoiceDuckingCategory(index);
  if (!category) {
    if (exists) {
      *exists = false;
    }
    return 0.0f;
  }

  return ReadCVarFloatOrFallback(category->base_volume_cvar, 0.0f, exists);
}

std::optional<std::uint32_t> ResolveVoicePushToTalkScancode(const int key_code) {
  switch (key_code) {
  case 0:
    return 225;
  case 1:
    return 229;
  case 2:
    return 224;
  case 3:
    return 228;
  case 4:
    return 226;
  case 5:
    return 230;
  case 32:
    return 44;
  case 256:
    return 98;
  case 257:
    return 89;
  case 258:
    return 90;
  case 259:
    return 91;
  case 260:
    return 92;
  case 261:
    return 93;
  case 262:
    return 94;
  case 263:
    return 95;
  case 264:
    return 96;
  case 265:
    return 97;
  case 266:
    return 87;
  case 267:
    return 86;
  case 268:
    return 85;
  case 269:
    return 84;
  case 270:
    return 99;
  case 512:
    return 41;
  case 513:
    return 40;
  case 514:
    return 42;
  case 515:
    return 43;
  case 516:
    return 80;
  case 517:
    return 82;
  case 518:
    return 79;
  case 519:
    return 81;
  case 520:
    return 73;
  case 521:
    return 76;
  case 522:
    return 74;
  case 523:
    return 77;
  case 524:
    return 75;
  case 525:
    return 78;
  case 526:
    return 57;
  case 527:
    return 83;
  case 530:
    return 70;
  default:
    break;
  }

  if (key_code >= 768 && key_code <= 779) {
    return static_cast<std::uint32_t>(58 + (key_code - 768));
  }

  if (key_code >= 'A' && key_code <= 'Z') {
    return static_cast<std::uint32_t>(4 + (key_code - 'A'));
  }

  if (key_code >= 'a' && key_code <= 'z') {
    return static_cast<std::uint32_t>(4 + (key_code - 'a'));
  }

  switch (key_code) {
  case '1':
    return 30;
  case '2':
    return 31;
  case '3':
    return 32;
  case '4':
    return 33;
  case '5':
    return 34;
  case '6':
    return 35;
  case '7':
    return 36;
  case '8':
    return 37;
  case '9':
    return 38;
  case '0':
    return 39;
  case '-':
    return 45;
  case '=':
    return 46;
  case '[':
    return 47;
  case ']':
    return 48;
  case '\\':
    return 49;
  case ';':
    return 51;
  case '\'':
    return 52;
  case '`':
    return 53;
  case ',':
    return 54;
  case '.':
    return 55;
  case '/':
    return 56;
  default:
    return std::nullopt;
  }
}

bool IsVoicePushToTalkKeyPressed(const int key_code) {
  const auto scancode = ResolveVoicePushToTalkScancode(key_code);
  if (!scancode.has_value()) {
    return false;
  }

  return input::InputManager::Get().IsKeyDown(*scancode);
}

bool IsVoicePushToTalkMouseButtonPressed(const int button_mask) {
  return button_mask > 0 &&
         input::InputManager::Get().IsMouseButtonFlagDown(
             static_cast<std::uint32_t>(button_mask));
}

void ApplyInterpolatedDuckingVolumes(openwow::audio::SoundEngine& engine,
                                     const VolumeDuckingState &state,
                                     const float progress) {
  for (std::size_t index = 0; index < kVoiceDuckingChannelBindings.size(); ++index) {
    const auto &binding = kVoiceDuckingChannelBindings[index];
    const float volume = state.*(binding.start_volume) + state.*(binding.delta) * progress;
    SetVoiceDuckingCategoryVolume(engine, static_cast<int>(index), volume);
  }
}

void SyncActiveVoiceDuckingTargets(openwow::audio::SoundEngine& engine) {
  for (std::size_t index = 0; index < kVoiceDuckingChannelBindings.size(); ++index) {
    bool has_target = false;
    const float target_volume = ReadVoiceDuckingTargetVolume(static_cast<int>(index), &has_target);
    if (!has_target) {
      continue;
    }

    const float current_volume =
        GetVoiceDuckingCategoryVolume(engine, static_cast<int>(index));
    if (current_volume != target_volume) {
      SetVoiceDuckingCategoryVolume(engine, static_cast<int>(index), target_volume);
    }
  }
}

}

static int s_voice_disabled = 0;
static int s_comsat_running = 0;
static std::uint32_t s_comsat_shutdown_deadline_tick_ms = 0;
static int s_ptt_mouse_button = 0;
static int s_ptt_key_code = -1;
static uint32_t s_ptt_modifiers = 0;
static int s_sleep_interval = 10;
static int s_driver_enabled = 0;
static int s_last_reported_driver_enabled = 0;
static openwow::core::SCritSect s_comsat_runtime_lock;
static openwow::core::SCritSect s_voice_crit_sect;

static ComSatSoundRuntime s_comsat_sound_runtime;
static std::uint32_t s_enable_voice_chat_cvar_callback_handle = 0;
static std::uint32_t s_enable_microphone_cvar_callback_handle = 0;
static std::uint32_t s_voice_chat_mode_cvar_callback_handle = 0;
static std::uint32_t s_push_to_talk_button_cvar_callback_handle = 0;
static std::uint32_t s_voice_chat_output_driver_index_cvar_callback_handle = 0;
static std::uint32_t s_voice_chat_input_driver_index_cvar_callback_handle = 0;
static std::uint32_t s_voice_activation_sensitivity_cvar_callback_handle = 0;

static VolumeDuckingState s_ducking;
static openwow::core::SMutex s_voice_chat_single_instance_mutex;

struct ComSatActivityState {
  std::uint64_t cached_local_player_guid{0};
  std::unordered_set<std::uint64_t> active_speaker_guids;
  std::vector<ComSatSessionKey> active_sessions;
};

static ComSatActivityState s_activity_state;

static std::vector<ComSatCommand> s_command_queue;

static std::vector<ComSatCommand> s_event_queue;

namespace {

std::optional<std::string> TryResolveVoiceSpeakerDisplayName(
    const WorldSession& session, const std::uint64_t raw_guid) {
  if (raw_guid != 0) {
    if (const auto *player_name = session.query_cache().GetPlayerName(raw_guid)) {
      if (!player_name->name.empty()) {
        if (!player_name->realm_name.empty()) {
          return player_name->name + "-" + player_name->realm_name;
        }
        return player_name->name;
      }
    }

    const auto cached_name = session.objects().GetPlayerName(ObjectGuid(raw_guid));
    if (!cached_name.empty()) {
      return cached_name;
    }
  }

  return std::nullopt;
}

std::string ResolveVoiceSpeakerDisplayNameOrUnknown(
    const WorldSession& session, const std::uint64_t raw_guid) {
  if (const auto speaker_name = TryResolveVoiceSpeakerDisplayName(session, raw_guid);
      speaker_name.has_value()) {
    return *speaker_name;
  }

  return Localization::Get().GetString("UNKNOWN", "UNKNOWN");
}

std::uint64_t ResolveCachedLocalPlayerGuid() {
  return s_activity_state.cached_local_player_guid;
}

bool HasActiveComSatSpeaker(const std::uint64_t raw_guid) {
  return raw_guid != 0 && s_activity_state.active_speaker_guids.contains(raw_guid);
}

void SetActiveComSatSpeaker(const std::uint64_t raw_guid, const bool active) {
  if (raw_guid == 0) {
    return;
  }

  if (active) {
    s_activity_state.active_speaker_guids.insert(raw_guid);
  } else {
    s_activity_state.active_speaker_guids.erase(raw_guid);
  }
}

bool HasSelectedComSatSession() {
  return VoiceChat::Get().GetCurrentSessionOrdinal().has_value();
}

bool ShouldKeepComSatRuntimeActive() {
  const auto local_player_guid = ResolveCachedLocalPlayerGuid();
  return (local_player_guid != 0 && HasActiveComSatSpeaker(local_player_guid)) ||
         (VoiceChat::Get().IsEnabledAndActive() && HasSelectedComSatSession());
}

bool IsTrackedComSatSession(const std::uint32_t session_id_low,
                            const std::uint32_t session_id_high) {
  return std::any_of(
      s_activity_state.active_sessions.begin(),
      s_activity_state.active_sessions.end(),
      [session_id_low, session_id_high](const ComSatSessionKey &session_key) {
        return session_key.session_id_low == session_id_low &&
               session_key.session_id_high == session_id_high;
      });
}

std::uint64_t EventSpeakerGuid(const ComSatCommand &event) {
  return (static_cast<std::uint64_t>(event.param4) << 32) |
         static_cast<std::uint32_t>(event.int_val);
}

void FireLocalTalkerEvent(const WorldSession& session, const bool talking) {
  const auto player_guid = ResolveCachedLocalPlayerGuid();
  if (player_guid == 0) {
    return;
  }

  if (talking && !VoiceChat::Get().IsInVoiceChannel()) {
    return;
  }

  SetActiveComSatSpeaker(player_guid, talking);

  const auto speaker_name =
      ResolveVoiceSpeakerDisplayNameOrUnknown(session, player_guid);
  auto &events = ui::game::ScriptEventDispatch::Get();
  if (talking) {
    events.FireVoiceStart(player_guid, speaker_name);
    return;
  }

  events.FireVoiceStop(player_guid, speaker_name);
}

void FireRemoteTalkerEvent(const WorldSession& session,
                           const ComSatCommand &event, const bool talking) {
  if (!IsTrackedComSatSession(event.param1, event.param2)) {
    return;
  }

  const auto speaker_guid = EventSpeakerGuid(event);
  if (speaker_guid == 0) {
    return;
  }

  const auto speaker_name = TryResolveVoiceSpeakerDisplayName(session, speaker_guid);
  if (!speaker_name.has_value()) {
    return;
  }

  auto &events = ui::game::ScriptEventDispatch::Get();
  if (talking) {
    events.FireVoiceStart(speaker_guid, *speaker_name);
    SetActiveComSatSpeaker(speaker_guid, true);
    ++s_ducking.active_remote_talker_count;
    return;
  }

  events.FireVoiceStop(speaker_guid, *speaker_name);
  SetActiveComSatSpeaker(speaker_guid, false);
  if (s_ducking.active_remote_talker_count > 0) {
    --s_ducking.active_remote_talker_count;
  }
}

void VoiceChat_ProcessEventQueue(const WorldSession& session) {
  if (s_event_queue.empty()) {
    return;
  }

  auto pending_events = std::move(s_event_queue);
  s_event_queue.clear();

  for (const auto &event : pending_events) {
    switch (static_cast<ComSatEventType>(event.type)) {
    case ComSatEventType::kLocalTalkerStart:
      FireLocalTalkerEvent(session, true);
      break;
    case ComSatEventType::kLocalTalkerStop:
      FireLocalTalkerEvent(session, false);
      break;
    case ComSatEventType::kRemoteTalkerStart:
      FireRemoteTalkerEvent(session, event, true);
      break;
    case ComSatEventType::kRemoteTalkerStop:
      FireRemoteTalkerEvent(session, event, false);
      break;
    default:
      break;
    }
  }
}

void ClearTrackedVoiceActivity() {
  ScopedComSatLock runtime_lock(s_comsat_runtime_lock);
  s_activity_state.cached_local_player_guid = 0;
  s_activity_state.active_speaker_guids.clear();
  s_activity_state.active_sessions.clear();
}

}

static constexpr const char* kVoiceChatSessionLuaGlobals[] = {
    "VoiceEnumerateOutputDevices",
    "VoiceEnumerateCaptureDevices",
    "VoiceSelectOutputDevice",
    "VoiceSelectCaptureDevice",
    "VoiceGetCurrentOutputDevice",
    "VoiceGetCurrentCaptureDevice",
    "GetVoiceStatus",
    "GetNumVoiceSessions",
    "GetVoiceSessionInfo",
    "GetVoiceCurrentSessionID",
    "SetActiveVoiceChannelBySessionID",
    "GetNumVoiceSessionMembersBySessionID",
    "GetVoiceSessionMemberInfoBySessionID",
    "VoiceIsDisabledByClient",
    "UnitIsTalking",
};

static_assert(
    sizeof(kVoiceChatSessionLuaGlobals) / sizeof(kVoiceChatSessionLuaGlobals[0]) == 15,
    "IDA off_AF29C0 has exactly 15 (name, handler) pairs (30 entries, stride 2)");

void VoiceChat_BindRegisteredCVars(openwow::audio::SoundRuntime& sound_runtime) {
  auto &cvars = openwow::ui::game::CVarSystem::Instance();
  auto &engine = sound_runtime.sound_engine();

  if (s_enable_voice_chat_cvar_callback_handle != 0) {
    cvars.RemoveCallback(kEnableVoiceChatCVarName, s_enable_voice_chat_cvar_callback_handle);
  }
  s_enable_voice_chat_cvar_callback_handle = cvars.AddCallback(
      kEnableVoiceChatCVarName,
      [&sound_runtime](const std::string &old_value, const std::string &new_value) {
        (void)CVar_EnableVoiceChat_OnChanged(sound_runtime, old_value.c_str(),
                                             new_value.c_str());
      });

  if (s_enable_microphone_cvar_callback_handle != 0) {
    cvars.RemoveCallback(kEnableMicrophoneCVarName,
                         s_enable_microphone_cvar_callback_handle);
  }
  s_enable_microphone_cvar_callback_handle = cvars.AddCallback(
      kEnableMicrophoneCVarName,
      [&engine](const std::string &, const std::string &new_value) {
        (void)CVar_EnableMicrophone_OnChanged(engine, new_value.c_str());
      });

  if (s_voice_chat_mode_cvar_callback_handle != 0) {
    cvars.RemoveCallback(kVoiceChatModeCVarName, s_voice_chat_mode_cvar_callback_handle);
  }
  s_voice_chat_mode_cvar_callback_handle = cvars.AddCallback(
      kVoiceChatModeCVarName,
      [](const std::string &, const std::string &new_value) {
        SyncVoiceChatModeFromCVarValue(new_value);
      });

  if (s_push_to_talk_button_cvar_callback_handle != 0) {
    cvars.RemoveCallback(kPushToTalkButtonCVarName,
                         s_push_to_talk_button_cvar_callback_handle);
  }
  s_push_to_talk_button_cvar_callback_handle = cvars.AddCallback(
      kPushToTalkButtonCVarName,
      [](const std::string &, const std::string &new_value) {
        SyncPushToTalkBindingFromCVarValue(new_value);
      });

  if (s_voice_chat_output_driver_index_cvar_callback_handle != 0) {
    cvars.RemoveCallback(kVoiceChatOutputDriverIndexCVarName,
                         s_voice_chat_output_driver_index_cvar_callback_handle);
  }
  s_voice_chat_output_driver_index_cvar_callback_handle = cvars.AddCallback(
      kVoiceChatOutputDriverIndexCVarName,
      [&engine](const std::string& old_value, const std::string& new_value) {
        OnVoiceChatOutputDriverIndexChanged(engine, old_value, new_value);
      });

  if (s_voice_chat_input_driver_index_cvar_callback_handle != 0) {
    cvars.RemoveCallback(kVoiceChatInputDriverIndexCVarName,
                         s_voice_chat_input_driver_index_cvar_callback_handle);
  }
  s_voice_chat_input_driver_index_cvar_callback_handle = cvars.AddCallback(
      kVoiceChatInputDriverIndexCVarName,
      [&engine](const std::string& old_value, const std::string& new_value) {
        OnVoiceChatInputDriverIndexChanged(engine, old_value, new_value);
      });

  if (s_voice_activation_sensitivity_cvar_callback_handle != 0) {
    cvars.RemoveCallback(kVoiceActivationSensitivityCVarName,
                         s_voice_activation_sensitivity_cvar_callback_handle);
  }
  s_voice_activation_sensitivity_cvar_callback_handle = cvars.AddCallback(
      kVoiceActivationSensitivityCVarName,
      [](const std::string &, const std::string &new_value) {
        (void)CVar_VoiceActivationSensitivity_OnChanged(new_value.c_str());
      });

  if (cvars.Exists(kVoiceChatModeCVarName)) {
    SyncVoiceChatModeFromCVarValue(cvars.GetCVar(kVoiceChatModeCVarName));
  }
  if (cvars.Exists(kPushToTalkButtonCVarName)) {
    SyncPushToTalkBindingFromCVarValue(cvars.GetCVar(kPushToTalkButtonCVarName));
  }
}

void VoiceChat_SelectOutputDriverIndex(openwow::audio::SoundEngine& engine,
                                       int driver_index) {
  ScopedComSatLock lock(s_voice_crit_sect);

  if (!engine.IsVoiceChatEnabled()) {
    return;
  }

  const int count = engine.GetVoiceOutputDeviceCount();
  if (driver_index >= 0 && driver_index < count) {
    const char *name = engine.GetVoiceOutputDeviceName(driver_index);
    if (name) {
      engine.SetCurrentVoiceOutputDeviceName(name);
    }
  }
}

void VoiceChat_SelectInputDriverIndex(openwow::audio::SoundEngine& engine,
                                      int driver_index) {
  ScopedComSatLock lock(s_voice_crit_sect);

  if (!engine.IsVoiceChatEnabled()) {
    return;
  }

  const int count = engine.GetInputDeviceCount();
  if (driver_index >= 0 && driver_index < count) {
    const char *name = engine.GetInputDeviceName(driver_index);
    if (name) {
      engine.SetCurrentInputDeviceName(name);
    }
  }
}

void ComSat_UpdateDriverState() {
  int enabled = s_ptt_modifiers != 0 ? 1 : 0;

  if (s_ptt_key_code != -1) {
    enabled = IsVoicePushToTalkKeyPressed(s_ptt_key_code) ? 1 : 0;
  }

  if (s_ptt_mouse_button != 0) {
    enabled = IsVoicePushToTalkMouseButtonPressed(s_ptt_mouse_button) ? 1 : 0;
  }

  if (enabled != 0 && s_ptt_modifiers != 0) {
    for (int modifier_index = 0; modifier_index <= 5; ++modifier_index) {
      if (((s_ptt_modifiers >> modifier_index) & 1u) == 0) {
        continue;
      }

      if (!IsVoicePushToTalkKeyPressed(modifier_index)) {
        enabled = 0;
        break;
      }
    }
  }

  s_driver_enabled = enabled;
}

int VoiceChat_IsDisabled() {
  return s_voice_disabled;
}

int VoiceChat_CheckSingleInstance() {
  constexpr char kVoiceChatSingleInstanceMutexName[] = "WOWCOMSATCLIENT12340";

  s_voice_chat_single_instance_mutex.Create(true, kVoiceChatSingleInstanceMutexName);
  if (!s_voice_chat_single_instance_mutex.IsValid()) {
    return 0;
  }

  if (s_voice_chat_single_instance_mutex.Wait(0) == 0u) {
    return 1;
  }

  s_voice_chat_single_instance_mutex.Destroy();
  (void)s_voice_chat_single_instance_mutex.Release();
  return 0;
}

int VoiceChat_KeyNameToCode(const char *name) {
  if (!name || !*name)
    return -1;

  const auto first_byte = static_cast<std::int8_t>(
      static_cast<std::uint8_t>(name[0]));
  if (!name[1] && first_byte > 32)
    return static_cast<int>(first_byte);

  auto eq = [](const char *a, const char *b) -> bool {
    while (*a && *b) {
      char ca = (*a >= 'a' && *a <= 'z') ? *a - 32 : *a;
      char cb = (*b >= 'a' && *b <= 'z') ? *b - 32 : *b;
      if (ca != cb)
        return false;
      ++a;
      ++b;
    }
    return *a == *b;
  };

  if (eq(name, "LSHIFT"))
    return 0;
  if (eq(name, "RSHIFT"))
    return 1;
  if (eq(name, "LCTRL"))
    return 2;
  if (eq(name, "RCTRL"))
    return 3;
  if (eq(name, "LALT"))
    return 4;
  if (eq(name, "RALT"))
    return 5;
  if (eq(name, "SPACE"))
    return 32;
  if (eq(name, "NUMPADPLUS"))
    return 266;
  if (eq(name, "NUMPADMINUS"))
    return 267;
  if (eq(name, "NUMPADMULTIPLY"))
    return 268;
  if (eq(name, "NUMPADDIVIDE"))
    return 269;
  if (eq(name, "NUMPADDECIMAL"))
    return 270;
  if (eq(name, "NUMPADEQUALS"))
    return 780;

  constexpr std::string_view kNumpadPrefix = "NUMPAD";
  bool has_numpad_prefix = true;
  for (std::size_t i = 0; i < kNumpadPrefix.size(); ++i) {
    const char value = name[i];
    if (value == '\0') {
      has_numpad_prefix = false;
      break;
    }
    const char folded = value >= 'a' && value <= 'z'
        ? static_cast<char>(value - ('a' - 'A'))
        : value;
    if (folded != kNumpadPrefix[i]) {
      has_numpad_prefix = false;
      break;
    }
  }
  if (has_numpad_prefix) {
    const auto suffix = static_cast<std::uint8_t>(name[6]);
    return suffix < static_cast<std::uint8_t>(':')
        ? static_cast<int>(name[6]) + 0xD0
        : -1;
  }

  if (eq(name, "ESCAPE"))
    return 512;
  if (eq(name, "ENTER"))
    return 513;
  if (eq(name, "BACKSPACE"))
    return 514;
  if (eq(name, "TAB"))
    return 515;
  if (eq(name, "LEFT"))
    return 516;
  if (eq(name, "UP"))
    return 517;
  if (eq(name, "RIGHT"))
    return 518;
  if (eq(name, "DOWN"))
    return 519;
  if (eq(name, "INSERT"))
    return 520;
  if (eq(name, "DELETE"))
    return 521;
  if (eq(name, "HOME"))
    return 522;
  if (eq(name, "END"))
    return 523;
  if (eq(name, "PAGEUP"))
    return 524;
  if (eq(name, "PAGEDOWN"))
    return 525;
  if (eq(name, "CAPSLOCK"))
    return 526;
  if (eq(name, "NUMLOCK"))
    return 527;
  if (eq(name, "PRINTSCREEN"))
    return 530;

  if (name[0] == 'F') {
    return std::atoi(name + 1) + 0x2FF;
  }

  return -1;
}

uint32_t MiddleButton_NameToBitmask(const char *name) {
  if (!name || !*name)
    return 0;

  auto eq = [](const char *a, const char *b) -> bool {
    while (*a && *b) {
      char ca = (*a >= 'a' && *a <= 'z') ? *a - 32 : *a;
      char cb = (*b >= 'a' && *b <= 'z') ? *b - 32 : *b;
      if (ca != cb)
        return false;
      ++a;
      ++b;
    }
    return *a == *b;
  };

  if (eq(name, "LeftButton"))
    return 1;
  if (eq(name, "RightButton"))
    return 4;
  if (eq(name, "MiddleButton"))
    return 2;

  for (int i = 4; i <= 31; ++i) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "Button%d", i);
    if (eq(name, buf))
      return 1u << (i - 1);
  }

  return 0;
}

void VoiceChat_StartVolumeDucking(openwow::audio::SoundEngine& engine,
                                  VolumeDuckingState &state) {
  if (state.is_ducking)
    return;

  state.is_ducking = true;
  const float delay = ReadCVarFloatOrFallback("StartTalkingDelay", 0.0f);
  state.inv_delay = delay == 0.0f ? 0.0f : 1.0f / delay;

  const float transition = ReadCVarFloatOrFallback("StartTalkingTime", 0.2f);
  state.inv_transition = transition == 0.0f ? 0.0f : 1.0f / transition;

  state.sound_vol = GetVoiceDuckingCategoryVolume(engine, 0);
  state.music_vol = GetVoiceDuckingCategoryVolume(engine, 1);
  state.ambience_vol = GetVoiceDuckingCategoryVolume(engine, 2);

  bool has_target = false;
  const float sound_target = ReadVoiceDuckingTargetVolume(0, &has_target);
  if (has_target) {
    state.sound_delta = sound_target - state.sound_vol;
  }

  const float music_target = ReadVoiceDuckingTargetVolume(1, &has_target);
  if (has_target) {
    state.music_delta = music_target - state.music_vol;
  }

  const float ambience_target = ReadVoiceDuckingTargetVolume(2, &has_target);
  if (has_target) {
    state.ambience_delta = ambience_target - state.ambience_vol;
  }
}

void VoiceChat_StopVolumeDucking(openwow::audio::SoundEngine& engine,
                                 VolumeDuckingState &state) {
  if (!state.is_ducking)
    return;

  state.is_ducking = false;
  state.time_elapsed =
      static_cast<float>(openwow::core::GameClock::GetTickCount32()) * kMillisecondsToSeconds;

  const float delay = ReadCVarFloatOrFallback("StopTalkingDelay", 3.0f);
  state.inv_delay = delay == 0.0f ? 0.0f : 1.0f / delay;

  const float transition = ReadCVarFloatOrFallback("StopTalkingTime", 3.0f);
  state.inv_transition = transition == 0.0f ? 0.0f : 1.0f / transition;

  state.sound_vol = GetVoiceDuckingCategoryVolume(engine, 0);
  state.music_vol = GetVoiceDuckingCategoryVolume(engine, 1);
  state.ambience_vol = GetVoiceDuckingCategoryVolume(engine, 2);

  bool has_base = false;
  const float sound_base = ReadVoiceDuckingBaseVolume(0, &has_base);
  if (has_base) {
    state.sound_delta = sound_base - state.sound_vol;
  }

  const float music_base = ReadVoiceDuckingBaseVolume(1, &has_base);
  if (has_base) {
    state.music_delta = music_base - state.music_vol;
  }

  const float ambience_base = ReadVoiceDuckingBaseVolume(2, &has_base);
  if (has_base) {
    state.ambience_delta = ambience_base - state.ambience_vol;
  }
}

void VoiceChat_InitVolumeDucking(openwow::audio::SoundEngine& engine,
                                 VolumeDuckingState &state) {
  state.time_elapsed = 0.0f;
  state.inv_delay = 0.0f;
  state.active_remote_talker_count = 0;
  state.inv_transition = 0.0f;
  state.sound_delta = 0.0f;
  state.music_delta = 0.0f;
  state.ambience_delta = 0.0f;

  bool has_base = false;
  const float sound_base = ReadVoiceDuckingBaseVolume(0, &has_base);
  if (has_base) {
    state.sound_vol = sound_base;
    SetVoiceDuckingCategoryVolume(engine, 0, state.sound_vol);
  } else {
    state.sound_vol = 0.0f;
  }

  const float music_base = ReadVoiceDuckingBaseVolume(1, &has_base);
  if (has_base) {
    state.music_vol = music_base;
    SetVoiceDuckingCategoryVolume(engine, 1, state.music_vol);
  } else {
    state.music_vol = 0.0f;
  }

  const float ambience_base = ReadVoiceDuckingBaseVolume(2, &has_base);
  if (has_base) {
    state.ambience_vol = ambience_base;
    SetVoiceDuckingCategoryVolume(engine, 2, state.ambience_vol);
  } else {
    state.ambience_vol = 0.0f;
  }
}

void VoiceChat_UpdateVolumeDucking(openwow::audio::SoundEngine& engine,
                                   openwow::audio::VoiceChatLoopback& loopback,
                                   VolumeDuckingState &state,
                                   const std::uint32_t tick_count_ms) {
  const float current_time_seconds = static_cast<float>(tick_count_ms) * kMillisecondsToSeconds;

  if (state.inv_delay == 0.0f) {
    if (state.inv_transition == 0.0f) {
      if (VoiceChat_IsVoiceActivityActive(loopback)) {
        SyncActiveVoiceDuckingTargets(engine);
      }
    } else {
      float transition_progress =
          state.inv_transition * (current_time_seconds - state.time_elapsed);
      if (transition_progress >= 1.0f) {
        transition_progress = 1.0f;
        state.inv_transition = 0.0f;
        state.time_elapsed = 0.0f;
      }

      ApplyInterpolatedDuckingVolumes(engine, state, transition_progress);
    }
  } else if (state.inv_delay * (current_time_seconds - state.time_elapsed) >= 1.0f) {
    state.inv_delay = 0.0f;
    state.time_elapsed = current_time_seconds;
  }

  if (!state.is_ducking) {
    if (VoiceChat_IsVoiceActivityActive(loopback)) {
      VoiceChat_StartVolumeDucking(engine, state);
      return;
    }
  } else if (!VoiceChat_IsVoiceActivityActive(loopback)) {
    VoiceChat_StopVolumeDucking(engine, state);
  }
}

bool VoiceChat_IsVoiceActivityActive(openwow::audio::VoiceChatLoopback& loopback) {
  const auto local_player_guid = ResolveCachedLocalPlayerGuid();
  return s_ducking.active_remote_talker_count > 0 ||
         (local_player_guid != 0 && HasActiveComSatSpeaker(local_player_guid)) ||
         openwow::audio::VoiceChat_IsRecordingLoopbackSound(loopback) ||
         openwow::audio::VoiceChat_IsPlayingLoopbackSound(loopback);
}

void VoiceChat_SetLocalPlayerGuid(const std::uint64_t player_guid) {
  s_activity_state.cached_local_player_guid = player_guid;
}

void VoiceChat_SetLocalPlayerTalking(const bool talking) {
  SetActiveComSatSpeaker(ResolveCachedLocalPlayerGuid(), talking);
}

void VoiceChat_RequestRecordingLoopback(openwow::audio::VoiceChatLoopback& loopback) {
  (void)loopback.ActivatePreparedRecording();
}

void VoiceChat_ResetActivityState(openwow::audio::VoiceChatLoopback& loopback) {
  SetActiveComSatSpeaker(ResolveCachedLocalPlayerGuid(), false);
  openwow::audio::VoiceChat_StopRecordingLoopbackSound(loopback);
  openwow::audio::VoiceChat_SetLoopbackPlayback(loopback, false);
}

void VoiceChat_ClearActiveComSatSessions() {
  s_activity_state.active_sessions.clear();
}

void VoiceChat_AddActiveComSatSession(const std::uint32_t session_id_low,
                                      const std::uint32_t session_id_high) {
  if (session_id_low == 0 && session_id_high == 0) {
    return;
  }

  if (IsTrackedComSatSession(session_id_low, session_id_high)) {
    return;
  }

  if (s_activity_state.active_sessions.size() >= 32) {
    return;
  }

  s_activity_state.active_sessions.push_back({session_id_low, session_id_high});
}

void VoiceChat_RemoveActiveComSatSession(const std::uint32_t session_id_low,
                                         const std::uint32_t session_id_high) {
  s_activity_state.active_sessions.erase(
      std::remove_if(s_activity_state.active_sessions.begin(), s_activity_state.active_sessions.end(),
                     [session_id_low, session_id_high](const ComSatSessionKey &session_key) {
                       return session_key.session_id_low == session_id_low &&
                              session_key.session_id_high == session_id_high;
                     }),
      s_activity_state.active_sessions.end());
}

bool VoiceChat_StopTrackedLocalSpeaker(const WorldSession& session) {
  const auto guid = ResolveCachedLocalPlayerGuid();
  if (guid == 0 || !HasActiveComSatSpeaker(guid)) {
    return false;
  }

  SetActiveComSatSpeaker(guid, false);
  ui::game::ScriptEventDispatch::Get().FireVoiceStop(
      guid, ResolveVoiceSpeakerDisplayNameOrUnknown(session, guid));
  return true;
}

bool VoiceChat_StopTrackedRemoteSpeaker(const std::uint64_t guid) {
  if (guid == 0 || !HasActiveComSatSpeaker(guid)) {
    return false;
  }

  SetActiveComSatSpeaker(guid, false);
  if (s_ducking.active_remote_talker_count > 0) {
    --s_ducking.active_remote_talker_count;
  }
  return true;
}

void VoiceChat_ResetComSatRuntimeState(openwow::audio::VoiceChatLoopback& loopback) {
  ScopedComSatLock runtime_lock(s_comsat_runtime_lock);
  s_comsat_sound_runtime.Reset();
  s_activity_state.active_speaker_guids.clear();
  s_activity_state.active_sessions.clear();
  s_activity_state.cached_local_player_guid = 0;
  s_ducking = {};
  s_command_queue.clear();
  s_event_queue.clear();
  s_comsat_running = 0;
  s_comsat_shutdown_deadline_tick_ms = 0;
  s_ptt_mouse_button = 0;
  s_ptt_key_code = -1;
  s_ptt_modifiers = 0;
  s_driver_enabled = 0;
  s_last_reported_driver_enabled = 0;
  openwow::audio::VoiceChat_StopRecordingLoopbackSound(loopback);
  openwow::audio::VoiceChat_SetLoopbackPlayback(loopback, false);
}

void VoiceChat_LeaveAllSessions(const WorldSession& session) {
  ScopedComSatLock runtime_lock(s_comsat_runtime_lock);

  if (!s_comsat_sound_runtime.driver_created) {
    return;
  }

  const auto local_guid = ResolveCachedLocalPlayerGuid();
  auto speaker_guids_copy = s_activity_state.active_speaker_guids;
  for (const auto guid : speaker_guids_copy) {
    if (guid == 0 || guid == local_guid) {
      continue;
    }

    const auto speaker_name = TryResolveVoiceSpeakerDisplayName(session, guid);
    if (speaker_name.has_value()) {
      ui::game::ScriptEventDispatch::Get().FireVoiceStop(guid, *speaker_name);
    }
    SetActiveComSatSpeaker(guid, false);
    if (s_ducking.active_remote_talker_count > 0) {
      --s_ducking.active_remote_talker_count;
    }
  }

  const bool had_current_session =
      VoiceChat::Get().GetCurrentSessionOrdinal().has_value();

  s_activity_state.active_sessions.clear();

  if (had_current_session) {
    VoiceChat::Get().ClearCurrentSessionSelection(session.sound_runtime());

    if (local_guid != 0 && HasActiveComSatSpeaker(local_guid)) {
      SetActiveComSatSpeaker(local_guid, false);
    ui::game::ScriptEventDispatch::Get().FireVoiceStop(
        local_guid, ResolveVoiceSpeakerDisplayNameOrUnknown(session, local_guid));
    }
  }

  auto &events = ui::game::ScriptEventDispatch::Get();
  events.FireVoiceSessionsUpdate();
  events.FireVoiceLeftSession();
}

void VoiceChat_UpdateDriverState_Full() {
  if (s_driver_enabled != s_last_reported_driver_enabled) {
    ui::game::ScriptEventDispatch::Get().FireEvent(
        ui::game::events::VOICE_CHAT_ENABLED_UPDATE);
    s_last_reported_driver_enabled = s_driver_enabled;
  }

  s_sleep_interval = 10;
}

void *ComSatClient_Alloc(uint32_t size) {
  return std::malloc(size);
}

void ComSatClient_Free(void *ptr) {
  std::free(ptr);
}

bool CVar_EnableMicrophone_OnChanged(openwow::audio::SoundEngine& engine,
                                     const char *new_value) {
  const bool voice_enabled = ReadVoiceChatBoolCVar(kEnableVoiceChatCVarName);
  const bool microphone_enabled =
      new_value != nullptr && ParseVoiceToggleValue(new_value);
  (void)SendVoiceChatEnablePacket(voice_enabled, microphone_enabled);

  if (voice_enabled && microphone_enabled &&
      VoiceChat::Get().IsAllowedAndEnabled()) {
    openwow::audio::VoiceChat_SetCaptureEnabled(engine, true);
  } else {
    openwow::audio::VoiceChat_SetCaptureEnabled(engine, false);
  }
  return true;
}

bool CVar_VoiceActivationSensitivity_OnChanged(const char *new_value) {
  constexpr float kMinSensitivity = 0.0f;
  constexpr float kMaxSensitivity = 0.99f;

  auto &cvars = openwow::ui::game::CVarSystem::Instance();
  if (!cvars.Exists(kVoiceChatModeCVarName) ||
      cvars.GetCVarInt(kVoiceChatModeCVarName) != 1) {
    return true;
  }

  float sensitivity = static_cast<float>(
      openwow::core::ParseFloatLikeSub76FB80(new_value));

  if (sensitivity < kMinSensitivity) {
    sensitivity = kMinSensitivity;
  } else if (sensitivity >= kMaxSensitivity) {
    sensitivity = kMaxSensitivity;
  }

  VoiceChat_EnqueueFloatCommand(
      static_cast<uint32_t>(ComSatCommandType::kSensitivity),
      sensitivity, 0, 0);

  return true;
}

bool CVar_EnableVoiceChat_OnChanged(openwow::audio::SoundRuntime& sound_runtime,
                                     const char *old_value,
                                     const char *new_value) {
  auto &engine = sound_runtime.sound_engine();
  const bool old_voice_enabled =
      old_value != nullptr && ParseVoiceToggleValue(old_value);
  const bool voice_enabled =
      new_value != nullptr && ParseVoiceToggleValue(new_value);
  const bool microphone_enabled =
      ReadVoiceChatBoolCVar(kEnableMicrophoneCVarName, true);

  if (!old_voice_enabled && voice_enabled) {
    VoiceChat_Initialize(sound_runtime);

    SyncVoiceChatSettingsFromCVars();
  }

  (void)SendVoiceChatEnablePacket(voice_enabled, microphone_enabled);
  ui::game::ScriptEventDispatch::Get().FireEvent(
      ui::game::events::VOICE_CHAT_ENABLED_UPDATE);

  if (voice_enabled && microphone_enabled &&
      VoiceChat::Get().IsAllowedAndEnabled()) {
    openwow::audio::VoiceChat_SetCaptureEnabled(engine, true);
  } else {
    openwow::audio::VoiceChat_SetCaptureEnabled(engine, false);
  }

  if (old_voice_enabled && !voice_enabled) {
    VoiceChat_Shutdown(sound_runtime);
  }

  return true;
}

void ComSat_ThreadProc() {

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo, "ComSat: thread proc (stub, not started)");
}

void VoiceChat_HandlePushToTalkReassign() {
  s_ptt_key_code = -1;
  s_ptt_mouse_button = 0;
  s_ptt_modifiers = 0;

  const std::string binding = VoiceChat::Get().GetPushToTalkKey();
  if (binding.empty()) {
    return;
  }

  std::string_view key_token = binding;
  s_ptt_modifiers = actions::bindings::adapters::retail::ParseModifierBits(key_token, key_token);
  if (key_token.empty()) {
    return;
  }

  const std::string key_name(key_token);
  s_ptt_mouse_button = static_cast<int>(MiddleButton_NameToBitmask(key_name.c_str()));
  if (s_ptt_mouse_button != 0) {
    return;
  }

  s_ptt_key_code = VoiceChat_KeyNameToCode(key_name.c_str());
}

void ComSat_Init() {
  ScopedComSatLock runtime_lock(s_comsat_runtime_lock);
  if (s_comsat_running != 0) {
    return;
  }

  s_comsat_running = 1;
}

void VoiceChat_InitComSatDriver(openwow::audio::SoundEngine& engine) {
  {
    ScopedComSatLock runtime_lock(s_comsat_runtime_lock);
    s_comsat_sound_runtime.Initialize(engine);
  }

  SyncVoiceChatSettingsFromCVars();
  s_driver_enabled = 0;
  s_last_reported_driver_enabled = 0;
}

void VoiceChat_Initialize(openwow::audio::SoundRuntime& sound_runtime) {
  VoiceChat_BindRegisteredCVars(sound_runtime);
  auto &engine = sound_runtime.sound_engine();
  s_voice_disabled = 0;
  VoiceChat_InitVolumeDucking(engine, s_ducking);

  if (!SupportsVoiceChatCpuRequirements()) {
    s_voice_disabled = 1;
    LogVoiceChatDisabledBanner(engine,
                               " -# ERROR! SSE support is required for voice chat!");
    LogVoiceChatDisabledFooter(engine,
                               " -# Voice Chat DISABLED.");
    return;
  }

  if (!VoiceChat_CheckSingleInstance()) {
    s_voice_disabled = 1;
    LogVoiceChatDisabledBanner(engine,
        " -# ERROR! Voice Chat does not support multiple clients on one computer.");
    LogVoiceChatDisabledFooter(engine,
                               " -# Voice Chat DISABLED for this client.");
    return;
  }

  VoiceChat_InitComSatDriver(engine);
  ClearTrackedVoiceActivity();
  (void)SendVoiceChatEnablePacket(ReadVoiceChatBoolCVar(kEnableVoiceChatCVarName),
                                  ReadVoiceChatBoolCVar(kEnableMicrophoneCVarName, true));
}

void VoiceChat_Shutdown(openwow::audio::SoundRuntime& sound_runtime) {
  auto &engine = sound_runtime.sound_engine();
  auto &loopback = sound_runtime.voice_loopback();
  {
    ScopedComSatLock runtime_lock(s_comsat_runtime_lock);
    s_comsat_running = 0;
    s_comsat_shutdown_deadline_tick_ms = 0;
  }

  VoiceChat::Get().ShutdownRuntime(sound_runtime);
  VoiceChat_ResetComSatRuntimeState(loopback);

  if (s_voice_chat_single_instance_mutex.IsValid()) {
    s_voice_chat_single_instance_mutex.Destroy();
    (void)s_voice_chat_single_instance_mutex.Release();
  }

  VoiceChat_InitVolumeDucking(engine, s_ducking);
  engine.ShutdownVoiceChat();
}

void VoiceChat_EnqueueFloatCommand(uint32_t type, float value, uint32_t param1, uint32_t param2) {
  ScopedComSatLock runtime_lock(s_comsat_runtime_lock);
  ComSatCommand cmd{};
  cmd.type = type;
  cmd.param1 = param1;
  cmd.param2 = param2;
  cmd.float_val = value;
  s_command_queue.push_back(cmd);
}

void VoiceChat_EnqueueIntCommand(uint32_t type, int32_t value, uint32_t param1, uint32_t param2) {
  ScopedComSatLock runtime_lock(s_comsat_runtime_lock);
  ComSatCommand cmd{};
  cmd.type = type;
  cmd.param1 = param1;
  cmd.param2 = param2;
  cmd.int_val = value;
  s_command_queue.push_back(cmd);
}

void VoiceChat_EnqueueComSatEvent(uint32_t type, uint32_t p1, uint32_t p2, uint32_t p3,
                                  uint32_t p4) {
  ScopedComSatLock runtime_lock(s_comsat_runtime_lock);
  ComSatCommand cmd{};
  cmd.type = type;
  cmd.param1 = p1;
  cmd.param2 = p2;
  cmd.int_val = static_cast<int32_t>(p3);
  cmd.param4 = p4;
  s_event_queue.push_back(cmd);
}

void VoiceChat_ProcessCommandQueue() {
  for (const auto &cmd : s_command_queue) {
    switch (static_cast<ComSatCommandType>(cmd.type)) {
    case ComSatCommandType::kSessionVolume:
    case ComSatCommandType::kMuteToggle:
    case ComSatCommandType::kSensitivity:
    case ComSatCommandType::kRemoteTalkerVol:
    case ComSatCommandType::kSelectSession:
    case ComSatCommandType::kDeselectSession:
    case ComSatCommandType::kPriority:
    case ComSatCommandType::kCodec:
    case ComSatCommandType::kReportInterval:

      break;
    case ComSatCommandType::kPushToTalkReassign:
      VoiceChat_HandlePushToTalkReassign();
      break;
    }
  }
  s_command_queue.clear();
}

int VoiceChat_ScheduledUpdate(const WorldSession& session,
                              openwow::audio::SoundEngine& engine,
                              openwow::audio::VoiceChatLoopback& loopback,
                              const std::uint32_t tick_count_ms) {
  const std::uint32_t runtime_tick_ms = tick_count_ms;
  bool keep_runtime_active = false;

  {
    ScopedComSatLock runtime_lock(s_comsat_runtime_lock);
    VoiceChat_ProcessEventQueue(session);
    VoiceChat_UpdateDriverState_Full();
    VoiceChat_ProcessCommandQueue();
    keep_runtime_active = ShouldKeepComSatRuntimeActive();
  }

  if (keep_runtime_active) {
    ComSat_Init();
    ScopedComSatLock runtime_lock(s_comsat_runtime_lock);
    s_comsat_shutdown_deadline_tick_ms = runtime_tick_ms + kComSatShutdownGracePeriodMs;
  } else {
    {
      ScopedComSatLock runtime_lock(s_comsat_runtime_lock);
      if (s_comsat_shutdown_deadline_tick_ms == 0 ||
          runtime_tick_ms > s_comsat_shutdown_deadline_tick_ms) {
        s_comsat_running = 0;
        s_comsat_shutdown_deadline_tick_ms = 0;
      }
    }
  }

  engine.CaptureWork(tick_count_ms);

  VoiceChat_UpdateVolumeDucking(engine, loopback, s_ducking, tick_count_ms);
  return 1;
}

ComSatRuntimeStateSnapshot VoiceChat_GetComSatRuntimeStateSnapshot() {
  ScopedComSatLock runtime_lock(s_comsat_runtime_lock);
  return {
      .active = s_comsat_running != 0,
      .shutdown_deadline_tick_ms = s_comsat_shutdown_deadline_tick_ms,
      .driver_created = s_comsat_sound_runtime.driver_created,
      .sound_io_initialized = s_comsat_sound_runtime.sound_io.initialized,
      .sound_io_slot_count = static_cast<std::uint32_t>(s_comsat_sound_runtime.sound_io.slots.size()),
      .datagram_socket_open = s_comsat_sound_runtime.datagram_socket_open(),
      .datagram_socket_bound = s_comsat_sound_runtime.datagram_socket_bound,
  };
}

}

#pragma once
#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/audio/playback/audio_engine.h"
#include "openwow/audio/adapters/ui/sound_cvar_defaults.h"
#include "openwow/audio/adapters/ui/sound_cvar_handlers.h"
#include "openwow/audio/playback/sound_engine.h"
#include "openwow/audio/resources/sound_entry_resolver.h"
#include "openwow/audio/voice/voice_chat_audio_setup.h"
#include "openwow/core/console.h"
#include "openwow/core/decimal_parse.h"
#include "openwow/runtime/scheduling/frame_scheduler.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/core/storm_alloc.h"
#include "openwow/core/storm_cmd.h"
#include "openwow/core/storm_string.h"
#include "openwow/data/formats/dbc/dbc_enums.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/streaming_init.h"
#include "openwow/foundation/math/vec3_exact_compare.h"
#include "openwow/game/chat_system.h"
#include "openwow/game/comsat_client.h"
#include "openwow/game/group_system.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/render/models/animation/animation_state.h"
#include "openwow/game/voice_chat.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/vfs/sfile_core.h"
#include "openwow/world/coordinates/world_coordinates.h"
#include "openwow/world/coordinates/world_coordinates.h"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <numbers>
#include <span>
#include <string_view>
#include <thread>
namespace openwow::audio {

namespace retail_rng = openwow::foundation::hashing;
inline constexpr std::uint32_t kVoiceChatToggleOnLookupResolvedBit = 0x1u;
inline constexpr std::uint32_t kVoiceChatToggleOffLookupResolvedBit = 0x2u;
inline constexpr std::uint32_t kVoiceChatTogglePlaybackPriority = 128u;
inline float ParseSoundVolumeCVar(std::string_view value) { return static_cast<float>(openwow::core::ParseFloatLikeSub76FB80(value)); }
inline void ApplySoundChannelGroupVolume(SoundEngine &engine, AudioEngine &audio, const char *group_name, const float volume) {
  engine.SetChannelGroupVolume(group_name, volume);
  if (openwow::text::EqualsIgnoreCaseAscii(group_name, "SFX")) {
    audio.SetPlaybackChannelVolume(PlaybackChannel::SFX, volume);
    audio.SetSoundVolume(volume);
  } else if (openwow::text::EqualsIgnoreCaseAscii(group_name, "MUSIC") ||
             openwow::text::EqualsIgnoreCaseAscii(group_name, "SCRIPTMUSIC")) {
    audio.SetPlaybackChannelVolume(PlaybackChannel::Music, volume);
    audio.SetMusicVolume(volume);
  } else if (openwow::text::EqualsIgnoreCaseAscii(group_name, "AMBIENCE")) {
    audio.SetPlaybackChannelVolume(PlaybackChannel::Ambience, volume);
    audio.SetAmbienceVolume(volume);
  }
}

[[nodiscard]] inline PlaybackChannel ResolveAudioPlaybackChannel(const std::uint32_t sound_type) {
  if (sound_type == 1 || sound_type == 5) { return PlaybackChannel::Music; }
  if (sound_type == 2) { return PlaybackChannel::Ambience; }
  if (sound_type == 3 || sound_type == 4 || sound_type == 6) { return PlaybackChannel::Dialog; }
  return PlaybackChannel::SFX;
}
inline bool CopyDriverNameToBuffer(const std::string &driver_name, char *name_out, const std::uint32_t max_len) {
  if (driver_name.empty() || !name_out || max_len == 0) { return false; }
  const std::size_t copy_len = std::min<std::size_t>(driver_name.size(), max_len - 1);
  std::memcpy(name_out, driver_name.data(), copy_len);
  name_out[copy_len] = '\0';
  return true;
}
inline openwow::ui::game::CVarValidationCallbackRegistration BindValidationCallbackAndApplyCurrentValue( openwow::ui::game::CVarSystem &cvars, const std::string &name, openwow::ui::game::CVarValidationCallback callback) {
  auto registration = cvars.RegisterValidationCallback(name, callback);
  const std::string current_value = cvars.GetCVar(name);
  if (callback) { callback(name, current_value, current_value); }
  return registration;
}
struct WorldAudioCallbackRegistrationState {
  std::mutex mutex;
  openwow::core::CallbackHandle engine_update_handle =
      openwow::core::CallbackHandle::Invalid;
  openwow::core::CallbackHandle zone_music_handle =
      openwow::core::CallbackHandle::Invalid;
  openwow::core::CallbackHandle liquid_update_handle =
      openwow::core::CallbackHandle::Invalid;
  openwow::core::CallbackHandle chaos_mode_handle =
      openwow::core::CallbackHandle::Invalid;
};
struct SoundValidationCallbackRegistrationState {
  std::array<openwow::ui::game::CVarValidationCallbackRegistration, 8> handles;
  void Reset() {
    for (auto &handle : handles) { handle.Reset(); }
  }
};
inline constexpr std::array<std::string_view, 18> kSoundTypeLabels = { "SFX", "MUSIC", "AMBIENCE", "CINEMATIC", "SCRIPTSOUND", "SCRIPTMUSIC", "RACIALCINEMATIC", "SFX", "SFX",   "SFX",      "SFX",       "SFX",         "SFX",         "SFX", "SFX", "SFX",   "SFX",      "SFX", };
inline constexpr std::array<std::uint32_t, 18> kSoundTypeMaxActiveCounts = { 0x7fffffffu, 0x7fffffffu, 0x7fffffffu, 0x7fffffffu, 0x7fffffffu, 0x7fffffffu, 1u, 1u, 2u, 1u,          2u,          2u,          1u,          6u,          4u,          1u, 2u, 4u, };
[[nodiscard]] constexpr bool IsSpecialZoneMusicPriority(const int priority) { return priority == 5 || priority == 6 || priority == 7; }
[[nodiscard]] constexpr bool UsesZoneMusicDelayGate(const int priority) { return priority == 0 || priority == 1 || priority == 2; }
inline constexpr std::uint32_t kScreenEffectAudioSelectionPriority = 10u;
inline constexpr std::uint32_t kWeatherAudioSelectionPriority = 8u;

inline constexpr std::uint32_t kUnderwaterAudioSelectionPriority = 9u;
inline constexpr std::int32_t kUnderwaterAmbienceSoundKitId = 4209;
[[nodiscard]] inline bool SoundTypeUsesSfxCVar(const std::uint32_t sound_type) { return kSoundTypeLabels[sound_type] == "SFX"; }
[[nodiscard]] inline bool SoundTypeUsesMusicCVar(const std::uint32_t sound_type) { const auto label = kSoundTypeLabels[sound_type]; return label == "MUSIC" || label == "SCRIPTMUSIC"; }
[[nodiscard]] inline bool SoundTypeUsesAmbienceCVar(const std::uint32_t sound_type) { return sound_type == 2; }
[[nodiscard]] constexpr bool IsAdvancedDuckValueActive(const float value) { return value >= 0.0f && value < 1.0f; }
[[nodiscard]] inline std::size_t ResolveAdvancedDuckChannel(const std::uint32_t sound_type) {
  if (sound_type == 1) { return 1; }
  if (sound_type == 2) { return 2; }
  return 0;
}
[[nodiscard]] inline bool SoundTypeUsesAdvancedDucking(const std::uint32_t sound_type) { return sound_type == 0 || sound_type == 1 || sound_type == 2 || sound_type >= 7; }
[[nodiscard]] inline bool IsZeroVector3(const float *position) { return position[0] == 0.0f && position[1] == 0.0f && position[2] == 0.0f; }
[[nodiscard]] inline float ComputeDistance3(const float *lhs, const float *rhs) { const float dx = lhs[0] - rhs[0]; const float dy = lhs[1] - rhs[1]; const float dz = lhs[2] - rhs[2]; return std::sqrt(dx * dx + dy * dy + dz * dz); }
inline void ResetSoundHandlePlaybackStateForRestart(SoundHandle &handle) {
  handle.playback_priority.reset(); handle.max_audible_behavior = SoundKitMaxAudibleBehavior::kStealLowest;
  handle.max_audible_mute_fade_speed = 0.5f; handle.bound_object_guid.reset(); handle.selected_file_index = -1;
  handle.sound_model_override.clear(); handle.has_active_sound = false; handle.is_playing = false; handle.loops = false;
  handle.bypass_virtual_play_window = false; handle.min_distance = 0.0f; handle.max_distance = 0.0f;
  handle.tracks_instance_limit = false; handle.tracks_exclusive_kit = false;
  handle.audio_engine_handle_id.reset(); handle.engine_pushed_channel_volume.reset(); handle.source_label.clear(); handle.selected_file_path.clear();
  handle.relative_volume = {}; handle.stop_state = {}; handle.fade_in_state = {};
  handle.fade_in_seconds = 0.0f; handle.fade_out_seconds = 0.0f;
  handle.virtual_play_state = {};
}

inline void ApplySoundKitFadeOptionsToHandle(SoundHandle &handle, const SoundKitPlaybackOptions &options) {
  if (options.fade_in_seconds >= 0.0f) { handle.fade_in_seconds = options.fade_in_seconds; }
  if (options.fade_out_seconds >= 0.0f) { handle.fade_out_seconds = options.fade_out_seconds; }
}
[[nodiscard]] inline const char *ResolvePlaybackSourceLabel(const SoundKitData &kit) { return kit.name.empty() ? "<UNKNOWN ERROR>" : kit.name.c_str(); }
[[nodiscard]] inline std::uint32_t CountLoadedSoundKitFiles(const SoundKitData &input) {
  std::uint32_t count = 0;
  for (const auto &path : input.file_paths) {
    if (path.empty()) { break; }
    ++count;
  }
  return count;
}
[[nodiscard]] inline SoundKitData BuildLoadedSoundKitData(const SoundKitData &input) {
  SoundKitData loaded{};
  loaded.id = input.id;
  loaded.name = input.name;
  loaded.volume = input.volume;
  loaded.min_distance = input.min_distance;
  loaded.max_distance = input.max_distance;
  loaded.eax_def = input.eax_def;
  loaded.dbc_sound_type = input.dbc_sound_type;
  loaded.flags = input.flags;
  loaded.advanced_id = input.advanced_id;
  loaded.file_count = CountLoadedSoundKitFiles(input);
  for (std::size_t index = 0; index < loaded.file_count; ++index) { loaded.file_paths[index] = input.file_paths[index]; loaded.frequencies[index] = input.frequencies[index]; }
  return loaded;
}
[[nodiscard]] inline bool
MatchesWorldStateZoneSoundGate(const WorldStateZoneSoundEntryData &entry, const std::function<std::uint32_t(std::uint32_t)> &resolver) { const std::uint32_t value = resolver ? resolver(entry.world_state_id) : 0u; return value == entry.world_state_value; }
inline constexpr float kDspFilterActivationDelaySeconds = 3.0f;
inline constexpr float kLowPassDspQ = 512.0f;
inline WorldReverbProperties BuildGlueWorldReverbProperties() {
  return WorldReverbProperties{
      .environment = 0,
      .room_flags = -1,
      .environment_size = 7.5f,
      .environment_diffusion = 1.0f,
      .room = -10000,
      .room_hf = -10000,
      .room_lf = 0,
      .decay_time = 1.0f,
      .decay_hf_ratio = 1.0f,
      .decay_lf_ratio = 1.0f,
      .reflections = -2602,
      .reflections_delay = 0.0070000002f,
      .reflections_pan = {0.0f, 0.0f, 0.0f},
      .reverb = 200,
      .reverb_delay = 0.011f,
      .reverb_pan = {0.0f, 0.0f, 0.0f},
      .echo_time = 0.25f,
      .echo_depth = 0.0f,
      .modulation_time = 0.25f,
      .modulation_depth = 0.0f,
      .air_absorption_hf = -5.0f,
      .hf_reference = 5000.0f,
      .lf_reference = 250.0f,
      .room_rolloff_factor = 0.0f,
      .engine_scalar_28 = 0.0f,
      .engine_scalar_29 = 0.0f,
      .flags = 831,
  };
}
inline WorldReverbProperties
BuildWorldReverbPropertiesFromSoundProvider(const SoundProviderPreferenceData &provider) {
  WorldReverbProperties properties = BuildGlueWorldReverbProperties();
  properties.environment = 0;
  properties.room_flags = provider.room_flags;
  properties.environment_size = provider.environment_size;
  properties.environment_diffusion = provider.environment_diffusion;
  properties.room = provider.room;
  properties.room_hf = provider.room_hf;
  properties.room_lf = provider.room_lf;
  properties.decay_time = provider.decay_time;
  properties.decay_hf_ratio = provider.decay_hf_ratio;
  properties.decay_lf_ratio = provider.decay_lf_ratio;
  properties.reflections = provider.reflections;
  properties.reflections_delay = provider.reflections_delay;
  properties.reflections_pan = {0.0f, 0.0f, 0.0f};
  properties.reverb = provider.reverb;
  properties.reverb_delay = provider.reverb_delay;
  properties.reverb_pan = {0.0f, 0.0f, 0.0f};
  properties.echo_time = provider.echo_time;
  properties.echo_depth = provider.echo_depth;
  properties.modulation_time = provider.modulation_time;
  properties.modulation_depth = provider.modulation_depth;
  properties.air_absorption_hf = provider.air_absorption_hf;
  properties.hf_reference = provider.hf_reference;
  properties.lf_reference = provider.lf_reference;
  properties.room_rolloff_factor = provider.room_rolloff_factor;
  properties.engine_scalar_28 = 100.0f;
  properties.engine_scalar_29 = 100.0f;
  properties.flags = 1279u;
  return properties;
}
inline int ParseDriverIndex(std::string_view value) {
  try { return std::max(0, std::stoi(std::string(value))); } catch (...) { return 0; }
}
inline bool ContainsExactDeviceName(const std::vector<std::string> &devices, std::string_view device_name) { return std::find(devices.begin(), devices.end(), device_name) != devices.end(); }
inline constexpr std::int32_t kRoomLfMinimumMillibels = -10000;
inline constexpr std::int32_t kRoomLfMaximumMillibels = 0;
inline constexpr float kLfReferenceMinimumHz = 20.0f;
inline constexpr float kLfReferenceMaximumHz = 1000.0f;
[[nodiscard]] inline std::int32_t ClampRoomLfMillibels(const std::int32_t room_lf) { return std::clamp(room_lf, kRoomLfMinimumMillibels, kRoomLfMaximumMillibels); }
[[nodiscard]] inline float ClampLfReferenceHz(const float lf_reference_hz) { return std::clamp(lf_reference_hz, kLfReferenceMinimumHz, kLfReferenceMaximumHz); }
[[nodiscard]] inline WorldReverbRoomLfDspState BuildWorldReverbRoomLfDspState( const WorldReverbProperties &properties, const float sample_rate_hz) {
  WorldReverbRoomLfDspState state;
  state.clamped_room_lf = ClampRoomLfMillibels(properties.room_lf);
  state.clamped_lf_reference = ClampLfReferenceHz(properties.lf_reference);
  state.room_lf_gain_db = static_cast<float>(state.clamped_room_lf) * 0.0099999998f;
  const double safe_sample_rate = sample_rate_hz > 0.0f ? sample_rate_hz : 44100.0f;
  const double k = std::tan(static_cast<double>(state.clamped_lf_reference) / safe_sample_rate * std::numbers::pi);
  const double low_shelf_gain =
      std::exp(static_cast<double>(state.room_lf_gain_db) * 0.057564627);
  const double scaled_k = k / low_shelf_gain;
  const double normalization = 1.0 / (((scaled_k + 1.4142135) * scaled_k) + 1.0);
  state.low_shelf.b0 = static_cast<float>((((k + 1.4142135) * k) + 1.0) * normalization);
  state.low_shelf.b1 = static_cast<float>(((k * k) - 1.0) * normalization * 2.0);
  state.low_shelf.b2 = static_cast<float>(((k * (k - 1.4142135)) + 1.0) * normalization);
  state.low_shelf.a1 =
      static_cast<float>(-2.0 * (((scaled_k * scaled_k) - 1.0) * normalization));
  state.low_shelf.a2 =
      static_cast<float>(-normalization * ((scaled_k * (scaled_k - 1.4142135)) + 1.0));
  return state;
}
struct WorldReverbMixerControls {
  float room_size{0.5f};
  float damping{0.5f};
  float wet{0.0f};
  float dry{0.5f};
  float width{1.0f};
};
[[nodiscard]] inline float Clamp01(const float value) { return std::clamp(value, 0.0f, 1.0f); }
[[nodiscard]] inline float MillibelsToLinearAmplitude(const std::int32_t millibels) {
  if (millibels <= -10000) { return 0.0f; }
  return static_cast<float>(std::pow(10.0, static_cast<double>(millibels) * 0.0005));
}
[[nodiscard]] inline WorldReverbMixerControls
BuildWorldReverbMixerControls(const WorldReverbProperties &properties) {
  constexpr float kMinDecaySeconds = 0.1f;
  constexpr float kMaxMappedDecaySeconds = 20.0f;
  const float decay_time =
      std::clamp(properties.decay_time, kMinDecaySeconds, kMaxMappedDecaySeconds);
  const float decay_position =
      (std::log10(decay_time) - std::log10(kMinDecaySeconds)) /
      (std::log10(kMaxMappedDecaySeconds) - std::log10(kMinDecaySeconds));
  return WorldReverbMixerControls{ .room_size = Clamp01(decay_position), .damping = Clamp01(1.0f - properties.decay_hf_ratio * 0.5f), .wet = Clamp01(MillibelsToLinearAmplitude(properties.room + properties.reverb)), .dry = 0.5f, .width = Clamp01(properties.environment_diffusion), };
}
inline std::string DefaultEnumeratedDeviceName(const std::vector<std::string> &devices) {
  if (devices.empty()) {
    return {};
  }
  return devices.front();
}
inline std::string ResolveActiveEnumeratedDeviceName(const int selected_index, std::string_view selected_device_name, std::string_view enumerated_default_name) {
  if (selected_index == 0) { return std::string(enumerated_default_name); }
  return std::string(selected_device_name);
}
[[nodiscard]] inline bool IsNormalizedVolume(const float volume) { return volume >= 0.0f && volume <= 1.0f; }
inline void UpsertDspParameterWrite(std::vector<DspParameterWrite> &writes, const std::uint32_t index, const float value) {
  const auto it =
      std::find_if(writes.begin(), writes.end(), [index](const DspParameterWrite &write) { return write.index == index; });
  if (it != writes.end()) { it->value = value; return; }
  writes.push_back(DspParameterWrite{.index = index, .value = value});
}
inline void AppendInputParameterWriteIfPresent(std::vector<DspParameterWrite> &writes, const std::uint32_t parameter_index, std::span<const float> params, const std::size_t input_index) {
  if (input_index >= params.size()) { return; }
  writes.push_back(DspParameterWrite{.index = parameter_index, .value = params[input_index]});
}
[[nodiscard]] inline std::vector<DspParameterWrite>
BuildAppliedDspParameterWrites(const DspEffectType type, std::span<const float> params) {
  std::vector<DspParameterWrite> writes;
  switch (type) {
  case DspEffectType::kLowPass:
    AppendInputParameterWriteIfPresent(writes, 0, params, 1);
    writes.push_back(DspParameterWrite{.index = 1, .value = kLowPassDspQ});
    break;
  case DspEffectType::kHighPass:
  case DspEffectType::kEcho:
    AppendInputParameterWriteIfPresent(writes, 0, params, 1);
    AppendInputParameterWriteIfPresent(writes, 1, params, 2);
    break;
  case DspEffectType::kParametricEQ:
    for (std::uint32_t parameter_index = 0; parameter_index < 8; ++parameter_index) { AppendInputParameterWriteIfPresent(writes, parameter_index, params, parameter_index + 1); }
    break;
  case DspEffectType::kCompressor:
    AppendInputParameterWriteIfPresent(writes, 0, params, 1);
    AppendInputParameterWriteIfPresent(writes, 1, params, 2);
    AppendInputParameterWriteIfPresent(writes, 3, params, 3);
    AppendInputParameterWriteIfPresent(writes, 4, params, 4);
    break;
  case DspEffectType::kNone:
  case DspEffectType::kVolume:
  default:
    break;
  }
  return writes;
}
[[nodiscard]] inline bool DspEffectStartsEnabled(std::span<const float> params) { return !params.empty() && params.front() > 0.0f; }
[[nodiscard]] inline bool DspEffectStartsBypassed(std::span<const float> params) { return !params.empty() && params.front() == 0.0f; }

[[nodiscard]] inline std::uint32_t ResolvePlaybackSoundType(const SoundKitPlaybackOptions &options) { return options.sound_type; }
[[nodiscard]] inline std::int32_t ResolvePlaybackPreloadQueueHint(const std::uint32_t sound_type, const SoundKitPlaybackOptions &options) { return options.preload_queue_hint.value_or(ResolveDataPreloadQueueForSoundType(sound_type)); }

[[nodiscard]] inline bool ResolveExclusiveRepeatTracking(const SoundKitData &kit, const SoundKitPlaybackOptions &options) {
  (void)options;
  return (kit.flags & kSoundKitFlagExclusiveRepeat) != 0;
}
[[nodiscard]] inline bool ResolveLoopingPlayback(const SoundKitData &kit, const SoundKitPlaybackOptions &options) {
  if (options.force_ambient_loop) { return true; }
  switch (options.loop_mode) {
  case SoundLoopMode::kForceLoop:
    return true;
  case SoundLoopMode::kForceOneShot:
    return false;
  case SoundLoopMode::kUseSoundKit:
  default:
    return (kit.flags & kSoundKitFlagLoop) != 0;
  }
}
[[nodiscard]] inline SoundKitMaxAudibleBehavior
ResolveMaxAudibleBehavior(const SoundKitPlaybackOptions &options) {

  return options.force_ambient_loop ? SoundKitMaxAudibleBehavior::kMuteAndContinue
                                    : options.max_audible_behavior;
}
inline void EnsurePlaybackRandomStateSeeded(retail_rng::AdlerSeedState &state) {
  if (state.value != 0u || state.packed != 0u) { return; }
  std::uint32_t processor_count = std::thread::hardware_concurrency();
  if (processor_count == 0u) { processor_count = 1u; }
  state = retail_rng::MakeAdlerSeedState(processor_count);
}
[[nodiscard]] inline bool
UsesReadySoundFile(const openwow::vfs::DataPreloadPathReadyState ready_state) {
  return ready_state != openwow::vfs::DataPreloadPathReadyState::kUnavailable &&
         ready_state != openwow::vfs::DataPreloadPathReadyState::kPartial;
}
[[nodiscard]] inline bool
KeepsPendingPreloadRequest(const openwow::vfs::DataPreloadPathReadyState ready_state) {
  return ready_state == openwow::vfs::DataPreloadPathReadyState::kUnavailable ||
         ready_state == openwow::vfs::DataPreloadPathReadyState::kPartial;
}
[[nodiscard]] inline bool IsStreamingPlaybackSelectionEnabled() {

  return openwow::data::IsStreamingInitialized();
}
inline void ResetDeviceSelectionToDefault(openwow::ui::game::CVarSystem &cvars,
                                   std::string_view index_cvar,
                                   std::string_view name_cvar,
                                   const std::vector<std::string> &devices) {
  (void)cvars.SetCVar(std::string(index_cvar), "0", true);
  (void)cvars.SetCVar(std::string(name_cvar), DefaultEnumeratedDeviceName(devices),
                     true);
}
[[nodiscard]] inline bool ActiveDeviceIsEnumerated(const std::vector<std::string> &devices,
                                            std::string_view active_device_name) {
  return ContainsExactDeviceName(devices, active_device_name);
}
[[nodiscard]] inline bool SystemDefaultSelectionChanged(const int selected_index,
                                                 std::string_view active_device_name,
                                                 std::string_view enumerated_default_name) {
  return selected_index == 0 && active_device_name != enumerated_default_name &&
         !enumerated_default_name.empty();
}
}

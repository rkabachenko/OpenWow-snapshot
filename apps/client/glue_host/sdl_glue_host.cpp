#include "sdl_glue_host.h"

#include "openwow/audio/playback/audio_engine.h"
#include "openwow/audio/playback/sound_interface.h"
#include "openwow/runtime/scheduling/frame_scheduler.h"
#include "openwow/core/storm_cmd.h"
#include "openwow/core/storm_string.h"
#include "composition/client_helpers.h"
#include "openwow/platform/window/window_manager.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <optional>
#include <string>

namespace openwow::client {

namespace {

constexpr std::size_t kRetailGlueSoundKitNameCapacity = 128;

[[nodiscard]] std::string CopyRetailGlueSoundKitName(const std::string& name) {
  std::array<char, kRetailGlueSoundKitNameCapacity> bounded_name{};
  openwow::core::SStrCopy(bounded_name.data(), name.c_str(), bounded_name.size());
  return bounded_name.data();
}

[[nodiscard]] std::string NormalizeGlueSoundPath(std::string path) {
  std::replace(path.begin(), path.end(), '\\', '/');
  if (!path.empty() && path.front() != '/') {
    path.insert(path.begin(), '/');
  }
  return path;
}

template <typename StartFn>
[[nodiscard]] bool TryPlayGlueSoundPath(const std::string& path, StartFn&& start) {
  if (path.empty()) {
    return false;
  }

  if (start(path)) {
    return true;
  }

  if (path.front() == '/') {
    return start(path.substr(1));
  }

  return false;
}

}

SdlGlueHost::SdlGlueHost(const openwow::vfs::VirtualFileSystem* vfs,
                         openwow::audio::SoundRuntime& sound_runtime)
    : vfs_(vfs), sound_runtime_(sound_runtime) {}

SdlGlueHost::~SdlGlueHost() {
  UnregisterMusicRestartMonitor(RestartingMusicChannel::kGlueMusic);
  UnregisterMusicRestartMonitor(RestartingMusicChannel::kCredits);
}

bool SdlGlueHost::InitializeAudio() {
  EnsureAudioEngine();
  return audio_initialized_;
}

void SdlGlueHost::BeginAudioDevicePrepare() {

  if (openwow::core::StormCmd::Instance().IsCommandEnabled(
          openwow::core::StartupCommandId::kNoSound)) {
    return;
  }

  const auto& cvars = openwow::ui::game::CVarSystem::Instance();
  const int output_quality = cvars.GetCVarInt("Sound_OutputQuality");
  const int sample_rate = output_quality == 0 ? 22050
                          : output_quality == 2 ? 48000
                                                : 44100;

  if (audio_device_prepare_.Begin([this, sample_rate] {

        return sound_runtime_.PrepareOutputDevice(
            sample_rate, 2, 4096);
      })) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                       "Audio device prepare: started asynchronously");
  }
}

void SdlGlueHost::FinishAudioDevicePrepare() {
  if (audio_device_prepare_reported_) {
    return;
  }
  const auto result = audio_device_prepare_.Finish();
  if (!result.has_value()) {
    return;
  }
  audio_device_prepare_reported_ = true;

  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              result->elapsed)
                              .count();
  openwow::diagnostics::Log(
      result->succeeded ? openwow::diagnostics::LogLevel::kInfo
                        : openwow::diagnostics::LogLevel::kWarn,
      "Audio device prepare: completed success=" +
          std::to_string(result->succeeded ? 1 : 0) +
          " elapsed_ms=" + std::to_string(elapsed_ms));
}

void SdlGlueHost::EnsureAudioEngine() {

  FinishAudioDevicePrepare();

  auto& engine = sound_runtime_;
  if (audio_initialized_ && engine.IsOutputDeviceReady()) return;
  audio_initialized_ = false;
  const int initialize_result =
      sound_runtime_.InitializeFull(false);
  if (initialize_result != 0 || !engine.IsOutputDeviceReady()) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kError,
        "SoundRuntime initialization failed: result=" +
            std::to_string(initialize_result) + " SDL=" + SDL_GetError());
    return;
  }

  const char* driver = SDL_GetCurrentAudioDriver();
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     std::string("SoundRuntime initialized — SDL driver: ") +
                         (driver ? driver : "(null)"));

  if (vfs_ != nullptr) {
    engine.SetVfsLoader([this](const std::string& path)
        -> std::optional<std::vector<std::uint8_t>> {
      return vfs_->ReadFileBytes(path);
    });
  }

  audio_initialized_ = true;
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "SoundRuntime: audio fully initialized, CVar volumes applied");
}

void SdlGlueHost::PrepareMoviePlayback() {
  EnsureAudioEngine();
}

void SdlGlueHost::PlayGlueMusic(const std::string& track) {
  EnsureAudioEngine();
  auto& engine = sound_runtime_;
  if (!engine.IsOutputDeviceReady() || vfs_ == nullptr) return;

  const std::uint32_t requested_sound_kit_id = engine.LookupSoundKitIdByName(track);
  if (engine.IsMusicFilePlaying() && requested_sound_kit_id != 0 &&
      current_glue_music_sound_kit_id_ == requested_sound_kit_id) {
    return;
  }

  UnregisterMusicRestartMonitor(RestartingMusicChannel::kCredits);
  engine.StopCreditsFile(3000.0f);

  if (openwow::core::SStrCmpNoCase(track.c_str(), current_glue_music_track_.c_str(),
                                  0x7fffffffu) == 0) {
    return;
  }

  UnregisterMusicRestartMonitor(RestartingMusicChannel::kGlueMusic);
  current_glue_music_track_.clear();
  current_glue_music_sound_kit_id_.reset();
  engine.StopMusicFile(0.0f);

  current_glue_music_track_ = CopyRetailGlueSoundKitName(track);
  const std::uint32_t bounded_sound_kit_id =
      engine.LookupSoundKitIdByName(current_glue_music_track_);
  if (bounded_sound_kit_id != 0) {
    current_glue_music_sound_kit_id_ = bounded_sound_kit_id;
  }
  (void)TryPlayRestartingMusicTrack(current_glue_music_track_,
                                    RestartingMusicChannel::kGlueMusic,
                                    false);
  RegisterMusicRestartMonitor(RestartingMusicChannel::kGlueMusic);
}

void SdlGlueHost::StopGlueMusic() {

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kDebug,
                     "StopGlueMusic called (current track='" + current_glue_music_track_ + "')");
  UnregisterMusicRestartMonitor(RestartingMusicChannel::kCredits);
  UnregisterMusicRestartMonitor(RestartingMusicChannel::kGlueMusic);
  auto& engine = sound_runtime_;
  if (engine.IsOutputDeviceReady()) {
    engine.StopCreditsFile(3000.0f);
    engine.StopMusicFile(0.0f);
  }
  current_glue_music_track_.clear();
  current_glue_music_sound_kit_id_.reset();
}

void SdlGlueHost::PlayGlueAmbience(const std::string& track, double fade_seconds) {
  EnsureAudioEngine();
  auto& engine = sound_runtime_;
  if (!engine.IsOutputDeviceReady() || vfs_ == nullptr) return;

  const std::uint32_t requested_sound_kit_id = engine.LookupSoundKitIdByName(track);
  const std::optional<std::uint32_t> requested_sound_kit =
      requested_sound_kit_id != 0 ? std::optional<std::uint32_t>{requested_sound_kit_id}
                                  : std::nullopt;

  if (glue_ambience_state_.ShouldSuppressRestart(engine.IsAmbienceFilePlaying(),
                                                 requested_sound_kit)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kDebug,
                       "PlayGlueAmbience: same track already playing, skipping");
    return;
  }

  engine.StopAmbienceFile(3000.0f);

  const std::string retail_track = CopyRetailGlueSoundKitName(track);
  const std::uint32_t sound_kit_id = engine.LookupSoundKitIdByName(retail_track);
  const auto* kit = engine.GetSoundKitData(sound_kit_id);
  if (kit == nullptr || kit->file_count == 0) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "PlayGlueAmbience: unknown or empty SoundEntries name '" +
                           retail_track + "'");
    return;
  }

  const auto selected_file = engine.SelectSoundKitFileForPlayback(
      sound_kit_id,
      openwow::audio::SoundKitVariationSelectionMode::kConsumeFrequenciesAcrossCalls,
      std::nullopt, openwow::audio::ResolveDataPreloadQueueForSoundType(2));
  if (!selected_file.has_value()) {
    return;
  }
  const std::string path = NormalizeGlueSoundPath(std::string(selected_file->path));
  const float volume = engine.ResolveSoundKitPlaybackVolume(*kit).effective_volume();

  const float retail_fade_seconds = static_cast<float>(fade_seconds);
  const float fade_in_ms =
      (retail_fade_seconds >= 0.0f) ? retail_fade_seconds * 1000.0f : 0.0f;

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kDebug,
                     "PlayGlueAmbience: track='" + retail_track + "' resolved='"
                         + path + "' fade=" + std::to_string(retail_fade_seconds)
                         + " fade_in_ms=" + std::to_string(fade_in_ms));

  if (TryPlayGlueSoundPath(
          path,
          [&](const std::string& candidate) {
            return engine.PlayAmbienceFile(candidate, volume, fade_in_ms);
          })) {
    glue_ambience_state_.RememberStartedTrack(retail_track, sound_kit_id);
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kDebug,
                       "Audio started: glue-ambience path=" + path);
    return;
  }

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                     "Audio start failed: glue-ambience track='" + retail_track + "'");
}

void SdlGlueHost::StopGlueAmbience() {

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kDebug,
                     "StopGlueAmbience called (current track='"
                         + glue_ambience_state_.current_track_name() + "')");
  auto& engine = sound_runtime_;
  if (engine.IsOutputDeviceReady()) {
    engine.StopAmbienceFile(1000.0f);
  }
}

void SdlGlueHost::PlayCreditsMusic(const std::string& track) {
  EnsureAudioEngine();
  auto& engine = sound_runtime_;

  UnregisterMusicRestartMonitor(RestartingMusicChannel::kCredits);
  if (engine.IsOutputDeviceReady()) {
    engine.StopCreditsFile(3000.0f);
  }

  UnregisterMusicRestartMonitor(RestartingMusicChannel::kGlueMusic);
  current_glue_music_track_.clear();
  current_glue_music_sound_kit_id_.reset();

  if (engine.IsOutputDeviceReady()) {
    engine.StopMusicFile(3000.0f);
  }

  const std::string retail_track = CopyRetailGlueSoundKitName(track);
  current_credits_music_track_ = retail_track;
  (void)TryPlayRestartingMusicTrack(retail_track, RestartingMusicChannel::kCredits,
                                    true);
  RegisterMusicRestartMonitor(RestartingMusicChannel::kCredits);
}

void SdlGlueHost::StopCreditsMusic() {
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kDebug, "StopCreditsMusic called");
  UnregisterMusicRestartMonitor(RestartingMusicChannel::kCredits);
  ClearCreditsPlaybackState();
  auto& engine = sound_runtime_;
  if (engine.IsOutputDeviceReady()) {
    engine.StopCreditsFile(3000.0f);
    engine.StopMusicFile(0.0f);
  }
}

bool SdlGlueHost::TryPlayRestartingMusicTrack(
    const std::string& track, const RestartingMusicChannel channel,
    const bool reset_variation_state) {
  auto& engine = sound_runtime_;
  if (!engine.IsOutputDeviceReady() || vfs_ == nullptr) {
    return false;
  }

  const auto sound_kit_id = engine.LookupSoundKitIdByName(track);
  if (sound_kit_id == 0) {
    return false;
  }

  if (reset_variation_state) {
    engine.ResetSoundKitVariationSelectionState(sound_kit_id);
  }

  const auto* kit = engine.GetSoundKitData(sound_kit_id);
  if (kit == nullptr || kit->file_count == 0) {
    return false;
  }

  const auto selected_file = engine.SelectSoundKitFileForPlayback(
      sound_kit_id, openwow::audio::SoundKitVariationSelectionMode::kConsumeFrequenciesAcrossCalls,
      std::nullopt, openwow::audio::ResolveDataPreloadQueueForSoundType(1));
  if (!selected_file.has_value()) {
    return false;
  }

  const std::string path = NormalizeGlueSoundPath(std::string(selected_file->path));
  const bool loop = (kit->flags & 0x200u) != 0;
  const float volume = engine.ResolveSoundKitPlaybackVolume(*kit).effective_volume();

  return TryPlayGlueSoundPath(path, [&](const std::string& candidate) {
    if (channel == RestartingMusicChannel::kGlueMusic) {
      return engine.PlayMusicFile(candidate, loop, 0.0f, volume);
    }
    return engine.PlayCreditsFile(candidate, loop, volume);
  });
}

void SdlGlueHost::RegisterMusicRestartMonitor(const RestartingMusicChannel channel) {
  auto& handle = channel == RestartingMusicChannel::kGlueMusic
                     ? glue_music_restart_handle_
                     : credits_restart_handle_;
  if (handle != openwow::core::CallbackHandle::Invalid) {
    return;
  }

  handle = openwow::core::FrameScheduler::Instance().Register(
      openwow::core::Phase::Update,
      7,
      [this, channel](double delta_seconds) { PollMusicRestart(channel, delta_seconds); },
      channel == RestartingMusicChannel::kGlueMusic
          ? "SdlGlueHost::PollGlueMusicRestart"
          : "SdlGlueHost::PollCreditsRestart");
}

void SdlGlueHost::UnregisterMusicRestartMonitor(const RestartingMusicChannel channel) {
  auto& handle = channel == RestartingMusicChannel::kGlueMusic
                     ? glue_music_restart_handle_
                     : credits_restart_handle_;
  if (handle == openwow::core::CallbackHandle::Invalid) {
    return;
  }

  openwow::core::FrameScheduler::Instance().Unregister(handle);
  handle = openwow::core::CallbackHandle::Invalid;
}

void SdlGlueHost::ClearCreditsPlaybackState() {
  current_credits_music_track_.clear();
}

void SdlGlueHost::PollMusicRestart(const RestartingMusicChannel channel,
                                   const double delta_seconds) {
  (void)delta_seconds;

  auto& engine = sound_runtime_;
  const bool is_playing = channel == RestartingMusicChannel::kGlueMusic
                              ? engine.IsMusicFilePlaying()
                              : engine.IsCreditsFilePlaying();
  if (!engine.IsOutputDeviceReady() || is_playing) {
    return;
  }

  const std::string& track = channel == RestartingMusicChannel::kGlueMusic
                                 ? current_glue_music_track_
                                 : current_credits_music_track_;
  (void)TryPlayRestartingMusicTrack(track, channel, false);
}

void SdlGlueHost::OpenUrl(const std::string& url) {
  if (url.empty()) {
    return;
  }

  (void)SDL_OpenURL(url.c_str());
}

void SdlGlueHost::SetCursorVisible(bool visible) {
  if (cursor_visibility_hook_) {

    cursor_visibility_hook_(visible);
    return;
  }
  SDL_ShowCursor(visible ? SDL_ENABLE : SDL_DISABLE);
}

std::pair<double, double> SdlGlueHost::GetCursorPositionDdc(int viewport_width,
                                                            int viewport_height) const {
  int x = 0;
  int y = 0;

  if (const auto cursor =
          openwow::platform::WindowManager::Get().ResolveLogicalCursorPosition();
      cursor.has_value()) {
    x = cursor->first;
    y = cursor->second;
  } else {
    SDL_GetMouseState(&x, &y);
  }

  if (window_ != nullptr) {
    ScaleMouseToDrawable(window_, x, y);
  }

  const double ddc_x = static_cast<double>(x);
  const double ddc_y = static_cast<double>(viewport_height - y);
  (void)viewport_width;
  return {ddc_x, ddc_y};
}

bool SdlGlueHost::IsShiftKeyDown() const {
  const SDL_Keymod mod = SDL_GetModState();
  return (mod & KMOD_SHIFT) != 0;
}

}

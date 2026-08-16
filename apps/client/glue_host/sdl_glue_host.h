#pragma once

#include "glue_sound_playback_state.h"
#include "openwow/audio/lifecycle/startup_audio_device_prepare.h"
#include "openwow/runtime/scheduling/frame_scheduler.h"
#include "openwow/ui/glue/glue_host.h"
#include "openwow/vfs/virtual_file_system.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <functional>

struct SDL_Window;

namespace openwow::audio { class SoundRuntime; }

namespace openwow::client {

class SdlGlueHost final : public openwow::ui::glue::GlueHost {
 public:
  SdlGlueHost(const openwow::vfs::VirtualFileSystem* vfs,
              openwow::audio::SoundRuntime& sound_runtime);
  ~SdlGlueHost() override;

  void SetCursorVisibilityHook(std::function<void(bool)> hook) {
    cursor_visibility_hook_ = std::move(hook);
  }

  void SetWindow(SDL_Window* window) { window_ = window; }

  [[nodiscard]] bool InitializeAudio();

  void BeginAudioDevicePrepare();

  void FinishAudioDevicePrepare();

  void PrepareMoviePlayback() override;

  void PlayGlueMusic(const std::string& track) override;
  void StopGlueMusic() override;

  void PlayGlueAmbience(const std::string& track, double fade_seconds) override;
  void StopGlueAmbience() override;

  void PlayCreditsMusic(const std::string& track) override;
  void StopCreditsMusic() override;

  void OpenUrl(const std::string& url) override;
  void SetCursorVisible(bool visible) override;

  std::pair<double, double> GetCursorPositionDdc(int viewport_width,
                                                 int viewport_height) const override;
  bool IsShiftKeyDown() const override;

 private:
  std::function<void(bool)> cursor_visibility_hook_;
  enum class RestartingMusicChannel : std::uint8_t {
    kGlueMusic,
    kCredits,
  };

  const openwow::vfs::VirtualFileSystem* vfs_{nullptr};
  openwow::audio::SoundRuntime& sound_runtime_;
  SDL_Window* window_{nullptr};
  bool audio_initialized_{false};
  openwow::audio::StartupAudioDevicePrepare audio_device_prepare_;
  bool audio_device_prepare_reported_{false};

  std::string current_glue_music_track_;
  std::optional<std::uint32_t> current_glue_music_sound_kit_id_;
  openwow::core::CallbackHandle glue_music_restart_handle_{
      openwow::core::CallbackHandle::Invalid};

  GlueSoundPlaybackState glue_ambience_state_;

  std::string current_credits_music_track_;
  openwow::core::CallbackHandle credits_restart_handle_{
      openwow::core::CallbackHandle::Invalid};

  void EnsureAudioEngine();

  [[nodiscard]] bool TryPlayRestartingMusicTrack(const std::string& track,
                                                 RestartingMusicChannel channel,
                                                 bool reset_variation_state);
  void RegisterMusicRestartMonitor(RestartingMusicChannel channel);
  void UnregisterMusicRestartMonitor(RestartingMusicChannel channel);
  void ClearCreditsPlaybackState();
  void PollMusicRestart(RestartingMusicChannel channel, double delta_seconds);
};

}

#pragma once

#include "openwow/media/adapters/ffmpeg/movie_decoder.h"
#include "openwow/media/subtitles/subtitle_parser.h"
#include "openwow/audio/playback/movie_audio_source.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace openwow::vfs {
class VirtualFileSystem;
}

namespace openwow::media {

struct MovieEvent {
  enum Type {
    kFinished,
    kShowSubtitle,
    kHideSubtitle,
  };
  Type type;
  std::string text;
};

enum class MovieStopReason : std::uint8_t {
  kUserRequest,
  kNaturalCompletion,
  kOwnerTeardown,
  kReplaced,
};

class MoviePlayer {
 public:
  MoviePlayer();
  ~MoviePlayer();
  MoviePlayer(const MoviePlayer&) = delete;
  MoviePlayer& operator=(const MoviePlayer&) = delete;

  bool Start(const std::string& avi_path, int volume,
             const openwow::vfs::VirtualFileSystem* vfs,
             const std::filesystem::path& data_dir,
             int audio_sample_rate = 44100, int audio_channels = 2);

  void Stop(MovieStopReason reason = MovieStopReason::kUserRequest);

  void Update(double elapsed_seconds,
              std::optional<double> clock_seconds_override = std::nullopt);

  [[nodiscard]] bool IsPlaying() const noexcept { return playing_; }

  [[nodiscard]] const std::uint8_t* CurrentFrameRGBA() const noexcept;

  [[nodiscard]] int FrameWidth() const noexcept { return frame_width_; }

  [[nodiscard]] int FrameHeight() const noexcept { return frame_height_; }

  [[nodiscard]] std::uint32_t FrameVersion() const noexcept {
    return frame_version_;
  }

  [[nodiscard]] std::shared_ptr<openwow::audio::IMovieAudioSource>
  AudioSource() const noexcept;

  [[nodiscard]] bool HasAudioSource() const noexcept {
    return audio_source_ != nullptr;
  }

  [[nodiscard]] std::optional<openwow::audio::IMovieAudioSource::Stats>
  AudioStats() const noexcept;

  [[nodiscard]] int AudioSampleRate() const noexcept;

  [[nodiscard]] int AudioChannels() const noexcept;

  [[nodiscard]] int Volume() const noexcept { return volume_; }

  [[nodiscard]] float VolumeNormalized() const noexcept;

  std::vector<MovieEvent> ConsumeEvents();

  [[nodiscard]] double FrameRate() const noexcept;

  [[nodiscard]] double DurationSeconds() const noexcept;

  [[nodiscard]] int VideoWidth() const noexcept;

  [[nodiscard]] int VideoHeight() const noexcept;

 private:

  bool OpenMovieFile(const std::string& avi_path,
                     const openwow::vfs::VirtualFileSystem* vfs,
                     const std::filesystem::path& data_dir);

  void AdvanceVideoToTime(double target_seconds);

  void UpdateSubtitles(double current_seconds);

  MovieDecoder decoder_;
  std::optional<SubtitleTrack> subtitles_;
  std::shared_ptr<openwow::audio::IMovieAudioSource> audio_source_;
  MovieInfo info_;

  std::vector<std::uint8_t> current_rgba_;
  int frame_width_{0};
  int frame_height_{0};
  std::uint32_t frame_version_{0};

  bool playing_{false};
  double playback_time_{0.0};
  double next_frame_time_{0.0};
  int volume_{0};

  int active_subtitle_idx_{-1};

  std::vector<MovieEvent> pending_events_;
  bool completion_emitted_{false};

  void ReleasePlaybackResources();
  void EmitCompletionOnce();
};

}

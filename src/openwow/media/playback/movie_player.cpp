
#include "openwow/media/playback/movie_player.h"
#include "openwow/vfs/virtual_file_system.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iterator>

namespace openwow::media {

MoviePlayer::MoviePlayer() = default;
MoviePlayer::~MoviePlayer() { Stop(MovieStopReason::kOwnerTeardown); }

bool MoviePlayer::Start(const std::string& avi_path, int volume,
                         const openwow::vfs::VirtualFileSystem* vfs,
                         const std::filesystem::path& data_dir,
                         const int audio_sample_rate,
                         const int audio_channels) {
  Stop(MovieStopReason::kReplaced);
  pending_events_.clear();
  completion_emitted_ = false;

  volume_ = std::clamp(volume, 0, 255);

  if (!OpenMovieFile(avi_path, vfs, data_dir)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                        "MoviePlayer::Start: failed to open '" + avi_path + "'");
    return false;
  }

  info_ = decoder_.GetInfo();
  if (info_.video_width <= 0 || info_.video_height <= 0) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                        "MoviePlayer::Start: invalid video dimensions");
    decoder_.Close();
    return false;
  }

  frame_width_ = info_.video_width;
  frame_height_ = info_.video_height;

  audio_source_ = decoder_.CreateAudioSource(audio_sample_rate, audio_channels);

  {

    std::string sbt_path = avi_path;
    std::replace(sbt_path.begin(), sbt_path.end(), '\\', '/');
    sbt_path += ".sbt";

    if (vfs) {
      if (auto bytes = vfs->ReadFileBytes(sbt_path); bytes.has_value()) {
        subtitles_ = SubtitleTrack::Parse(*bytes);
      }
    }
    if (!subtitles_.has_value()) {

      auto fs_path = data_dir / sbt_path;
      if (std::filesystem::exists(fs_path)) {

        std::ifstream f(fs_path, std::ios::binary);
        if (f.is_open()) {
          std::vector<std::uint8_t> bytes(
              (std::istreambuf_iterator<char>(f)),
              std::istreambuf_iterator<char>());
          subtitles_ = SubtitleTrack::Parse(bytes);
        }
      }
    }
  }

  if (auto frame = decoder_.DecodeNextVideoFrame(); frame.has_value()) {
    current_rgba_ = std::move(frame->rgba);
    ++frame_version_;
  }

  playing_ = true;
  playback_time_ = 0.0;
  next_frame_time_ = (info_.frame_rate > 0.0)
                         ? 1.0 / info_.frame_rate
                         : 1.0 / 15.0;
  active_subtitle_idx_ = -1;

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
      "MoviePlayer: started '" + avi_path + "' " +
      std::to_string(info_.video_width) + "x" +
      std::to_string(info_.video_height) + " @ " +
      std::to_string(info_.frame_rate) + " fps, " +
      std::to_string(info_.duration_seconds) + "s, " +
      "audio=" + (audio_source_ != nullptr ? "streaming" : "none") + ", " +
      "subtitles=" + (subtitles_.has_value()
          ? std::to_string(subtitles_->size()) : "none"));
  return true;
}

void MoviePlayer::EmitCompletionOnce() {
  if (!completion_emitted_) {
    pending_events_.push_back({MovieEvent::kFinished, {}});
    completion_emitted_ = true;
  }
}

void MoviePlayer::ReleasePlaybackResources() {
  playing_ = false;
  if (active_subtitle_idx_ >= 0) {
    pending_events_.push_back({MovieEvent::kHideSubtitle, {}});
    active_subtitle_idx_ = -1;
  }
  audio_source_.reset();
  decoder_.Close();
  subtitles_.reset();
  current_rgba_.clear();
  frame_width_ = 0;
  frame_height_ = 0;
  playback_time_ = 0.0;
  next_frame_time_ = 0.0;
  info_ = {};
}

void MoviePlayer::Stop(const MovieStopReason reason) {
  const bool was_playing = playing_;
  ReleasePlaybackResources();
  if (was_playing &&
      (reason == MovieStopReason::kUserRequest ||
       reason == MovieStopReason::kNaturalCompletion)) {
    EmitCompletionOnce();
  }
}

void MoviePlayer::Update(double elapsed_seconds, std::optional<double> clock_seconds_override) {
  if (!playing_) return;

  if (clock_seconds_override.has_value()) {
    playback_time_ = std::max(0.0, *clock_seconds_override);
  } else {
    playback_time_ += elapsed_seconds;
  }

  AdvanceVideoToTime(playback_time_);

  if (decoder_.IsEndOfStream()) {
    Stop(MovieStopReason::kNaturalCompletion);
    return;
  }

  UpdateSubtitles(playback_time_);
}

bool MoviePlayer::OpenMovieFile(const std::string& avi_path,
                                 const openwow::vfs::VirtualFileSystem* vfs,
                                 const std::filesystem::path& data_dir) {

  std::string path = avi_path;
  std::replace(path.begin(), path.end(), '\\', '/');
  const std::string avi_file = path + ".avi";

  if (vfs) {
    if (auto bytes = vfs->ReadFileBytes(avi_file); bytes.has_value()) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kDebug,
          "MoviePlayer: loading from VFS: " + avi_file);
      return decoder_.OpenOwned(std::move(*bytes));
    }
  }

  if (!data_dir.empty()) {

    auto direct_path = data_dir / avi_file;
    if (std::filesystem::exists(direct_path)) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kDebug,
          "MoviePlayer: loading from filesystem: " + direct_path.string());
      return decoder_.OpenPath(direct_path.string());
    }

    static constexpr std::array<const char*, 16> kLocales = {
        "enUS", "enGB", "deDE", "frFR", "esES", "esMX",
        "ptBR", "ruRU", "zhCN", "zhTW", "koKR", "itIT",
        "ptPT", "enCN", "enTW", "jaJP"
    };
    for (const char* locale : kLocales) {
      auto locale_path = data_dir / locale / avi_file;
      if (std::filesystem::exists(locale_path)) {
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kDebug,
            "MoviePlayer: loading from locale dir: " + locale_path.string());
        return decoder_.OpenPath(locale_path.string());
      }
    }
  }

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
      "MoviePlayer: movie file not found: " + avi_file);
  return false;
}

void MoviePlayer::AdvanceVideoToTime(double target_seconds) {
  if (!decoder_.IsOpen()) return;

  const double frame_interval =
      (info_.frame_rate > 0.0) ? 1.0 / info_.frame_rate : 1.0 / 15.0;

  while (next_frame_time_ <= target_seconds) {
    auto frame = decoder_.DecodeNextVideoFrame();
    if (!frame.has_value()) {

      return;
    }

    current_rgba_ = std::move(frame->rgba);
    ++frame_version_;
    next_frame_time_ += frame_interval;
  }
}

void MoviePlayer::UpdateSubtitles(double current_seconds) {
  if (!subtitles_.has_value()) return;

  const auto current_ms =
      static_cast<std::uint32_t>(current_seconds * 1000.0);

  if (active_subtitle_idx_ >= 0) {
    const auto& entries = subtitles_->entries();
    if (active_subtitle_idx_ < static_cast<int>(entries.size()) &&
        entries[active_subtitle_idx_].end_ms <= current_ms) {
      pending_events_.push_back({MovieEvent::kHideSubtitle, {}});
      active_subtitle_idx_ = -1;
    }
  }

  if (active_subtitle_idx_ < 0) {
    int idx = subtitles_->FindActiveAt(current_ms);
    if (idx >= 0) {
      active_subtitle_idx_ = idx;
      const auto& text = subtitles_->entries()[idx].text;
      pending_events_.push_back({MovieEvent::kShowSubtitle, text});
    }
  }
}

const std::uint8_t* MoviePlayer::CurrentFrameRGBA() const noexcept {
  return current_rgba_.empty() ? nullptr : current_rgba_.data();
}

std::shared_ptr<openwow::audio::IMovieAudioSource>
MoviePlayer::AudioSource() const noexcept {
  return audio_source_;
}

std::optional<openwow::audio::IMovieAudioSource::Stats>
MoviePlayer::AudioStats() const noexcept {
  return audio_source_ != nullptr
      ? std::optional<openwow::audio::IMovieAudioSource::Stats>(audio_source_->GetStats())
      : std::nullopt;
}

int MoviePlayer::AudioSampleRate() const noexcept {
  return audio_source_ != nullptr ? audio_source_->SampleRate() : 44100;
}

int MoviePlayer::AudioChannels() const noexcept {
  return audio_source_ != nullptr ? audio_source_->Channels() : 2;
}

float MoviePlayer::VolumeNormalized() const noexcept {
  return static_cast<float>(volume_) / 255.0f;
}

std::vector<MovieEvent> MoviePlayer::ConsumeEvents() {
  std::vector<MovieEvent> out;
  std::swap(out, pending_events_);
  return out;
}

double MoviePlayer::FrameRate() const noexcept { return info_.frame_rate; }
double MoviePlayer::DurationSeconds() const noexcept { return info_.duration_seconds; }
int MoviePlayer::VideoWidth() const noexcept { return info_.video_width; }
int MoviePlayer::VideoHeight() const noexcept { return info_.video_height; }

}

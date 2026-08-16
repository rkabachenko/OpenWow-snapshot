#include "openwow/ui/game/runtime/movie_recording_runtime.h"

#include <utility>

namespace openwow::ui::game::runtime {

MovieRecordingRuntime::~MovieRecordingRuntime() {
  Reset();
}

void MovieRecordingRuntime::BindBackend(
    std::unique_ptr<MovieRecordingBackend> backend) noexcept {
  if (backend_) {
    backend_->Cancel();
  }
  capture_sound_ = false;
  backend_ = std::move(backend);
}

void MovieRecordingRuntime::Reset() noexcept {
  if (backend_) {
    backend_->Cancel();
  }
  capture_sound_ = false;
  backend_.reset();
}

bool MovieRecordingRuntime::IsSupported(const bool force_enable) const noexcept {
  return backend_ != nullptr && backend_->IsSupported(force_enable);
}

bool MovieRecordingRuntime::IsCodecSupported(
    const std::uint32_t codec) const noexcept {
  return backend_ != nullptr && backend_->IsCodecSupported(codec);
}

bool MovieRecordingRuntime::IsCursorRecordingSupported() const noexcept {
  return backend_ != nullptr && backend_->IsCursorRecordingSupported();
}

MovieRecordingToggleResult MovieRecordingRuntime::Toggle(
    const MovieRecordingOptions& options) {
  if (!IsSupported(options.force_enable)) {
    return MovieRecordingToggleResult::kUnsupported;
  }

  const auto result = backend_->Toggle(options);
  if (result == MovieRecordingToggleResult::kStartedOrStopped) {
    capture_sound_ = backend_->IsRecording() && options.capture_sound;
  }
  return result;
}

void MovieRecordingRuntime::Cancel() noexcept {
  if (backend_) {
    backend_->Cancel();
  }
  capture_sound_ = false;
}

bool MovieRecordingRuntime::IsRecording() const noexcept {
  return backend_ != nullptr && backend_->IsRecording();
}

bool MovieRecordingRuntime::IsCapturingSound() const noexcept {
  return capture_sound_ && IsRecording();
}

bool MovieRecordingRuntime::IsCompressing() const noexcept {
  return backend_ != nullptr && backend_->IsCompressing();
}

MovieCompressionProgress MovieRecordingRuntime::CompressionProgress()
    const noexcept {
  return backend_ != nullptr ? backend_->CompressionProgress()
                             : MovieCompressionProgress{};
}

std::uint64_t MovieRecordingRuntime::RecordingTimeMicroseconds()
    const noexcept {
  return backend_ != nullptr ? backend_->RecordingTimeMicroseconds() : 0u;
}

std::string MovieRecordingRuntime::MovieFullPath() const {
  return backend_ != nullptr ? backend_->MovieFullPath() : std::string{};
}

void MovieRecordingRuntime::SearchUncompressedMovies(
    const bool warn_if_none) {
  if (backend_) {
    backend_->SearchUncompressedMovies(warn_if_none);
  }
}

void MovieRecordingRuntime::QueueMovieToCompress(
    std::optional<std::string> path) {
  if (backend_) {
    backend_->QueueMovieToCompress(std::move(path));
  }
}

void MovieRecordingRuntime::DeleteMovie(
    std::optional<std::string> path) {
  if (backend_) {
    backend_->DeleteMovie(std::move(path));
  }
}

}

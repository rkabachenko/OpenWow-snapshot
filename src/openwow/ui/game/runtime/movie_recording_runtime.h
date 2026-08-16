#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace openwow::ui::game::runtime {

struct MovieRecordingOptions final {
  std::uint32_t width{0};
  std::uint32_t height{0};
  float frame_rate{0.0F};
  bool capture_sound{false};
  std::uint32_t codec{0};
  std::uint32_t quality{0};
  bool capture_gui{true};
  bool capture_cursor{false};
  bool force_enable{false};
};

enum class MovieRecordingToggleResult : std::uint8_t {
  kStartedOrStopped = 0,
  kDiskFull = 1,
  kCompressionActive = 2,
  kUnsupported = 3,
};

struct MovieCompressionProgress final {

  bool complete{true};
  float fraction{1.0F};
};

class MovieRecordingBackend {
 public:
  virtual ~MovieRecordingBackend() = default;

  [[nodiscard]] virtual bool IsSupported(bool force_enable) const noexcept = 0;
  [[nodiscard]] virtual bool IsCodecSupported(
      std::uint32_t codec) const noexcept = 0;
  [[nodiscard]] virtual bool IsCursorRecordingSupported() const noexcept = 0;
  [[nodiscard]] virtual MovieRecordingToggleResult Toggle(
      const MovieRecordingOptions& options) = 0;
  virtual void Cancel() noexcept = 0;
  [[nodiscard]] virtual bool IsRecording() const noexcept = 0;
  [[nodiscard]] virtual bool IsCompressing() const noexcept = 0;
  [[nodiscard]] virtual MovieCompressionProgress CompressionProgress()
      const noexcept = 0;
  [[nodiscard]] virtual std::uint64_t RecordingTimeMicroseconds()
      const noexcept = 0;
  [[nodiscard]] virtual std::string MovieFullPath() const = 0;
  virtual void SearchUncompressedMovies(bool warn_if_none) = 0;
  virtual void QueueMovieToCompress(std::optional<std::string> path) = 0;
  virtual void DeleteMovie(std::optional<std::string> path) = 0;
};

class MovieRecordingRuntime final {
 public:
  ~MovieRecordingRuntime();

  void BindBackend(std::unique_ptr<MovieRecordingBackend> backend) noexcept;
  void Reset() noexcept;

  [[nodiscard]] bool IsSupported(bool force_enable = false) const noexcept;
  [[nodiscard]] bool IsCodecSupported(std::uint32_t codec) const noexcept;
  [[nodiscard]] bool IsCursorRecordingSupported() const noexcept;
  [[nodiscard]] MovieRecordingToggleResult Toggle(
      const MovieRecordingOptions& options);
  void Cancel() noexcept;
  [[nodiscard]] bool IsRecording() const noexcept;

  [[nodiscard]] bool IsCapturingSound() const noexcept;
  [[nodiscard]] bool IsCompressing() const noexcept;
  [[nodiscard]] MovieCompressionProgress CompressionProgress() const noexcept;
  [[nodiscard]] std::uint64_t RecordingTimeMicroseconds() const noexcept;
  [[nodiscard]] std::string MovieFullPath() const;
  void SearchUncompressedMovies(bool warn_if_none);
  void QueueMovieToCompress(std::optional<std::string> path);
  void DeleteMovie(std::optional<std::string> path);

 private:
  std::unique_ptr<MovieRecordingBackend> backend_;
  bool capture_sound_{false};
};

}

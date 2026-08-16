#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace openwow::audio {
class IMovieAudioSource;
}

namespace openwow::media {

struct VideoFrame {
  std::vector<std::uint8_t> rgba;
  double pts_seconds{0.0};
};

struct AudioBuffer {
  std::vector<std::int16_t> samples;
  int sample_rate{44100};
  int channels{2};
  double duration_seconds{0.0};
};

struct MovieInfo {
  int video_width{0};
  int video_height{0};
  double frame_rate{0.0};
  double duration_seconds{0.0};
  int total_video_frames{0};
  bool has_audio{false};
};

class MovieDecoder {
 public:
  MovieDecoder();
  ~MovieDecoder();

  MovieDecoder(const MovieDecoder&) = delete;
  MovieDecoder& operator=(const MovieDecoder&) = delete;
  MovieDecoder(MovieDecoder&&) noexcept;
  MovieDecoder& operator=(MovieDecoder&&) noexcept;

  bool Open(const std::vector<std::uint8_t>& data);

  bool OpenOwned(std::vector<std::uint8_t> data);

  bool OpenPath(const std::string& path);

  void Close();

  [[nodiscard]] bool IsOpen() const noexcept;

  [[nodiscard]] MovieInfo GetInfo() const noexcept;

  std::optional<VideoFrame> DecodeNextVideoFrame();

  bool Seek(double seconds);

  std::shared_ptr<openwow::audio::IMovieAudioSource> CreateAudioSource(
      int sample_rate, int channels) const;

  std::optional<AudioBuffer> DecodeAllAudio();

  [[nodiscard]] double CurrentPositionSeconds() const noexcept;

  [[nodiscard]] bool IsEndOfStream() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}

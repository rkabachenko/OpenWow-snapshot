
#define MINIMP3_IMPLEMENTATION
#include "minimp3/minimp3_ex.h"

#include "openwow/audio/codecs/mp3/mp3_decoder.h"

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace openwow::audio {

struct Mp3Decoder::Impl {
  ~Impl() {
    if (initialized) {
      mp3dec_ex_close(&decoder);
    }
  }

  mp3dec_ex_t decoder{};
  std::vector<std::uint8_t> file_data;
  std::vector<mp3d_sample_t> sample_buffer;
  std::uint32_t sample_rate{0};
  std::uint32_t channels{0};
  std::uint64_t total_frames{0};
  bool initialized{false};
  bool open{false};
};

Mp3Decoder::Mp3Decoder() : impl_(std::make_unique<Impl>()) {}
Mp3Decoder::~Mp3Decoder() = default;
Mp3Decoder::Mp3Decoder(Mp3Decoder&&) noexcept = default;
Mp3Decoder& Mp3Decoder::operator=(Mp3Decoder&&) noexcept = default;

bool Mp3Decoder::Open(const std::vector<std::uint8_t>& data) {
  return OpenOwned(data);
}

bool Mp3Decoder::OpenOwned(std::vector<std::uint8_t> data) {
  impl_ = std::make_unique<Impl>();
  if (data.size() < 4) {
    return false;
  }

  impl_->file_data = std::move(data);
  constexpr int kOpenFlags = MP3D_SEEK_TO_SAMPLE | MP3D_DO_NOT_SCAN;
  if (mp3dec_ex_open_buf(&impl_->decoder, impl_->file_data.data(),
                         impl_->file_data.size(), kOpenFlags) != 0) {
    return false;
  }
  impl_->initialized = true;

  const auto& info = impl_->decoder.info;
  if (info.hz <= 0 || info.channels <= 0) {
    return false;
  }
  impl_->sample_rate = static_cast<std::uint32_t>(info.hz);
  impl_->channels = static_cast<std::uint32_t>(info.channels);

  if (impl_->decoder.samples > 0) {
    impl_->total_frames = impl_->decoder.samples / impl_->channels;
  } else if (info.bitrate_kbps > 0) {

    const long double bits = static_cast<long double>(impl_->file_data.size()) * 8.0L;
    const long double seconds = bits / (static_cast<long double>(info.bitrate_kbps) * 1000.0L);
    const long double frames = seconds * impl_->sample_rate;
    impl_->total_frames = static_cast<std::uint64_t>(std::clamp(
        frames, 1.0L, static_cast<long double>(std::numeric_limits<std::uint64_t>::max())));
  }

  impl_->open = true;
  return true;
}

std::size_t Mp3Decoder::Decode(float* output, const std::size_t frames) {
  if (!impl_->open || output == nullptr || frames == 0) {
    return 0;
  }
  if (frames > std::numeric_limits<std::size_t>::max() / impl_->channels) {
    return 0;
  }

  const std::size_t requested_samples = frames * impl_->channels;
  impl_->sample_buffer.resize(requested_samples);
  const std::size_t decoded_samples =
      mp3dec_ex_read(&impl_->decoder, impl_->sample_buffer.data(), requested_samples);
  for (std::size_t i = 0; i < decoded_samples; ++i) {
    output[i] = static_cast<float>(impl_->sample_buffer[i]) / 32768.0F;
  }
  return decoded_samples / impl_->channels;
}

std::uint32_t Mp3Decoder::GetSampleRate() const {
  return impl_->sample_rate;
}

std::uint32_t Mp3Decoder::GetChannels() const {
  return impl_->channels;
}

std::uint64_t Mp3Decoder::GetTotalFrames() const {
  return impl_->total_frames;
}

bool Mp3Decoder::Seek(const std::uint64_t frame) {
  if (!impl_->open || frame > std::numeric_limits<std::uint64_t>::max() / impl_->channels) {
    return false;
  }
  return mp3dec_ex_seek(&impl_->decoder, frame * impl_->channels) == 0;
}

bool Mp3Decoder::IsOpen() const {
  return impl_->open;
}

}

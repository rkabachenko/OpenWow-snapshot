#include "openwow/audio/codecs/flac/flac_decoder.h"
#include "openwow/audio/codecs/wma/wma_decoder.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/frame.h>
#include <libswresample/swresample.h>
}

namespace openwow::audio {
namespace {

enum class CodecFamily {
  Flac,
  Wma,
};

bool IsSupportedCodec(const CodecFamily family, const AVCodecID codec) {
  if (family == CodecFamily::Flac) {
    return codec == AV_CODEC_ID_FLAC;
  }
  return codec == AV_CODEC_ID_WMAV1 || codec == AV_CODEC_ID_WMAV2 ||
         codec == AV_CODEC_ID_WMAPRO || codec == AV_CODEC_ID_WMALOSSLESS;
}

struct MemoryInput {
  std::vector<std::uint8_t> bytes;
  std::size_t position = 0;
};

int ReadMemory(void* opaque, std::uint8_t* output, const int output_size) {
  auto& input = *static_cast<MemoryInput*>(opaque);
  if (input.position >= input.bytes.size()) {
    return AVERROR_EOF;
  }
  const std::size_t count =
      std::min(static_cast<std::size_t>(output_size),
               input.bytes.size() - input.position);
  std::memcpy(output, input.bytes.data() + input.position, count);
  input.position += count;
  return static_cast<int>(count);
}

std::int64_t SeekMemory(void* opaque, const std::int64_t offset,
                        const int whence) {
  auto& input = *static_cast<MemoryInput*>(opaque);
  if (whence == AVSEEK_SIZE) {
    return static_cast<std::int64_t>(input.bytes.size());
  }

  std::int64_t base = 0;
  switch (whence & ~AVSEEK_FORCE) {
    case SEEK_SET:
      break;
    case SEEK_CUR:
      base = static_cast<std::int64_t>(input.position);
      break;
    case SEEK_END:
      base = static_cast<std::int64_t>(input.bytes.size());
      break;
    default:
      return AVERROR(EINVAL);
  }
  if ((offset < 0 && base < -offset) ||
      (offset > 0 &&
       base > std::numeric_limits<std::int64_t>::max() - offset)) {
    return AVERROR(EINVAL);
  }
  const std::int64_t next = base + offset;
  if (next < 0 ||
      static_cast<std::uint64_t>(next) > input.bytes.size()) {
    return AVERROR(EINVAL);
  }
  input.position = static_cast<std::size_t>(next);
  return next;
}

class DecoderState {
 public:
  explicit DecoderState(const CodecFamily family) : family_(family) {}
  ~DecoderState() { Close(); }

  bool Open(const std::vector<std::uint8_t>& data) {
    Close();
    if (data.size() < 16) {
      return false;
    }
    input_.bytes = data;

    constexpr int kIoBufferSize = 32768;
    io_buffer_ =
        static_cast<std::uint8_t*>(av_malloc(kIoBufferSize));
    if (io_buffer_ == nullptr) {
      return false;
    }
    io_ = avio_alloc_context(io_buffer_, kIoBufferSize, 0, &input_,
                             ReadMemory, nullptr, SeekMemory);
    if (io_ == nullptr) {
      av_free(io_buffer_);
      io_buffer_ = nullptr;
      return false;
    }

    format_ = avformat_alloc_context();
    if (format_ == nullptr) {
      Close();
      return false;
    }
    format_->pb = io_;
    if (avformat_open_input(&format_, nullptr, nullptr, nullptr) < 0 ||
        avformat_find_stream_info(format_, nullptr) < 0) {
      Close();
      return false;
    }

    stream_index_ =
        av_find_best_stream(format_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (stream_index_ < 0) {
      Close();
      return false;
    }
    const AVStream& stream = *format_->streams[stream_index_];
    if (!IsSupportedCodec(family_, stream.codecpar->codec_id)) {
      Close();
      return false;
    }
    const AVCodec* codec = avcodec_find_decoder(stream.codecpar->codec_id);
    decoder_ = codec == nullptr ? nullptr : avcodec_alloc_context3(codec);
    if (decoder_ == nullptr ||
        avcodec_parameters_to_context(decoder_, stream.codecpar) < 0 ||
        avcodec_open2(decoder_, codec, nullptr) < 0) {
      Close();
      return false;
    }

    sample_rate_ = static_cast<std::uint32_t>(decoder_->sample_rate);
    channels_ =
        static_cast<std::uint32_t>(decoder_->ch_layout.nb_channels);
    if (sample_rate_ == 0 || channels_ == 0) {
      Close();
      return false;
    }

    AVChannelLayout output_layout{};
    AVChannelLayout input_layout{};
    av_channel_layout_default(&output_layout, static_cast<int>(channels_));
    av_channel_layout_copy(&input_layout, &decoder_->ch_layout);
    const int resample_result = swr_alloc_set_opts2(
        &resampler_, &output_layout, AV_SAMPLE_FMT_FLT,
        static_cast<int>(sample_rate_), &input_layout, decoder_->sample_fmt,
        static_cast<int>(sample_rate_), 0, nullptr);
    av_channel_layout_uninit(&input_layout);
    av_channel_layout_uninit(&output_layout);
    if (resample_result < 0 || resampler_ == nullptr ||
        swr_init(resampler_) < 0) {
      Close();
      return false;
    }

    if (stream.duration != AV_NOPTS_VALUE && stream.duration > 0) {
      total_frames_ = static_cast<std::uint64_t>(
          stream.duration * av_q2d(stream.time_base) * sample_rate_ + 0.5);
    } else if (format_->duration > 0) {
      total_frames_ = static_cast<std::uint64_t>(
          format_->duration * sample_rate_ / AV_TIME_BASE + 0.5);
    }

    frame_ = av_frame_alloc();
    packet_ = av_packet_alloc();
    open_ = frame_ != nullptr && packet_ != nullptr;
    if (!open_) {
      Close();
    }
    return open_;
  }

  std::size_t Decode(float* output, const std::size_t frames) {
    if (!open_ || end_of_stream_ || output == nullptr || frames == 0) {
      return 0;
    }
    std::size_t written = 0;
    while (written < frames) {
      if (buffer_position_ < buffer_.size()) {
        const std::size_t available =
            (buffer_.size() - buffer_position_) / channels_;
        const std::size_t count = std::min(available, frames - written);
        std::memcpy(output + written * channels_,
                    buffer_.data() + buffer_position_,
                    count * channels_ * sizeof(float));
        buffer_position_ += count * channels_;
        written += count;
        continue;
      }
      if (!DecodeFrame()) {
        end_of_stream_ = true;
        break;
      }
    }
    return written;
  }

  bool Seek(const std::uint64_t target) {
    if (!open_ || (total_frames_ != 0 && target > total_frames_)) {
      return false;
    }
    if (av_seek_frame(format_, stream_index_, 0, AVSEEK_FLAG_BACKWARD) < 0) {
      return false;
    }
    avcodec_flush_buffers(decoder_);
    swr_close(resampler_);
    if (swr_init(resampler_) < 0) {
      return false;
    }
    buffer_.clear();
    buffer_position_ = 0;
    end_of_stream_ = false;
    flushed_ = false;

    std::vector<float> scratch(
        static_cast<std::size_t>(
            std::min<std::uint64_t>(4096, target)) *
        channels_);
    std::uint64_t skipped = 0;
    while (skipped < target) {
      const std::size_t count = static_cast<std::size_t>(
          std::min<std::uint64_t>(4096, target - skipped));
      const std::size_t decoded = Decode(scratch.data(), count);
      if (decoded == 0) {
        return false;
      }
      skipped += decoded;
    }
    return true;
  }

  [[nodiscard]] std::uint32_t sample_rate() const noexcept {
    return sample_rate_;
  }
  [[nodiscard]] std::uint32_t channels() const noexcept { return channels_; }
  [[nodiscard]] std::uint64_t total_frames() const noexcept {
    return total_frames_;
  }
  [[nodiscard]] bool is_open() const noexcept { return open_; }

 private:
  bool DecodeFrame() {
    buffer_.clear();
    buffer_position_ = 0;
    if (flushed_) {
      if (avcodec_receive_frame(decoder_, frame_) == 0) {
        return Convert(frame_->extended_data, frame_->nb_samples);
      }
      return Convert(nullptr, 0);
    }
    if (avcodec_receive_frame(decoder_, frame_) == 0) {
      return Convert(frame_->extended_data, frame_->nb_samples);
    }
    while (av_read_frame(format_, packet_) >= 0) {
      if (packet_->stream_index != stream_index_) {
        av_packet_unref(packet_);
        continue;
      }
      const int send_result = avcodec_send_packet(decoder_, packet_);
      av_packet_unref(packet_);
      if (send_result < 0) {
        continue;
      }
      if (avcodec_receive_frame(decoder_, frame_) == 0) {
        return Convert(frame_->extended_data, frame_->nb_samples);
      }
    }
    avcodec_send_packet(decoder_, nullptr);
    flushed_ = true;
    if (avcodec_receive_frame(decoder_, frame_) == 0) {
      return Convert(frame_->extended_data, frame_->nb_samples);
    }
    return Convert(nullptr, 0);
  }

  bool Convert(std::uint8_t** input, const int input_samples) {
    const int output_samples =
        swr_get_out_samples(resampler_, input_samples);
    if (output_samples <= 0) {
      return false;
    }
    buffer_.resize(static_cast<std::size_t>(output_samples) * channels_);
    std::uint8_t* output[] = {
        reinterpret_cast<std::uint8_t*>(buffer_.data())};
    const int converted = swr_convert(
        resampler_, output, output_samples,
        const_cast<const std::uint8_t**>(input), input_samples);
    if (converted <= 0) {
      buffer_.clear();
      return false;
    }
    buffer_.resize(static_cast<std::size_t>(converted) * channels_);
    return true;
  }

  void Close() {
    if (frame_ != nullptr) av_frame_free(&frame_);
    if (packet_ != nullptr) av_packet_free(&packet_);
    if (resampler_ != nullptr) swr_free(&resampler_);
    if (decoder_ != nullptr) avcodec_free_context(&decoder_);
    if (format_ != nullptr) {
      if (io_ != nullptr) format_->pb = nullptr;
      avformat_close_input(&format_);
    }
    if (io_ != nullptr) {
      avio_context_free(&io_);
      io_buffer_ = nullptr;
    } else if (io_buffer_ != nullptr) {
      av_free(io_buffer_);
      io_buffer_ = nullptr;
    }
    input_ = {};
    stream_index_ = -1;
    sample_rate_ = 0;
    channels_ = 0;
    total_frames_ = 0;
    buffer_.clear();
    buffer_position_ = 0;
    open_ = false;
    end_of_stream_ = false;
    flushed_ = false;
  }

  CodecFamily family_;
  MemoryInput input_;
  AVIOContext* io_ = nullptr;
  std::uint8_t* io_buffer_ = nullptr;
  AVFormatContext* format_ = nullptr;
  int stream_index_ = -1;
  AVCodecContext* decoder_ = nullptr;
  SwrContext* resampler_ = nullptr;
  AVFrame* frame_ = nullptr;
  AVPacket* packet_ = nullptr;
  std::uint32_t sample_rate_ = 0;
  std::uint32_t channels_ = 0;
  std::uint64_t total_frames_ = 0;
  std::vector<float> buffer_;
  std::size_t buffer_position_ = 0;
  bool open_ = false;
  bool end_of_stream_ = false;
  bool flushed_ = false;
};

}

struct FlacDecoder::Impl {
  DecoderState decoder{CodecFamily::Flac};
};

FlacDecoder::FlacDecoder() : impl_(std::make_unique<Impl>()) {}
FlacDecoder::~FlacDecoder() = default;
FlacDecoder::FlacDecoder(FlacDecoder&&) noexcept = default;
FlacDecoder& FlacDecoder::operator=(FlacDecoder&&) noexcept = default;
bool FlacDecoder::Open(const std::vector<std::uint8_t>& data) {
  return impl_->decoder.Open(data);
}
std::size_t FlacDecoder::Decode(float* output, const std::size_t frames) {
  return impl_->decoder.Decode(output, frames);
}
std::uint32_t FlacDecoder::GetSampleRate() const {
  return impl_->decoder.sample_rate();
}
std::uint32_t FlacDecoder::GetChannels() const {
  return impl_->decoder.channels();
}
std::uint64_t FlacDecoder::GetTotalFrames() const {
  return impl_->decoder.total_frames();
}
bool FlacDecoder::Seek(const std::uint64_t frame) {
  return impl_->decoder.Seek(frame);
}
bool FlacDecoder::IsOpen() const { return impl_->decoder.is_open(); }

struct WmaDecoder::Impl {
  DecoderState decoder{CodecFamily::Wma};
};

WmaDecoder::WmaDecoder() : impl_(std::make_unique<Impl>()) {}
WmaDecoder::~WmaDecoder() = default;
WmaDecoder::WmaDecoder(WmaDecoder&&) noexcept = default;
WmaDecoder& WmaDecoder::operator=(WmaDecoder&&) noexcept = default;
bool WmaDecoder::Open(const std::vector<std::uint8_t>& data) {
  return impl_->decoder.Open(data);
}
std::size_t WmaDecoder::Decode(float* output, const std::size_t frames) {
  return impl_->decoder.Decode(output, frames);
}
std::uint32_t WmaDecoder::GetSampleRate() const {
  return impl_->decoder.sample_rate();
}
std::uint32_t WmaDecoder::GetChannels() const {
  return impl_->decoder.channels();
}
std::uint64_t WmaDecoder::GetTotalFrames() const {
  return impl_->decoder.total_frames();
}
bool WmaDecoder::Seek(const std::uint64_t frame) {
  return impl_->decoder.Seek(frame);
}
bool WmaDecoder::IsOpen() const { return impl_->decoder.is_open(); }

}

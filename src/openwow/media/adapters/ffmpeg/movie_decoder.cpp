
#include "openwow/media/adapters/ffmpeg/movie_decoder.h"
#include "openwow/audio/playback/movie_audio_source.h"
#include "openwow/runtime/scheduling/jthread_compat.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace openwow::media {

struct MemoryBuffer {
  const std::uint8_t* data{nullptr};
  std::size_t size{0};
  std::size_t pos{0};
};

static int AvioReadPacket(void* opaque, uint8_t* buf, int buf_size) {
  auto* mb = static_cast<MemoryBuffer*>(opaque);
  if (mb->pos >= mb->size) return AVERROR_EOF;
  const auto remaining = mb->size - mb->pos;
  const auto to_read = std::min(static_cast<std::size_t>(buf_size), remaining);
  std::memcpy(buf, mb->data + mb->pos, to_read);
  mb->pos += to_read;
  return static_cast<int>(to_read);
}

static int64_t AvioSeek(void* opaque, int64_t offset, int whence) {
  auto* mb = static_cast<MemoryBuffer*>(opaque);
  if (whence == AVSEEK_SIZE) {
    return static_cast<int64_t>(mb->size);
  }

  const int origin = whence & ~AVSEEK_FORCE;
  std::int64_t base = 0;
  if (origin == SEEK_CUR) {
    base = static_cast<std::int64_t>(mb->pos);
  } else if (origin == SEEK_END) {
    base = static_cast<std::int64_t>(mb->size);
  } else if (origin != SEEK_SET) {
    return AVERROR(EINVAL);
  }

  if ((offset > 0 && base > std::numeric_limits<std::int64_t>::max() - offset) ||
      (offset < 0 && base < std::numeric_limits<std::int64_t>::min() - offset)) {
    return AVERROR(EINVAL);
  }
  const std::int64_t target = base + offset;
  if (target < 0 || static_cast<std::uint64_t>(target) > mb->size) {
    return AVERROR(EINVAL);
  }
  mb->pos = static_cast<std::size_t>(target);
  return static_cast<int64_t>(mb->pos);
}

namespace {

constexpr int kMovieAudioRate = 44100;
constexpr int kMovieAudioChannels = 2;
constexpr int kMovieAudioBufferSeconds = 2;

struct MovieInputSource {
  std::shared_ptr<const std::vector<std::uint8_t>> bytes;
  std::string path;

  [[nodiscard]] bool IsValid() const noexcept {
    return (bytes != nullptr && !bytes->empty()) || !path.empty();
  }

  [[nodiscard]] std::string Label() const {
    return !path.empty() ? path : "VFS AVI";
  }
};

class FfmpegInput {
 public:
  FfmpegInput() = default;
  ~FfmpegInput() { Close(); }

  FfmpegInput(const FfmpegInput&) = delete;
  FfmpegInput& operator=(const FfmpegInput&) = delete;

  bool Open(const MovieInputSource& source) {
    Close();
    if (!source.IsValid()) {
      return false;
    }

    if (!source.path.empty()) {
      return avformat_open_input(&format_, source.path.c_str(), nullptr, nullptr) >= 0;
    }

    bytes_ = source.bytes;
    memory_ = {bytes_->data(), bytes_->size(), 0};
    avio_buffer_ = static_cast<std::uint8_t*>(av_malloc(kAvioBufferSize));
    if (avio_buffer_ == nullptr) {
      Close();
      return false;
    }
    avio_ = avio_alloc_context(avio_buffer_, kAvioBufferSize, 0, &memory_,
                               AvioReadPacket, nullptr, AvioSeek);
    if (avio_ == nullptr) {
      av_free(avio_buffer_);
      avio_buffer_ = nullptr;
      Close();
      return false;
    }

    format_ = avformat_alloc_context();
    if (format_ == nullptr) {
      Close();
      return false;
    }
    format_->pb = avio_;
    const AVInputFormat* avi = av_find_input_format("avi");
    if (avformat_open_input(&format_, nullptr, avi, nullptr) < 0) {
      Close();
      return false;
    }
    return true;
  }

  [[nodiscard]] AVFormatContext* Get() const noexcept { return format_; }

 private:
  void Close() noexcept {
    if (format_ != nullptr) {
      if (avio_ != nullptr) {
        format_->pb = nullptr;
      }
      avformat_close_input(&format_);
    }
    if (avio_ != nullptr) {
      avio_context_free(&avio_);
      avio_buffer_ = nullptr;
    } else if (avio_buffer_ != nullptr) {
      av_free(avio_buffer_);
      avio_buffer_ = nullptr;
    }
    bytes_.reset();
    memory_ = {};
  }

  static constexpr int kAvioBufferSize = 32768;
  std::shared_ptr<const std::vector<std::uint8_t>> bytes_;
  MemoryBuffer memory_;
  AVFormatContext* format_{nullptr};
  AVIOContext* avio_{nullptr};
  std::uint8_t* avio_buffer_{nullptr};
};

enum class AudioChunkResult {
  kData,
  kEnd,
  kError,
};

class FfmpegAudioDecoder {
 public:
  FfmpegAudioDecoder() = default;
  ~FfmpegAudioDecoder() { Close(); }

  FfmpegAudioDecoder(const FfmpegAudioDecoder&) = delete;
  FfmpegAudioDecoder& operator=(const FfmpegAudioDecoder&) = delete;

  bool Open(const MovieInputSource& source,
            const int output_rate = kMovieAudioRate,
            const int output_channels = kMovieAudioChannels) {
    Close();
    if (output_rate <= 0 || output_channels <= 0 || output_channels > 8 ||
        !input_.Open(source)) {
      return false;
    }
    output_rate_ = output_rate;
    output_channels_ = output_channels;
    AVFormatContext* format = input_.Get();
    if (avformat_find_stream_info(format, nullptr) < 0) {
      return false;
    }

    stream_index_ = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO,
                                        -1, -1, nullptr, 0);
    if (stream_index_ < 0) {
      return false;
    }
    const AVStream* stream = format->streams[stream_index_];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (codec == nullptr) {
      return false;
    }
    codec_ = avcodec_alloc_context3(codec);
    if (codec_ == nullptr ||
        avcodec_parameters_to_context(codec_, stream->codecpar) < 0 ||
        avcodec_open2(codec_, codec, nullptr) < 0) {
      return false;
    }

    AVChannelLayout input_layout{};
    if (codec_->ch_layout.nb_channels > 0) {
      if (av_channel_layout_copy(&input_layout, &codec_->ch_layout) < 0) {
        return false;
      }
    } else {
      av_channel_layout_default(&input_layout, 2);
    }
    AVChannelLayout output_layout{};
    av_channel_layout_default(&output_layout, output_channels_);
    const int swr_result = swr_alloc_set_opts2(
        &resampler_, &output_layout, AV_SAMPLE_FMT_S16, output_rate_,
        &input_layout, codec_->sample_fmt, codec_->sample_rate, 0, nullptr);
    av_channel_layout_uninit(&input_layout);
    av_channel_layout_uninit(&output_layout);
    if (swr_result < 0 || resampler_ == nullptr || swr_init(resampler_) < 0) {
      return false;
    }

    frame_ = av_frame_alloc();
    packet_ = av_packet_alloc();
    return frame_ != nullptr && packet_ != nullptr;
  }

  AudioChunkResult ReadNext(std::vector<std::int16_t>& output) {
    output.clear();
    if (codec_ == nullptr || resampler_ == nullptr || frame_ == nullptr ||
        packet_ == nullptr) {
      return AudioChunkResult::kError;
    }

    while (true) {
      const int receive = avcodec_receive_frame(codec_, frame_);
      if (receive == 0) {
        const AudioChunkResult converted = ConvertFrame(output);
        av_frame_unref(frame_);
        if (converted != AudioChunkResult::kData || !output.empty()) {
          return converted;
        }
        continue;
      }
      if (receive != AVERROR(EAGAIN) && receive != AVERROR_EOF) {
        return AudioChunkResult::kError;
      }
      if (decoder_draining_) {
        return DrainResampler(output);
      }

      bool submitted = false;
      while (av_read_frame(input_.Get(), packet_) >= 0) {
        if (packet_->stream_index != stream_index_) {
          av_packet_unref(packet_);
          continue;
        }
        const int send = avcodec_send_packet(codec_, packet_);
        av_packet_unref(packet_);
        if (send < 0) {
          return AudioChunkResult::kError;
        }
        submitted = true;
        break;
      }
      if (submitted) {
        continue;
      }

      const int flush = avcodec_send_packet(codec_, nullptr);
      if (flush < 0 && flush != AVERROR_EOF) {
        return AudioChunkResult::kError;
      }
      decoder_draining_ = true;
    }
  }

 private:
  AudioChunkResult ConvertFrame(std::vector<std::int16_t>& output) {
    const int capacity_frames = swr_get_out_samples(resampler_, frame_->nb_samples);
    if (capacity_frames < 0) {
      return AudioChunkResult::kError;
    }
    if (capacity_frames == 0) {
      return AudioChunkResult::kData;
    }
    const auto capacity_samples = static_cast<std::size_t>(capacity_frames) *
                                  output_channels_;
    output.resize(capacity_samples);
    std::uint8_t* destination = reinterpret_cast<std::uint8_t*>(output.data());
    const int converted = swr_convert(
        resampler_, &destination, capacity_frames,
        const_cast<const std::uint8_t**>(frame_->extended_data), frame_->nb_samples);
    if (converted < 0) {
      output.clear();
      return AudioChunkResult::kError;
    }
    output.resize(static_cast<std::size_t>(converted) * output_channels_);
    return AudioChunkResult::kData;
  }

  AudioChunkResult DrainResampler(std::vector<std::int16_t>& output) {
    const int capacity_frames = swr_get_out_samples(resampler_, 0);
    if (capacity_frames < 0) {
      return AudioChunkResult::kError;
    }
    if (capacity_frames == 0) {
      return AudioChunkResult::kEnd;
    }
    output.resize(static_cast<std::size_t>(capacity_frames) * output_channels_);
    std::uint8_t* destination = reinterpret_cast<std::uint8_t*>(output.data());
    const int converted = swr_convert(resampler_, &destination, capacity_frames,
                                      nullptr, 0);
    if (converted < 0) {
      output.clear();
      return AudioChunkResult::kError;
    }
    output.resize(static_cast<std::size_t>(converted) * output_channels_);
    return output.empty() ? AudioChunkResult::kEnd : AudioChunkResult::kData;
  }

  void Close() noexcept {
    if (packet_ != nullptr) av_packet_free(&packet_);
    if (frame_ != nullptr) av_frame_free(&frame_);
    if (resampler_ != nullptr) swr_free(&resampler_);
    if (codec_ != nullptr) avcodec_free_context(&codec_);
    stream_index_ = -1;
    decoder_draining_ = false;
  }

  FfmpegInput input_;
  int stream_index_{-1};
  AVCodecContext* codec_{nullptr};
  SwrContext* resampler_{nullptr};
  AVFrame* frame_{nullptr};
  AVPacket* packet_{nullptr};
  bool decoder_draining_{false};
  int output_rate_{kMovieAudioRate};
  int output_channels_{kMovieAudioChannels};
};

class FfmpegMovieAudioSource final : public openwow::audio::IMovieAudioSource {
 public:
  static std::shared_ptr<FfmpegMovieAudioSource> Create(
      MovieInputSource input, const int sample_rate, const int channels) {
    auto source = std::shared_ptr<FfmpegMovieAudioSource>(
        new FfmpegMovieAudioSource(std::move(input), sample_rate, channels));
    source->worker_ = openwow::core::JthreadCompat(
        [state = source.get()](openwow::core::stop_token stop_token) {
          state->WorkerLoop(std::move(stop_token));
        });
    return source;
  }

  ~FfmpegMovieAudioSource() override {
    if (worker_.joinable()) {
      worker_.request_stop();
      producer_cv_.notify_all();
      worker_.join();
    }
  }

  std::size_t MixInto(std::int32_t* mix, const std::size_t sample_count,
                      const float volume) noexcept override {
    if (mix == nullptr || sample_count == 0) {
      return 0;
    }
    const std::uint64_t read = read_sample_.load(std::memory_order_relaxed);
    const std::uint64_t write = write_sample_.load(std::memory_order_acquire);
    const std::size_t available = write > read
        ? static_cast<std::size_t>(write - read)
        : 0U;
    const std::size_t count = std::min(sample_count, available);
    const std::size_t offset = static_cast<std::size_t>(read % ring_.size());
    const std::size_t first = std::min(count, ring_.size() - offset);

    if (volume != 0.0F) {
      for (std::size_t index = 0; index < first; ++index) {
        mix[index] += static_cast<std::int32_t>(
            static_cast<float>(ring_[offset + index]) * volume);
      }
      for (std::size_t index = first; index < count; ++index) {
        mix[index] += static_cast<std::int32_t>(
            static_cast<float>(ring_[index - first]) * volume);
      }
    }

    if (count > 0) {
      read_sample_.store(read + count, std::memory_order_release);
      producer_cv_.notify_one();
    }
    if (count < sample_count && !producer_finished_.load(std::memory_order_acquire)) {
      underflows_.fetch_add(1, std::memory_order_relaxed);
    }
    return count;
  }

  [[nodiscard]] bool IsDrained() const noexcept override {
    return producer_finished_.load(std::memory_order_acquire) && BufferedSamples() == 0;
  }

  [[nodiscard]] int SampleRate() const noexcept override { return sample_rate_; }
  [[nodiscard]] int Channels() const noexcept override { return channels_; }

  [[nodiscard]] Stats GetStats() const noexcept override {
    return {
        .buffered_samples = BufferedSamples(),
        .capacity_samples = ring_.size(),
        .consumed_samples = read_sample_.load(std::memory_order_acquire),
        .underflows = underflows_.load(std::memory_order_relaxed),
        .producer_finished = producer_finished_.load(std::memory_order_acquire),
    };
  }

 private:
  explicit FfmpegMovieAudioSource(MovieInputSource input,
                                  const int sample_rate,
                                  const int channels)
      : input_(std::move(input)),
        sample_rate_(sample_rate),
        channels_(channels),
        ring_(static_cast<std::size_t>(sample_rate_) * channels_ *
              kMovieAudioBufferSeconds) {}

  [[nodiscard]] std::size_t BufferedSamples() const noexcept {
    const std::uint64_t read = read_sample_.load(std::memory_order_acquire);
    const std::uint64_t write = write_sample_.load(std::memory_order_acquire);
    return write > read ? static_cast<std::size_t>(write - read) : 0U;
  }

  [[nodiscard]] std::size_t FreeSamples() const noexcept {
    return ring_.size() - BufferedSamples();
  }

  bool Push(const std::int16_t* samples, const std::size_t sample_count,
            const openwow::core::stop_token& stop_token) {
    std::size_t source_offset = 0;
    while (source_offset < sample_count && !stop_token.stop_requested()) {
      const std::size_t free = FreeSamples();
      if (free == 0) {
        std::unique_lock lock(producer_mutex_);
        producer_cv_.wait_for(lock, std::chrono::milliseconds(50), [this, &stop_token] {
          return stop_token.stop_requested() || FreeSamples() > 0;
        });
        continue;
      }

      const std::size_t count = std::min(free, sample_count - source_offset);
      const std::uint64_t write = write_sample_.load(std::memory_order_relaxed);
      const std::size_t offset = static_cast<std::size_t>(write % ring_.size());
      const std::size_t first = std::min(count, ring_.size() - offset);
      std::memcpy(ring_.data() + offset, samples + source_offset,
                  first * sizeof(std::int16_t));
      if (count > first) {
        std::memcpy(ring_.data(), samples + source_offset + first,
                    (count - first) * sizeof(std::int16_t));
      }
      write_sample_.store(write + count, std::memory_order_release);
      source_offset += count;
    }
    return source_offset == sample_count;
  }

  void WorkerLoop(openwow::core::stop_token stop_token) noexcept {
    try {
      FfmpegAudioDecoder decoder;
      if (!decoder.Open(input_, sample_rate_, channels_)) {
        LogWarning("Movie audio decoder could not open ");
        producer_finished_.store(true, std::memory_order_release);
        return;
      }

      std::vector<std::int16_t> decoded;
      while (!stop_token.stop_requested()) {
        const AudioChunkResult result = decoder.ReadNext(decoded);
        if (result == AudioChunkResult::kData) {
          if (!decoded.empty() && !Push(decoded.data(), decoded.size(), stop_token)) {
            return;
          }
          continue;
        }
        if (result == AudioChunkResult::kError) {
          LogWarning("Movie audio decode failed for ");
        }
        producer_finished_.store(true, std::memory_order_release);
        return;
      }
    } catch (...) {
      LogWarning("Movie audio worker failed for ");
      producer_finished_.store(true, std::memory_order_release);
    }
  }

  void LogWarning(const char* message) const noexcept {
    try {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                         std::string(message) + input_.Label());
    } catch (...) {
    }
  }

  MovieInputSource input_;
  int sample_rate_{kMovieAudioRate};
  int channels_{kMovieAudioChannels};
  std::vector<std::int16_t> ring_;
  alignas(64) std::atomic<std::uint64_t> read_sample_{0};
  alignas(64) std::atomic<std::uint64_t> write_sample_{0};
  std::atomic<std::uint64_t> underflows_{0};
  std::atomic<bool> producer_finished_{false};
  std::mutex producer_mutex_;
  std::condition_variable producer_cv_;
  openwow::core::JthreadCompat worker_;
};

}

struct MovieDecoder::Impl {

  MovieInputSource input_source;
  MemoryBuffer mem_buf;
  AVIOContext* avio_ctx{nullptr};
  std::uint8_t* avio_buffer{nullptr};
  static constexpr int kAvioBufferSize = 32768;

  AVFormatContext* fmt_ctx{nullptr};

  int video_stream_idx{-1};
  AVCodecContext* video_dec_ctx{nullptr};
  SwsContext* sws_ctx{nullptr};

  int audio_stream_idx{-1};

  AVFrame* frame{nullptr};
  AVFrame* rgba_frame{nullptr};
  AVPacket* packet{nullptr};

  bool open{false};
  bool eos{false};
  double current_pts{0.0};
  int decoded_frame_count{0};

  MovieInfo info;

  ~Impl() { Close(); }

  bool OpenFromMemory(std::shared_ptr<const std::vector<std::uint8_t>> data) {
    if (data == nullptr || data->empty()) {
      return false;
    }
    input_source.bytes = std::move(data);
    mem_buf = {input_source.bytes->data(), input_source.bytes->size(), 0};

    avio_buffer =
        static_cast<std::uint8_t*>(av_malloc(kAvioBufferSize));
    if (!avio_buffer) {
      Close();
      return false;
    }

    avio_ctx = avio_alloc_context(avio_buffer, kAvioBufferSize, 0,
                                  &mem_buf, AvioReadPacket, nullptr,
                                  AvioSeek);
    if (!avio_ctx) {
      av_free(avio_buffer);
      avio_buffer = nullptr;
      Close();
      return false;
    }

    fmt_ctx = avformat_alloc_context();
    if (!fmt_ctx) {
      Close();
      return false;
    }
    fmt_ctx->pb = avio_ctx;

    const AVInputFormat* avi_fmt = av_find_input_format("avi");
    if (avformat_open_input(&fmt_ctx, nullptr, avi_fmt, nullptr) < 0) {
      Close();
      return false;
    }

    return InitStreams();
  }

  bool OpenFromPath(const std::string& path) {
    input_source.path = path;
    if (avformat_open_input(&fmt_ctx, path.c_str(), nullptr, nullptr) < 0) {
      Close();
      return false;
    }
    return InitStreams();
  }

  bool InitStreams() {
    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
      Close();
      return false;
    }

    video_stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO,
                                           -1, -1, nullptr, 0);
    if (video_stream_idx < 0) {
      Close();
      return false;
    }

    const auto* video_stream = fmt_ctx->streams[video_stream_idx];
    const auto* video_codec =
        avcodec_find_decoder(video_stream->codecpar->codec_id);
    if (!video_codec) {
      Close();
      return false;
    }
    video_dec_ctx = avcodec_alloc_context3(video_codec);
    if (video_dec_ctx == nullptr ||
        avcodec_parameters_to_context(video_dec_ctx, video_stream->codecpar) < 0 ||
        avcodec_open2(video_dec_ctx, video_codec, nullptr) < 0) {
      Close();
      return false;
    }

    sws_ctx = sws_getContext(
        video_dec_ctx->width, video_dec_ctx->height, video_dec_ctx->pix_fmt,
        video_dec_ctx->width, video_dec_ctx->height, AV_PIX_FMT_RGBA,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_ctx) {
      Close();
      return false;
    }

    audio_stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO,
                                           -1, -1, nullptr, 0);

    frame = av_frame_alloc();
    rgba_frame = av_frame_alloc();
    packet = av_packet_alloc();
    if (!frame || !rgba_frame || !packet) {
      Close();
      return false;
    }

    rgba_frame->format = AV_PIX_FMT_RGBA;
    rgba_frame->width = video_dec_ctx->width;
    rgba_frame->height = video_dec_ctx->height;
    if (av_image_alloc(rgba_frame->data, rgba_frame->linesize,
                       video_dec_ctx->width, video_dec_ctx->height,
                       AV_PIX_FMT_RGBA, 32) < 0) {
      Close();
      return false;
    }

    auto* vs = fmt_ctx->streams[video_stream_idx];
    info.video_width = video_dec_ctx->width;
    info.video_height = video_dec_ctx->height;

    if (vs->avg_frame_rate.den > 0) {
      info.frame_rate =
          static_cast<double>(vs->avg_frame_rate.num) / vs->avg_frame_rate.den;
    } else if (vs->r_frame_rate.den > 0) {
      info.frame_rate =
          static_cast<double>(vs->r_frame_rate.num) / vs->r_frame_rate.den;
    } else {
      info.frame_rate = 15.0;
    }

    if (fmt_ctx->duration > 0) {
      info.duration_seconds =
          static_cast<double>(fmt_ctx->duration) / AV_TIME_BASE;
    }

    if (info.frame_rate > 0.0 && info.duration_seconds > 0.0) {
      info.total_video_frames =
          static_cast<int>(info.duration_seconds * info.frame_rate + 0.5);
    } else if (vs->nb_frames > 0) {
      info.total_video_frames = static_cast<int>(vs->nb_frames);
    }

    info.has_audio = audio_stream_idx >= 0;
    open = true;
    eos = false;
    current_pts = 0.0;
    decoded_frame_count = 0;
    return true;
  }

  std::optional<VideoFrame> DecodeNextVideoFrame() {
    if (!open || eos) return std::nullopt;

    while (true) {
      int ret = av_read_frame(fmt_ctx, packet);
      if (ret < 0) {

        avcodec_send_packet(video_dec_ctx, nullptr);
        ret = avcodec_receive_frame(video_dec_ctx, frame);
        if (ret == 0) {
          return ConvertFrame();
        }
        eos = true;
        return std::nullopt;
      }

      if (packet->stream_index == video_stream_idx) {
        ret = avcodec_send_packet(video_dec_ctx, packet);
        av_packet_unref(packet);
        if (ret < 0) continue;

        ret = avcodec_receive_frame(video_dec_ctx, frame);
        if (ret == 0) {
          return ConvertFrame();
        }

      } else {
        av_packet_unref(packet);
      }
    }
  }

  VideoFrame ConvertFrame() {

    sws_scale(sws_ctx, frame->data, frame->linesize, 0,
              video_dec_ctx->height, rgba_frame->data,
              rgba_frame->linesize);

    VideoFrame vf;
    const int row_bytes = video_dec_ctx->width * 4;
    const int h = video_dec_ctx->height;
    vf.rgba.resize(static_cast<std::size_t>(row_bytes) * h);

    for (int y = 0; y < h; ++y) {
      std::memcpy(vf.rgba.data() + y * row_bytes,
                  rgba_frame->data[0] + y * rgba_frame->linesize[0],
                  row_bytes);
    }

    auto* vs = fmt_ctx->streams[video_stream_idx];
    if (frame->pts != AV_NOPTS_VALUE && vs->time_base.den > 0) {
      vf.pts_seconds = static_cast<double>(frame->pts) *
                        av_q2d(vs->time_base);
    } else {

      vf.pts_seconds = (info.frame_rate > 0.0)
                            ? decoded_frame_count / info.frame_rate
                            : 0.0;
    }

    current_pts = vf.pts_seconds;
    ++decoded_frame_count;
    return vf;
  }

  std::optional<AudioBuffer> DecodeAllAudio() {
    if (!open || audio_stream_idx < 0 || !input_source.IsValid()) return std::nullopt;

    FfmpegAudioDecoder decoder;
    if (!decoder.Open(input_source)) {
      return std::nullopt;
    }

    AudioBuffer out;
    out.sample_rate = kMovieAudioRate;
    out.channels = kMovieAudioChannels;
    if (info.duration_seconds > 0.0) {
      const long double expected = info.duration_seconds * kMovieAudioRate *
                                   kMovieAudioChannels;
      if (expected < static_cast<long double>(out.samples.max_size())) {
        out.samples.reserve(static_cast<std::size_t>(expected));
      }
    }

    std::vector<std::int16_t> chunk;
    while (true) {
      const AudioChunkResult result = decoder.ReadNext(chunk);
      if (result == AudioChunkResult::kData) {
        out.samples.insert(out.samples.end(), chunk.begin(), chunk.end());
        continue;
      }
      if (result == AudioChunkResult::kError) {
        return std::nullopt;
      }
      break;
    }

    out.duration_seconds = (out.sample_rate > 0)
        ? static_cast<double>(out.samples.size() / 2) / out.sample_rate
        : 0.0;
    return out;
  }

  std::shared_ptr<openwow::audio::IMovieAudioSource> CreateAudioSource(
      const int sample_rate, const int channels) const {
    if (!open || audio_stream_idx < 0 || !input_source.IsValid() ||
        sample_rate <= 0 || channels <= 0 || channels > 8) {
      return nullptr;
    }
    try {
      return FfmpegMovieAudioSource::Create(input_source, sample_rate, channels);
    } catch (...) {
      try {
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                           "Movie audio worker creation failed for " +
                               input_source.Label());
      } catch (...) {
      }
      return nullptr;
    }
  }

  bool Seek(double seconds) {
    if (!open) return false;
    const int64_t ts = static_cast<int64_t>(seconds * AV_TIME_BASE);
    if (av_seek_frame(fmt_ctx, -1, ts, AVSEEK_FLAG_BACKWARD) < 0)
      return false;
    if (video_dec_ctx) avcodec_flush_buffers(video_dec_ctx);
    eos = false;
    current_pts = seconds;
    return true;
  }

  void Close() {
    if (rgba_frame) {
      if (rgba_frame->data[0])
        av_freep(&rgba_frame->data[0]);
      av_frame_free(&rgba_frame);
    }
    if (frame) av_frame_free(&frame);
    if (packet) av_packet_free(&packet);
    if (sws_ctx) {
      sws_freeContext(sws_ctx);
      sws_ctx = nullptr;
    }
    if (video_dec_ctx) avcodec_free_context(&video_dec_ctx);
    if (fmt_ctx) {

      if (avio_ctx) fmt_ctx->pb = nullptr;
      avformat_close_input(&fmt_ctx);
    }
    if (avio_ctx) {

      avio_context_free(&avio_ctx);
      avio_buffer = nullptr;
    } else if (avio_buffer) {
      av_free(avio_buffer);
      avio_buffer = nullptr;
    }
    input_source = {};
    mem_buf = {};
    open = false;
    eos = true;
    info = {};
    video_stream_idx = -1;
    audio_stream_idx = -1;
    current_pts = 0.0;
    decoded_frame_count = 0;
  }
};

MovieDecoder::MovieDecoder() : impl_(std::make_unique<Impl>()) {}
MovieDecoder::~MovieDecoder() = default;
MovieDecoder::MovieDecoder(MovieDecoder&&) noexcept = default;
MovieDecoder& MovieDecoder::operator=(MovieDecoder&&) noexcept = default;

bool MovieDecoder::Open(const std::vector<std::uint8_t>& data) {
  return OpenOwned(std::vector<std::uint8_t>(data));
}

bool MovieDecoder::OpenOwned(std::vector<std::uint8_t> data) {
  impl_->Close();
  return impl_->OpenFromMemory(
      std::make_shared<const std::vector<std::uint8_t>>(std::move(data)));
}

bool MovieDecoder::OpenPath(const std::string& path) {
  impl_->Close();
  return impl_->OpenFromPath(path);
}

void MovieDecoder::Close() { impl_->Close(); }

bool MovieDecoder::IsOpen() const noexcept { return impl_->open; }

MovieInfo MovieDecoder::GetInfo() const noexcept { return impl_->info; }

std::optional<VideoFrame> MovieDecoder::DecodeNextVideoFrame() {
  return impl_->DecodeNextVideoFrame();
}

bool MovieDecoder::Seek(double seconds) { return impl_->Seek(seconds); }

std::shared_ptr<openwow::audio::IMovieAudioSource>
MovieDecoder::CreateAudioSource(const int sample_rate, const int channels) const {
  return impl_->CreateAudioSource(sample_rate, channels);
}

std::optional<AudioBuffer> MovieDecoder::DecodeAllAudio() {
  return impl_->DecodeAllAudio();
}

double MovieDecoder::CurrentPositionSeconds() const noexcept {
  return impl_->current_pts;
}

bool MovieDecoder::IsEndOfStream() const noexcept { return impl_->eos; }

}

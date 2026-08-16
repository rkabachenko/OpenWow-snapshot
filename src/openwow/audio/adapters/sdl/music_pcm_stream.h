#pragma once
#include "openwow/audio/codecs/audio_decoder.h"
#include "openwow/runtime/scheduling/jthread_compat.h"
#include "openwow/foundation/diagnostics/logging.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
namespace openwow::audio {
class MusicPcmStream {
 public:
  static std::unique_ptr<MusicPcmStream> Create(std::unique_ptr<IAudioDecoder> decoder, const int output_rate, const int output_channels, const int device_buffer_frames, const bool loop) {
    if (!decoder || !decoder->IsOpen() || decoder->GetSampleRate() == 0 || decoder->GetChannels() == 0 || decoder->GetChannels() > 8 || output_rate <= 0 || output_channels <= 0 || output_channels > 8 || device_buffer_frames <= 0) { return nullptr; }
    try {
      auto stream = std::unique_ptr<MusicPcmStream>(new MusicPcmStream( std::move(decoder), output_rate, output_channels, device_buffer_frames, loop));
      if (stream->converter_ == nullptr) { return nullptr; }
      stream->worker_ = openwow::core::JthreadCompat( [state = stream.get()](openwow::core::stop_token stop_token) { state->WorkerLoop(std::move(stop_token)); });
      return stream;
    } catch (const std::exception& exception) { openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, std::string("Music stream creation failed: ") + exception.what()); return nullptr; }
  }
  ~MusicPcmStream() {
    StopAndJoin();
    if (converter_ != nullptr) { SDL_FreeAudioStream(converter_); }
  }
  MusicPcmStream(const MusicPcmStream&) = delete;
  MusicPcmStream& operator=(const MusicPcmStream&) = delete;
  void RequestStop() noexcept {
    if (worker_.joinable()) { worker_.request_stop(); producer_cv_.notify_one(); }
  }
  void MixInto(std::int32_t* mix, const int frames, const float volume) noexcept {
    if (mix == nullptr || frames <= 0) { return; }
    const auto requested_samples =
        static_cast<std::size_t>(frames) * static_cast<std::size_t>(output_channels_);
    const std::uint64_t read = read_sample_.load(std::memory_order_relaxed);
    const std::uint64_t write = write_sample_.load(std::memory_order_acquire);
    const auto available =
        write > read ? static_cast<std::size_t>(write - read) : 0U;
    const auto samples_to_mix = std::min(requested_samples, available);
    const std::size_t ring_offset = static_cast<std::size_t>(read % ring_.size());
    const std::size_t first = std::min(samples_to_mix, ring_.size() - ring_offset);
    if (volume != 0.0F) {
      for (std::size_t sample = 0; sample < first; ++sample) { mix[sample] += static_cast<std::int32_t>( static_cast<float>(ring_[ring_offset + sample]) * volume); }
      for (std::size_t sample = first; sample < samples_to_mix; ++sample) { mix[sample] += static_cast<std::int32_t>( static_cast<float>(ring_[sample - first]) * volume); }
    }
    if (samples_to_mix != 0) {
      read_sample_.store(read + samples_to_mix, std::memory_order_release);
      const auto remaining = available - samples_to_mix;
      if (available > low_watermark_samples_ && remaining <= low_watermark_samples_) { producer_cv_.notify_one(); }
    }
    if (samples_to_mix < requested_samples && !producer_finished_.load(std::memory_order_acquire)) { underflow_count_.fetch_add(1, std::memory_order_relaxed); }
  }
  [[nodiscard]] bool IsFinished() const noexcept { return producer_finished_.load(std::memory_order_acquire) && BufferedSamples() == 0; }
  [[nodiscard]] std::size_t BufferedFrames() const noexcept { return BufferedSamples() / static_cast<std::size_t>(output_channels_); }
  [[nodiscard]] std::size_t CapacityFrames() const noexcept { return ring_.size() / static_cast<std::size_t>(output_channels_); }
  [[nodiscard]] std::uint64_t UnderflowCount() const noexcept { return underflow_count_.load(std::memory_order_relaxed); }
 private:
  static constexpr std::size_t kDecodeFrames = 4096;
  enum class PumpResult { kProgress, kBlocked, kFinished, kError, };
  MusicPcmStream(std::unique_ptr<IAudioDecoder> decoder, const int output_rate, const int output_channels, const int device_buffer_frames, const bool loop)
      : decoder_(std::move(decoder)),
        output_rate_(output_rate),
        output_channels_(output_channels),
        loop_(loop),
        ring_(ComputeRingSamples(output_rate, output_channels, device_buffer_frames)),
        decode_buffer_(kDecodeFrames * decoder_->GetChannels()),
        converted_buffer_(ComputeDecodeOutputSamples(*decoder_, output_rate, output_channels)),
        decode_output_budget_samples_(converted_buffer_.size()),
        low_watermark_samples_(ring_.size() / 2U) { converter_ = SDL_NewAudioStream( AUDIO_F32SYS, static_cast<Uint8>(decoder_->GetChannels()), static_cast<int>(decoder_->GetSampleRate()), AUDIO_S16SYS, static_cast<Uint8>(output_channels_), output_rate_); }
  static std::size_t ComputeRingSamples(const int output_rate, const int output_channels, const int device_buffer_frames) { const auto capacity_frames = std::max<std::size_t>( static_cast<std::size_t>(output_rate) * 2U, std::max<std::size_t>(static_cast<std::size_t>(device_buffer_frames) * 8U, kDecodeFrames * 4U)); return capacity_frames * static_cast<std::size_t>(output_channels); }
  static std::size_t ComputeDecodeOutputSamples(const IAudioDecoder& decoder, const int output_rate, const int output_channels) { const long double ratio = static_cast<long double>(output_rate) / static_cast<long double>(decoder.GetSampleRate()); const auto frames = static_cast<std::size_t>( std::ceil(static_cast<long double>(kDecodeFrames) * ratio)) + 256U; return frames * static_cast<std::size_t>(output_channels); }
  [[nodiscard]] std::size_t BufferedSamples() const noexcept { const std::uint64_t read = read_sample_.load(std::memory_order_acquire); const std::uint64_t write = write_sample_.load(std::memory_order_acquire); return write > read ? static_cast<std::size_t>(write - read) : 0U; }
  [[nodiscard]] std::size_t FreeSamples() const noexcept { return ring_.size() - BufferedSamples(); }
  PumpResult DrainConverted() {
    const int available_bytes = SDL_AudioStreamAvailable(converter_);
    if (available_bytes < 0) { return PumpResult::kError; }
    if (available_bytes == 0) { return PumpResult::kBlocked; }
    const std::size_t free_samples = FreeSamples();
    const std::size_t channel_count = static_cast<std::size_t>(output_channels_);
    std::size_t samples_to_read = std::min( {static_cast<std::size_t>(available_bytes) / sizeof(std::int16_t), free_samples, converted_buffer_.size()});
    samples_to_read -= samples_to_read % channel_count;
    if (samples_to_read == 0) { return PumpResult::kBlocked; }
    const auto requested_bytes = samples_to_read * sizeof(std::int16_t);
    if (requested_bytes > static_cast<std::size_t>(std::numeric_limits<int>::max())) { return PumpResult::kError; }
    const int bytes_read = SDL_AudioStreamGet( converter_, converted_buffer_.data(), static_cast<int>(requested_bytes));
    if (bytes_read <= 0 || bytes_read % static_cast<int>(sizeof(std::int16_t)) != 0) { return PumpResult::kError; }
    const std::size_t samples_read =
        static_cast<std::size_t>(bytes_read) / sizeof(std::int16_t);
    if (samples_read == 0 || samples_read % channel_count != 0) { return PumpResult::kError; }
    PushSamples(converted_buffer_.data(), samples_read);
    return PumpResult::kProgress;
  }
  void PushSamples(const std::int16_t* samples, const std::size_t sample_count) noexcept {
    const std::uint64_t write = write_sample_.load(std::memory_order_relaxed);
    const std::size_t offset = static_cast<std::size_t>(write % ring_.size());
    const std::size_t first = std::min(sample_count, ring_.size() - offset);
    std::memcpy(ring_.data() + offset, samples, first * sizeof(std::int16_t));
    if (sample_count > first) { std::memcpy(ring_.data(), samples + first, (sample_count - first) * sizeof(std::int16_t)); }
    write_sample_.store(write + sample_count, std::memory_order_release);
  }
  PumpResult PumpOnce() {
    const PumpResult drained = DrainConverted();
    if (drained == PumpResult::kProgress || drained == PumpResult::kError) { return drained; }
    const int converted_bytes = SDL_AudioStreamAvailable(converter_);
    if (converted_bytes < 0) { return PumpResult::kError; }
    if (converted_bytes > 0) { return PumpResult::kBlocked; }
    if (source_eof_) {
      if (!converter_flushed_) {
        if (SDL_AudioStreamFlush(converter_) != 0) { return PumpResult::kError; }
        converter_flushed_ = true;
        return PumpResult::kProgress;
      }
      if (loop_) {
        if (!decoder_->Seek(0)) { return PumpResult::kError; }
        SDL_AudioStreamClear(converter_);
        source_eof_ = false;
        converter_flushed_ = false;
        return PumpResult::kProgress;
      }
      return PumpResult::kFinished;
    }
    if (FreeSamples() < decode_output_budget_samples_) { return PumpResult::kBlocked; }
    const std::size_t decoded_frames = decoder_->Decode(decode_buffer_.data(), kDecodeFrames);
    if (decoded_frames == 0) { source_eof_ = true; return PumpResult::kProgress; }
    const std::size_t input_samples = decoded_frames * decoder_->GetChannels();
    const std::size_t input_bytes = input_samples * sizeof(float);
    if (input_bytes > static_cast<std::size_t>(std::numeric_limits<int>::max()) || SDL_AudioStreamPut(converter_, decode_buffer_.data(), static_cast<int>(input_bytes)) != 0) { return PumpResult::kError; }
    return PumpResult::kProgress;
  }
  void WorkerLoop(openwow::core::stop_token stop_token) noexcept {
    try {
      while (!stop_token.stop_requested()) {
        std::size_t steps = 0;
        PumpResult result = PumpResult::kProgress;
        for (; steps < 64U && result == PumpResult::kProgress; ++steps) { result = PumpOnce(); }
        if (result == PumpResult::kFinished || result == PumpResult::kError) { producer_finished_.store(true, std::memory_order_release); return; }
        if (steps == 64U) { continue; }
        std::unique_lock lock(producer_mutex_);
        producer_cv_.wait_for(lock, std::chrono::milliseconds(100), [this, &stop_token] { return stop_token.stop_requested() || FreeSamples() >= decode_output_budget_samples_; });
      }
    } catch (...) {

      producer_finished_.store(true, std::memory_order_release);
    }
  }
  void StopAndJoin() noexcept {
    if (!worker_.joinable()) { return; }
    RequestStop();
    worker_.join();
  }
  std::unique_ptr<IAudioDecoder> decoder_;
  int output_rate_{0};
  int output_channels_{0};
  bool loop_{false};
  SDL_AudioStream* converter_{nullptr};
  std::vector<std::int16_t> ring_;
  std::vector<float> decode_buffer_;
  std::vector<std::int16_t> converted_buffer_;
  std::size_t decode_output_budget_samples_{0};
  std::size_t low_watermark_samples_{0};
  alignas(64) std::atomic<std::uint64_t> read_sample_{0};
  alignas(64) std::atomic<std::uint64_t> write_sample_{0};
  std::atomic<std::uint64_t> underflow_count_{0};
  std::atomic<bool> producer_finished_{false};
  bool source_eof_{false};
  bool converter_flushed_{false};
  std::mutex producer_mutex_;
  std::condition_variable producer_cv_;
  openwow::core::JthreadCompat worker_;
};
}

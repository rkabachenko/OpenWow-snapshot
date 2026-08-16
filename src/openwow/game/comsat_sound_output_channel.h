
#pragma once

#include "openwow/audio/playback/sound_engine.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace openwow::game {

static constexpr std::uint32_t kMixedRingCapacity = 0x4000;

class ComSatMixedRingBuffer {
public:
  bool Write(const std::int16_t *samples, std::uint32_t count);

  std::uint32_t Read(std::int16_t *dest, std::uint32_t max_count);

  [[nodiscard]] std::uint32_t AvailableSamples() const;

  void Reset();

private:
  mutable std::mutex mutex_;
  std::uint32_t write_cursor_{0};
  std::uint32_t count_{0};
  std::array<std::int16_t, kMixedRingCapacity> data_{};
};

class ComSatSoundOutputChannel : public openwow::audio::ICaptureIOSink {
public:

  ComSatSoundOutputChannel(openwow::audio::SoundEngine& sound_engine,
                           std::uint32_t initial_channel_count);

  ~ComSatSoundOutputChannel();

  ComSatSoundOutputChannel(const ComSatSoundOutputChannel &) = delete;
  ComSatSoundOutputChannel &operator=(const ComSatSoundOutputChannel &) = delete;

  bool Initialize(std::int32_t voice_output_driver, std::int32_t voice_input_driver,
                  float sample_rate, std::uint32_t bits_per_sample,
                  std::uint32_t output_channels);

  [[nodiscard]] bool IsReady() const { return true; }

  bool SetVoiceOutputDriverIndex(std::int32_t index);

  bool SetVoiceInputDriverIndex(std::int32_t index);

  [[nodiscard]] std::int32_t GetVoiceOutputDriverIndex() const { return voice_output_driver_index_; }

  [[nodiscard]] std::int32_t GetVoiceInputDriverIndex() const { return voice_input_driver_index_; }

  void SetChannelVolume(std::uint32_t channel_index, float volume);

  bool WriteAudioData(std::uint32_t channel_index, const std::int16_t *pcm_data,
                      std::uint32_t sample_count, double playback_rate);

  [[nodiscard]] std::uint32_t GetChannelAvailableBytes(std::uint32_t channel_index) const;

  [[nodiscard]] std::uint32_t GetOutputFrameSize() const {
    return bits_per_sample_ * output_channels_ / 8;
  }
  [[nodiscard]] std::uint32_t GetInputFrameSize() const { return GetOutputFrameSize(); }

  std::uint32_t ReadMixedOutputBuffer(std::int16_t *dest, std::uint32_t byte_count);

  [[nodiscard]] std::uint32_t GetMixedOutputAvailableBytes() const;

  std::int32_t CopyVoiceOutputDriverNames(char *buffer, std::int32_t max_drivers) const;

  std::int32_t CopyVoiceInputDriverNames(char *buffer, std::int32_t max_drivers) const;

  bool WriteMixedInputBuffer(std::int16_t *data, std::uint32_t sample_count);
  void OnCaptureData(const void* data, std::uint32_t sample_count) override;

  void StopAllChannels();
  void ResetChannel(std::uint32_t channel_index);

  void SetChannelCount(std::uint32_t count);

  [[nodiscard]] std::uint32_t channel_count() const { return static_cast<std::uint32_t>(channel_volumes_.size()); }

private:

  bool InitializeSoundEngine();

  std::vector<float> channel_volumes_;
  openwow::audio::SoundEngine& sound_engine_;
  ComSatMixedRingBuffer mixed_output_;
  std::vector<std::int16_t> capture_write_scratch_;

  std::int32_t voice_output_driver_index_{0};
  std::int32_t voice_input_driver_index_{0};
  float sample_rate_{0.0f};
  std::uint32_t bits_per_sample_{0};
  std::uint32_t output_channels_{0};
};

std::unique_ptr<ComSatSoundOutputChannel>
ComSatSoundOutputChannel_Create(openwow::audio::SoundEngine& sound_engine,
                                 std::uint32_t initial_channel_count);

}

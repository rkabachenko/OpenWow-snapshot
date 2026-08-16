
#include "comsat_sound_output_channel.h"

#include "openwow/audio/playback/sound_engine.h"
#include "openwow/audio/voice/voice_chat_audio_setup.h"
#include "openwow/game/comsat_client.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>

namespace openwow::game {

bool ComSatMixedRingBuffer::Write(const std::int16_t *samples, std::uint32_t count) {
  std::lock_guard lock(mutex_);
  if (count > kMixedRingCapacity - count_) {
    return false;
  }
  for (std::uint32_t i = 0; i < count; ++i) {
    data_[write_cursor_] = samples[i];
    write_cursor_ = (write_cursor_ + 1) & (kMixedRingCapacity - 1);
  }
  count_ += count;
  return true;
}

std::uint32_t ComSatMixedRingBuffer::Read(std::int16_t *dest, std::uint32_t max_count) {
  std::lock_guard lock(mutex_);
  if (count_ == 0) {
    return 0;
  }

  std::uint32_t to_read = std::min(max_count, count_);
  std::uint32_t read_cursor =
      (write_cursor_ - count_ + kMixedRingCapacity) & (kMixedRingCapacity - 1);

  count_ -= to_read;

  for (std::uint32_t i = 0; i < to_read; ++i) {
    dest[i] = data_[read_cursor];
    read_cursor = (read_cursor + 1) & (kMixedRingCapacity - 1);
  }

  if (to_read < max_count) {
    std::memset(dest + to_read, 0, (max_count - to_read) * sizeof(std::int16_t));
  }
  return to_read;
}

std::uint32_t ComSatMixedRingBuffer::AvailableSamples() const {
  std::lock_guard lock(mutex_);
  return count_;
}

void ComSatMixedRingBuffer::Reset() {
  std::lock_guard lock(mutex_);
  write_cursor_ = 0;
  count_ = 0;
  data_.fill(0);
}

ComSatSoundOutputChannel::ComSatSoundOutputChannel(
    openwow::audio::SoundEngine& sound_engine,
    std::uint32_t initial_channel_count)
    : sound_engine_(sound_engine) {

  if (initial_channel_count > 0) {
    SetChannelCount(initial_channel_count);
  }
}

ComSatSoundOutputChannel::~ComSatSoundOutputChannel() {
  sound_engine_.ClearCaptureIOSink(this);
  StopAllChannels();
}

bool ComSatSoundOutputChannel::Initialize(std::int32_t voice_output_driver,
                                           std::int32_t voice_input_driver, float sample_rate,
                                           std::uint32_t bits_per_sample,
                                           std::uint32_t output_channels) {
  voice_output_driver_index_ = voice_output_driver;
  voice_input_driver_index_ = voice_input_driver;
  sample_rate_ = sample_rate;
  bits_per_sample_ = bits_per_sample;
  output_channels_ = output_channels;
  return InitializeSoundEngine();
}

bool ComSatSoundOutputChannel::SetVoiceOutputDriverIndex(std::int32_t index) {
  StopAllChannels();
  voice_output_driver_index_ = index;
  return InitializeSoundEngine();
}

bool ComSatSoundOutputChannel::SetVoiceInputDriverIndex(std::int32_t index) {
  StopAllChannels();
  voice_input_driver_index_ = index;
  return InitializeSoundEngine();
}

void ComSatSoundOutputChannel::SetChannelVolume(std::uint32_t channel_index, float volume) {
  if (channel_index < channel_volumes_.size()) {
    channel_volumes_[channel_index] = std::clamp(volume, 0.0f, 1.0f);
    sound_engine_.SetVoicePlaybackVolume(channel_index,
                                         channel_volumes_[channel_index]);
  }
}

bool ComSatSoundOutputChannel::WriteAudioData(std::uint32_t channel_index,
                                               const std::int16_t *pcm_data,
                                               std::uint32_t sample_count, double playback_rate) {
  if (channel_index >= channel_volumes_.size()) {
    return false;
  }

  if (pcm_data == nullptr || sample_count == 0u || sample_rate_ <= 0.0f ||
      output_channels_ == 0u) {
    return false;
  }
  return sound_engine_.QueueVoicePlaybackPcm(
      channel_index, pcm_data, sample_count,
      static_cast<int>(sample_rate_), static_cast<int>(output_channels_),
      channel_volumes_[channel_index], std::clamp(playback_rate, 0.9, 1.1));
}

std::uint32_t
ComSatSoundOutputChannel::GetChannelAvailableBytes(std::uint32_t channel_index) const {
  if (channel_index >= channel_volumes_.size()) {
    return 0;
  }
  return static_cast<std::uint32_t>(std::min<std::size_t>(
      sound_engine_.GetQueuedVoicePlaybackSampleCount(channel_index) * 2u,
      std::numeric_limits<std::uint32_t>::max()));
}

std::uint32_t ComSatSoundOutputChannel::ReadMixedOutputBuffer(std::int16_t *dest,
                                                               std::uint32_t byte_count) {

  std::uint32_t samples_requested = byte_count / 2;
  std::uint32_t samples_read = mixed_output_.Read(dest, samples_requested);
  return samples_read * 2;
}

std::uint32_t ComSatSoundOutputChannel::GetMixedOutputAvailableBytes() const {
  return mixed_output_.AvailableSamples() * 2;

}

bool ComSatSoundOutputChannel::WriteMixedInputBuffer(std::int16_t *data,
                                                      std::uint32_t sample_count) {

  openwow::audio::VoiceChat_ApplyOutboundChatVolume(sound_engine_, data,
                                                     sample_count);
  return mixed_output_.Write(data, sample_count);
}

void ComSatSoundOutputChannel::OnCaptureData(const void* data,
                                             const std::uint32_t sample_count) {
  if (data == nullptr || sample_count == 0u) {
    return;
  }
  const auto* const samples = static_cast<const std::int16_t*>(data);
  capture_write_scratch_.assign(samples, samples + sample_count);
  (void)WriteMixedInputBuffer(capture_write_scratch_.data(), sample_count);
}

void ComSatSoundOutputChannel::StopAllChannels() {
  sound_engine_.ResetAllVoicePlaybackStreams();
  mixed_output_.Reset();
}

void ComSatSoundOutputChannel::ResetChannel(
    const std::uint32_t channel_index) {
  if (channel_index >= channel_volumes_.size()) {
    return;
  }
  sound_engine_.ResetVoicePlaybackStream(channel_index);
}

void ComSatSoundOutputChannel::SetChannelCount(std::uint32_t count) {
  if (count < channel_volumes_.size()) {
    for (std::size_t index = count; index < channel_volumes_.size(); ++index) {
      sound_engine_.ResetVoicePlaybackStream(static_cast<std::uint32_t>(index));
    }
  }
  channel_volumes_.resize(count, 1.0f);
}

std::int32_t ComSatSoundOutputChannel::CopyVoiceOutputDriverNames(char *buffer,
                                                                  std::int32_t max_drivers) const {
  if (buffer == nullptr || max_drivers <= 0) {
    return 0;
  }
  const auto count = std::min(max_drivers, sound_engine_.GetVoiceOutputDeviceCount());
  for (int index = 0; index < count; ++index) {
    char *const destination = buffer + static_cast<std::size_t>(index) * 256u;
    std::strncpy(destination, sound_engine_.GetVoiceOutputDeviceName(index), 255u);
    destination[255] = '\0';
  }
  return count;
}

std::int32_t ComSatSoundOutputChannel::CopyVoiceInputDriverNames(char *buffer,
                                                                 std::int32_t max_drivers) const {
  if (buffer == nullptr || max_drivers <= 0) {
    return 0;
  }
  const auto count = std::min(max_drivers, sound_engine_.GetInputDeviceCount());
  for (int index = 0; index < count; ++index) {
    char *const destination = buffer + static_cast<std::size_t>(index) * 256u;
    std::strncpy(destination, sound_engine_.GetInputDeviceName(index), 255u);
    destination[255] = '\0';
  }
  return count;
}

bool ComSatSoundOutputChannel::InitializeSoundEngine() {
  const bool initialized =
      openwow::audio::VoiceChat_SetupAudioDrivers(sound_engine_, sample_rate_);

  sound_engine_.SetCaptureIOSink(this);

  sound_engine_.StopCapture();

  {
    const bool voice_chat_enabled =
        openwow::game::ReadVoiceChatCVarBool("EnableVoiceChat");
    const bool microphone_enabled =
        openwow::game::ReadVoiceChatCVarBool("EnableMicrophone");

    if (voice_chat_enabled && microphone_enabled &&
        !openwow::game::VoiceChat_IsDisabled()) {
      sound_engine_.StartCapture();
    }
  }

  if (!initialized || !sound_engine_.IsVoiceChatEnabled()) {
    return false;
  }

  StopAllChannels();

  const auto slot_count = static_cast<std::uint32_t>(channel_volumes_.size());
  for (std::uint32_t i = 0; i < slot_count; ++i) {
    sound_engine_.ResetVoicePlaybackStream(i);
    sound_engine_.CreateCapturedStream(static_cast<int>(i), 73u, 0u, true);

  }
  return true;
}

std::unique_ptr<ComSatSoundOutputChannel>
ComSatSoundOutputChannel_Create(openwow::audio::SoundEngine& sound_engine,
                                 std::uint32_t initial_channel_count) {
  auto channel = std::make_unique<ComSatSoundOutputChannel>(sound_engine,
                                                             initial_channel_count);
  return channel;
}

}

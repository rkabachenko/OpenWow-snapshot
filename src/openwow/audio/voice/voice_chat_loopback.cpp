#include "openwow/audio/voice/voice_chat_loopback.h"

#include "openwow/audio/playback/audio_engine.h"
#include "openwow/audio/playback/sound_engine.h"

#include <algorithm>
#include <cstring>

namespace openwow::audio {

namespace {

void WriteLittleEndian16(std::span<std::uint8_t> bytes, const std::size_t offset,
                         const std::uint16_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value & 0xffu);
  bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
}

void WriteLittleEndian32(std::span<std::uint8_t> bytes, const std::size_t offset,
                         const std::uint32_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value & 0xffu);
  bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
  bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
  bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24u) & 0xffu);
}

std::vector<std::uint8_t> BuildLoopbackWave(const std::uint32_t seconds) {
  const std::uint32_t data_size = seconds * VoiceChatLoopback::kBytesPerSecond;
  const std::uint32_t total_size =
      data_size + static_cast<std::uint32_t>(VoiceChatLoopback::kWaveHeaderBytes);

  std::vector<std::uint8_t> bytes;
  try {
    bytes.assign(total_size, 0);
  } catch (...) {
    return {};
  }
  if (bytes.size() < VoiceChatLoopback::kWaveHeaderBytes) {

    return {};
  }

  const auto span = std::span<std::uint8_t>(bytes);
  std::memcpy(span.data() + 0, "RIFF", 4);
  WriteLittleEndian32(span, 4, data_size + 0x20u);
  std::memcpy(span.data() + 8, "WAVE", 4);
  std::memcpy(span.data() + 12, "fmt ", 4);
  WriteLittleEndian32(span, 16, 16u);
  WriteLittleEndian16(span, 20, 1u);
  WriteLittleEndian16(span, 22, 1u);
  WriteLittleEndian32(span, 24, VoiceChatLoopback::kSampleRateHz);
  WriteLittleEndian32(span, 28, VoiceChatLoopback::kBytesPerSecond);
  WriteLittleEndian16(span, 32, VoiceChatLoopback::kBytesPerSample);
  WriteLittleEndian16(span, 34, 16u);
  std::memcpy(span.data() + 36, "data", 4);
  WriteLittleEndian32(span, 40, data_size);
  return bytes;
}

}

VoiceChatLoopback::~VoiceChatLoopback() { StopPlaying(); }

bool VoiceChatLoopback::BeginRecording(const std::uint32_t max_record_seconds) {

  if (!sound_engine_.IsInitialized()) {
    return false;
  }

  {
    std::lock_guard lock(mutex_);
    if (playback_handle_.IsValid()) {
      audio_engine_.StopSound(playback_handle_);
      playback_handle_ = {};
    }
    std::vector<std::uint8_t>().swap(wave_bytes_);
    write_offset_ = kWaveHeaderBytes;
    capture_callback_selected_ = false;
  }

  auto wave = BuildLoopbackWave(max_record_seconds);
  if (wave.empty()) {
    return false;
  }

  std::lock_guard lock(mutex_);
  wave_bytes_ = std::move(wave);
  write_offset_ = kWaveHeaderBytes;
  capture_callback_selected_ = true;
  return true;
}

bool VoiceChatLoopback::ConsumeCapturedPcm(const std::span<std::int16_t> samples) {
  std::lock_guard lock(mutex_);
  if (!capture_callback_selected_ || wave_bytes_.empty()) {
    return false;
  }

  const std::size_t remaining =
      write_offset_ < wave_bytes_.size() ? wave_bytes_.size() - write_offset_ : 0;
  const std::size_t source_bytes = samples.size_bytes();
  const std::size_t copy_bytes = std::min(remaining, source_bytes) & ~std::size_t{1};
  if (copy_bytes != 0) {
    std::memcpy(wave_bytes_.data() + write_offset_, samples.data(), copy_bytes);
    write_offset_ += copy_bytes;
  }

  if (remaining == 0) {
    capture_callback_selected_ = false;
  }
  return true;
}

bool VoiceChatLoopback::ActivatePreparedRecording() {
  if (!sound_engine_.IsVoiceChatEnabled()) {
    return false;
  }
  std::lock_guard lock(mutex_);
  if (wave_bytes_.empty()) {
    return false;
  }
  capture_callback_selected_ = true;
  return true;
}

void VoiceChatLoopback::ActivatePrimaryCaptureCallback() {
  std::lock_guard lock(mutex_);
  capture_callback_selected_ = false;
}

bool VoiceChatLoopback::IsRecording() const {
  std::lock_guard lock(mutex_);
  return capture_callback_selected_ && sound_engine_.IsInitialized();
}

bool VoiceChatLoopback::Play() {

  if (!sound_engine_.IsInitialized()) {
    return false;
  }

  std::lock_guard lock(mutex_);
  if (playback_handle_.IsValid()) {
    audio_engine_.StopSound(playback_handle_);
    playback_handle_ = {};
  }
  if (wave_bytes_.empty()) {
    return false;
  }

  playback_handle_ = audio_engine_.PlaySoundFromBytesTracked(
      wave_bytes_, 1.0f, false, PlaybackChannel::Voice);
  return playback_handle_.IsValid();
}

void VoiceChatLoopback::StopPlaying() {
  std::lock_guard lock(mutex_);
  if (playback_handle_.IsValid()) {
    audio_engine_.StopSound(playback_handle_);
    playback_handle_ = {};
  }
}

bool VoiceChatLoopback::IsPlaying() {
  std::lock_guard lock(mutex_);
  if (!playback_handle_.IsValid()) {
    return false;
  }
  if (!audio_engine_.IsSoundChannelActive(playback_handle_)) {
    playback_handle_ = {};
    return false;
  }
  return true;
}

void VoiceChatLoopback::Reset() {
  std::lock_guard lock(mutex_);
  if (playback_handle_.IsValid()) {
    audio_engine_.StopSound(playback_handle_);
    playback_handle_ = {};
  }
  capture_callback_selected_ = false;
  write_offset_ = kWaveHeaderBytes;
  std::vector<std::uint8_t>().swap(wave_bytes_);
}

VoiceChatLoopback::Snapshot VoiceChatLoopback::GetSnapshot() const {
  std::lock_guard lock(mutex_);
  const std::size_t pcm_bytes_written =
      write_offset_ > kWaveHeaderBytes ? write_offset_ - kWaveHeaderBytes : 0;
  return Snapshot{
      .wave_bytes = wave_bytes_,
      .pcm_bytes_written = pcm_bytes_written,
      .capture_callback_selected = capture_callback_selected_,
      .recording = capture_callback_selected_ && sound_engine_.IsInitialized(),
      .playing = playback_handle_.IsValid() &&
                 audio_engine_.IsSoundChannelActive(playback_handle_)};
}

}

#pragma once

#include "openwow/audio/playback/audio_engine.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <vector>

namespace openwow::audio {

class SoundEngine;

class VoiceChatLoopback final {
public:
  static constexpr std::uint32_t kSampleRateHz = 8000;
  static constexpr std::uint32_t kBytesPerSample = 2;
  static constexpr std::uint32_t kBytesPerSecond = kSampleRateHz * kBytesPerSample;
  static constexpr std::size_t kWaveHeaderBytes = 44;

  struct Snapshot {
    std::vector<std::uint8_t> wave_bytes;
    std::size_t pcm_bytes_written{0};
    bool capture_callback_selected{false};
    bool recording{false};
    bool playing{false};
  };

  VoiceChatLoopback(SoundEngine& sound_engine, AudioEngine& audio_engine)
      : sound_engine_(sound_engine), audio_engine_(audio_engine) {}
  ~VoiceChatLoopback();

  VoiceChatLoopback(const VoiceChatLoopback &) = delete;
  VoiceChatLoopback &operator=(const VoiceChatLoopback &) = delete;

  bool BeginRecording(std::uint32_t max_record_seconds);

  bool ConsumeCapturedPcm(std::span<std::int16_t> samples);

  bool ActivatePreparedRecording();
  void ActivatePrimaryCaptureCallback();

  [[nodiscard]] bool IsRecording() const;

  bool Play();
  void StopPlaying();
  [[nodiscard]] bool IsPlaying();

  void Reset();

  [[nodiscard]] Snapshot GetSnapshot() const;

private:
  SoundEngine& sound_engine_;
  AudioEngine& audio_engine_;

  mutable std::mutex mutex_;
  std::vector<std::uint8_t> wave_bytes_;
  std::size_t write_offset_{kWaveHeaderBytes};
  AudioDirectPlaybackHandle playback_handle_{};
  bool capture_callback_selected_{false};
};

}


#include "openwow/audio/voice/voice_chat_audio_setup.h"

#include "openwow/audio/playback/sound_engine.h"
#include "openwow/audio/voice/voice_chat_loopback.h"
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

namespace openwow::audio {

namespace {

std::atomic<float> g_outbound_chat_volume{1.0f};

int TruncateFloatToSignedI32LikeX86(const float value) noexcept {
  constexpr float kSignedIntMin = -2147483648.0f;
  constexpr float kSignedIntMaxExclusive = 2147483648.0f;
  if (!std::isfinite(value) || value < kSignedIntMin ||
      value >= kSignedIntMaxExclusive) {
    return std::numeric_limits<int>::min();
  }
  return static_cast<int>(value);
}

}

int VoiceChat_SetupSoundOutput(SoundEngine& engine,
                               void* sound_handle_ptr,
                               void* context_ptr,
                               int use_voice_system) {
  if (!engine.IsVoiceChatEnabled() || sound_handle_ptr == nullptr ||
      context_ptr == nullptr) {
    return 17;
  }

  const auto sound_handle = reinterpret_cast<std::uintptr_t>(sound_handle_ptr);
  const auto context = reinterpret_cast<std::intptr_t>(context_ptr);
  if (sound_handle > std::numeric_limits<std::uint32_t>::max() ||
      context < std::numeric_limits<int>::min() ||
      context > std::numeric_limits<int>::max()) {
    return 17;
  }

  constexpr std::uint32_t kVoiceSoundKind = 73;
  engine.CreateCapturedStream(
      static_cast<int>(context), kVoiceSoundKind,
      static_cast<std::uint32_t>(sound_handle),
      use_voice_system != 0);

  return 0;
}

void VoiceChat_ApplyOutboundChatVolume(SoundEngine& engine,
                                        std::int16_t* samples,
                                        const unsigned int sample_count) {
  if (!engine.IsVoiceChatEnabled() || samples == nullptr || sample_count == 0) {
    return;
  }

  const float volume = g_outbound_chat_volume.load(std::memory_order_relaxed);
  if (volume == 1.0f) {
    return;
  }

  for (unsigned int i = 0; i < sample_count; ++i) {
    const float product = static_cast<float>(samples[i]) * volume;

    int scaled = TruncateFloatToSignedI32LikeX86(product);
    if (scaled < -32768) {
      scaled = -32768;
    } else if (scaled > 32767) {
      scaled = 32767;
    }
    samples[i] = static_cast<std::int16_t>(scaled);
  }
}

void VoiceChat_SetOutboundChatVolume(const float linear_gain) noexcept {
  g_outbound_chat_volume.store(linear_gain, std::memory_order_relaxed);
}

int VoiceChat_GetMicrophoneSignalLevel(SoundEngine& engine) {
  if (!engine.IsVoiceChatEnabled()) {
    return 0;
  }
  return engine.GetMicrophoneSignalLevel();
}

bool VoiceChat_SetupAudioDrivers(SoundEngine& engine,
                                 const float sample_rate_hz) {
  if (!engine.IsInitialized() || !std::isfinite(sample_rate_hz) ||
      sample_rate_hz <= 0.0f) {
    engine.SetMicrophoneSignalLevel(0);
    engine.SetInputLevelCallback({});
    return false;
  }

  engine.EnumerateDevices();
  const std::string output_name = engine.GetCurrentVoiceOutputDeviceName().empty()
                                      ? engine.GetEnumeratedDefaultVoiceOutputDeviceName()
                                      : engine.GetCurrentVoiceOutputDeviceName();
  const std::string input_name = engine.GetCurrentInputDeviceName().empty()
                                     ? engine.GetEnumeratedDefaultInputDeviceName()
                                     : engine.GetCurrentInputDeviceName();
  const char* output_device =
      output_name.empty() || output_name == "System Default" ? nullptr
                                                               : output_name.c_str();
  const char* input_device =
      input_name.empty() || input_name == "System Default" ? nullptr
                                                             : input_name.c_str();
  int output_driver = -1;
  int input_driver = -1;
  engine.SetMicrophoneSignalLevel(0);
  engine.SetInputLevelCallback({});
  return engine.InitVoiceChat(&output_driver, output_device, &input_driver,
                              input_device, sample_rate_hz);
}

bool VoiceChat_SetCaptureEnabled(SoundEngine& engine, const bool enabled) {
  if (enabled) {
    if (!engine.IsVoiceChatEnabled()) {
      engine.DisableCapture();
      return false;
    }
    engine.StartCapture();
    return engine.IsRecording();
  } else {
    engine.DisableCapture();
    return !engine.IsCaptureEnabled();
  }
}

bool VoiceChat_RecordLoopbackSound(VoiceChatLoopback& loopback,
                                   const std::uint32_t max_record_seconds) {
  return loopback.BeginRecording(max_record_seconds);
}

void VoiceChat_StopRecordingLoopbackSound(VoiceChatLoopback& loopback) {
  loopback.ActivatePrimaryCaptureCallback();
}

bool VoiceChat_SetLoopbackPlayback(VoiceChatLoopback& loopback,
                                   const bool enabled) {
  if (enabled) {
    return loopback.Play();
  } else {
    loopback.StopPlaying();
    return true;
  }
}

bool VoiceChat_IsRecordingLoopbackSound(VoiceChatLoopback& loopback) {
  return loopback.IsRecording();
}

bool VoiceChat_IsPlayingLoopbackSound(VoiceChatLoopback& loopback) {
  return loopback.IsPlaying();
}

}

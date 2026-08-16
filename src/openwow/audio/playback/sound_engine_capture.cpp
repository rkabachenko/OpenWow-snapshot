#include "openwow/audio/playback/sound_engine.h"
#include "openwow/audio/voice/voice_chat_loopback.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <span>

namespace {
constexpr std::size_t kVoiceCaptureScratchBufferBytes = 2048;
}

namespace openwow::audio {

void SoundEngine::CleanupActiveVoiceCaptureSession() {
  voice_capture_.recording_active = false;
  voice_capture_.scratch_buffer.clear();
  std::vector<std::uint8_t>().swap(voice_capture_.scratch_buffer);
  voice_capture_.capture_temp_buffer.clear();
  std::vector<std::int16_t>().swap(voice_capture_.capture_temp_buffer);
  voice_capture_.converted_capture_buffer.clear();
  voice_capture_.active_capture_stream_handle = 0;
}

void SoundEngine::StartCapture() {
  if (!initialized_) return;
  capture_enabled_ = true;
  if (!voice_chat_enabled_ || voice_capture_.recording_active) {
    return;
  }

  CleanupActiveVoiceCaptureSession();
  if (voice_capture_.capture_conversion_stream != nullptr) {
    SDL_AudioStreamClear(static_cast<SDL_AudioStream*>(
        voice_capture_.capture_conversion_stream));
  }
  voice_capture_.monitor_mode = 1;
  voice_capture_.scratch_buffer.resize(kVoiceCaptureScratchBufferBytes);
  voice_capture_.recording_active = true;

  if (voice_capture_.capture_device_id != 0) {
    SDL_PauseAudioDevice(
        static_cast<SDL_AudioDeviceID>(voice_capture_.capture_device_id), 0);
    LogLine(0, ".\\SoundEngine.cpp", 0, "StartCapture: SDL capture unpaused");
  }
}

void SoundEngine::StopCapture() {
  if (!initialized_ || !voice_chat_enabled_ || !voice_capture_.recording_active) {
    return;
  }

  if (voice_capture_.capture_device_id != 0) {
    const SDL_AudioDeviceID dev =
        static_cast<SDL_AudioDeviceID>(voice_capture_.capture_device_id);
    SDL_PauseAudioDevice(dev, 1);

    std::uint32_t remaining = SDL_GetQueuedAudioSize(dev);
    while (remaining > 0) {
      std::uint8_t drain[4096];
      const std::uint32_t chunk = std::min<std::uint32_t>(remaining, sizeof(drain));
      const std::uint32_t read = SDL_DequeueAudio(dev, drain, chunk);
      if (read == 0) break;
      remaining -= read;
    }
  }
  if (voice_capture_.capture_conversion_stream != nullptr) {
    SDL_AudioStreamClear(static_cast<SDL_AudioStream*>(
        voice_capture_.capture_conversion_stream));
  }

  CleanupActiveVoiceCaptureSession();
}

void SoundEngine::DisableCapture() {
  capture_enabled_ = false;

  if (initialized_) {

    if (voice_capture_.recording_active) {
      StopCapture();
    }
  }

  ProcessUpdateTick();
}

bool SoundEngine::IsRecording() const {
  return initialized_ && voice_chat_enabled_ && voice_capture_.recording_active;
}

void SoundEngine::CreateCapturedStream(int stream_handle,
                                        std::uint32_t flags,
                                        std::uint32_t handle_ptr,
                                        bool ) {

  if (!voice_chat_enabled_) {
    LogLine(0, ".\\SoundEngine.cpp", 0,
            "CreateCapturedStream: voice chat not enabled");
    return;
  }

  if (handle_ptr != 0) {
    *reinterpret_cast<std::uint32_t*>(
        static_cast<std::uintptr_t>(handle_ptr)) = 1;
  }

  voice_capture_.active_capture_stream_handle = stream_handle;

  LogLine(0, ".\\SoundEngine.cpp", 0,
          "CreateCapturedStream: stream_handle=%d flags=0x%X",
          stream_handle, flags);
}

bool SoundEngine::QueueVoicePlaybackPcm(
    const std::uint32_t stream_index, const std::int16_t *samples,
    const std::size_t sample_count, const int sample_rate, const int channels,
    const float volume, const double playback_rate) {
  return voice_chat_enabled_ && audio_engine_.QueueVoicePcm(
                                    stream_index, samples, sample_count,
                                    sample_rate, channels, volume, playback_rate);
}

void SoundEngine::SetVoicePlaybackVolume(const std::uint32_t stream_index,
                                         const float volume) {
  audio_engine_.SetVoicePcmStreamVolume(stream_index, volume);
}

void SoundEngine::ResetVoicePlaybackStream(const std::uint32_t stream_index) {
  audio_engine_.ResetVoicePcmStream(stream_index);
}

void SoundEngine::ResetAllVoicePlaybackStreams() {
  audio_engine_.ResetAllVoicePcmStreams();
}

std::size_t SoundEngine::GetQueuedVoicePlaybackSampleCount(
    const std::uint32_t stream_index) const {
  return audio_engine_.GetQueuedVoicePcmSampleCount(stream_index);
}

void SoundEngine::ShutdownVoiceChat() {
  if (voice_chat_loopback_ != nullptr) {
    voice_chat_loopback_->Reset();
  }
  ResetAllVoicePlaybackStreams();
  audio_engine_.CloseVoiceOutputDevice();

  if (voice_capture_.capture_device_id != 0) {
    SDL_CloseAudioDevice(
        static_cast<SDL_AudioDeviceID>(voice_capture_.capture_device_id));
    voice_capture_.capture_device_id = 0;
  }
  if (voice_capture_.capture_conversion_stream != nullptr) {
    SDL_FreeAudioStream(static_cast<SDL_AudioStream*>(
        voice_capture_.capture_conversion_stream));
    voice_capture_.capture_conversion_stream = nullptr;
  }

  if (!voice_chat_enabled_ && !capture_enabled_ && !voice_capture_.recording_active) return;

  LogLine(6321, ".\\SoundEngine.cpp", 0, " ");
  LogLine(6322, ".\\SoundEngine.cpp", 0, "=> Shutting Down Voice Chat");
  DisableCapture();

  CleanupFinishedSounds();
  CleanupExpiredSounds(true);

  CleanupActiveVoiceCaptureSession();

  if (monitor_dsp_ != nullptr) {
    std::lock_guard lock(dsp_graph_mutex_);
    (void)DeactivateDspNode(monitor_dsp_);
    DestroyDspUnit(monitor_dsp_);
    monitor_dsp_ = nullptr;
  }

  input_level_callback_ = nullptr;

  microphone_signal_level_ = 0;
  voice_chat_enabled_ = false;
  LogLine(6350, ".\\SoundEngine.cpp", 0, "=> Done Shutting Down Voice Chat");
}

bool SoundEngine::InitVoiceChat(int* output_driver,
                                const char* output_name,
                                int* input_driver,
                                const char* input_name,
                                float ) {

  if (!initialized_) {
    LogLine(0, ".\\SoundEngine.cpp", 0,
            "InitVoiceChat skipped because the sound engine is not initialized");
    CleanupActiveVoiceCaptureSession();
    voice_chat_enabled_ = false;
    capture_enabled_ = false;
    return false;
  }

  if (output_name && *output_name) {
    SetCurrentVoiceOutputDeviceName(output_name);
  } else if (current_voice_output_device_name_.empty()) {
    SetCurrentVoiceOutputDeviceName(GetEnumeratedDefaultVoiceOutputDeviceName());
  }

  if (input_name && *input_name) {
    SetCurrentInputDeviceName(input_name);
  } else if (current_input_device_name_.empty()) {
    SetCurrentInputDeviceName(GetEnumeratedDefaultInputDeviceName());
  }

  (void)audio_engine_.OpenVoiceOutputDevice(current_voice_output_device_name_);

  if (output_driver) *output_driver = 0;
  if (input_driver)  *input_driver  = 0;

  if (voice_capture_.capture_device_id != 0) {
    SDL_CloseAudioDevice(
        static_cast<SDL_AudioDeviceID>(voice_capture_.capture_device_id));
    voice_capture_.capture_device_id = 0;
  }
  if (voice_capture_.capture_conversion_stream != nullptr) {
    SDL_FreeAudioStream(static_cast<SDL_AudioStream*>(
        voice_capture_.capture_conversion_stream));
    voice_capture_.capture_conversion_stream = nullptr;
  }

  constexpr int kDesiredSampleRate = 8000;
  constexpr int kDesiredChannels   = 1;
  constexpr int kDesiredSamples    = 4096;

  SDL_AudioSpec desired{};
  desired.freq     = kDesiredSampleRate;
  desired.format   = AUDIO_S16SYS;
  desired.channels = static_cast<Uint8>(kDesiredChannels);
  desired.samples  = static_cast<Uint16>(kDesiredSamples);
  desired.callback = nullptr;
  desired.userdata = this;

  const std::string& input_device = current_input_device_name_;
  const char* device_name = nullptr;
  if (!input_device.empty() && input_device != "System Default") {
    device_name = input_device.c_str();
  }

  SDL_AudioSpec obtained{};
  const SDL_AudioDeviceID capture_dev = SDL_OpenAudioDevice(
      device_name, 1, &desired, &obtained,
      SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE);

  if (capture_dev == 0) {
    LogLine(0, ".\\SoundEngine.cpp", 0,
            "InitVoiceChat: SDL_OpenAudioDevice (capture) failed: %s",
            SDL_GetError());
    CleanupActiveVoiceCaptureSession();
    voice_chat_enabled_ = true;
    capture_enabled_ = false;
    return true;
  }

  voice_capture_.capture_device_id    = static_cast<std::uint32_t>(capture_dev);
  voice_capture_.capture_sample_rate  = obtained.freq;
  voice_capture_.capture_channels     = obtained.channels;
  voice_capture_.capture_temp_buffer.clear();
  voice_capture_.converted_capture_buffer.clear();

  if (obtained.freq != kDesiredSampleRate ||
      obtained.channels != kDesiredChannels) {
    voice_capture_.capture_conversion_stream = SDL_NewAudioStream(
        AUDIO_S16SYS, obtained.channels, obtained.freq,
        AUDIO_S16SYS, kDesiredChannels, kDesiredSampleRate);
    if (voice_capture_.capture_conversion_stream == nullptr) {
      LogLine(0, ".\\SoundEngine.cpp", 0,
              "InitVoiceChat: SDL_NewAudioStream failed: %s", SDL_GetError());
      SDL_CloseAudioDevice(capture_dev);
      voice_capture_.capture_device_id = 0;
      CleanupActiveVoiceCaptureSession();
      voice_chat_enabled_ = true;
      capture_enabled_ = false;
      return true;
    }
  }

  SDL_PauseAudioDevice(capture_dev, 1);

  CleanupActiveVoiceCaptureSession();
  voice_chat_enabled_ = true;
  capture_enabled_    = false;
  LogLine(0, ".\\SoundEngine.cpp", 0,
          "=> Initializing Voice Chat (SDL capture dev=%u, %d Hz, %d ch)",
          static_cast<unsigned>(capture_dev), obtained.freq,
          obtained.channels);
  return true;
}

void SoundEngine::CaptureWork(const std::uint32_t context_tick_ms) {
  last_voice_capture_context_tick_ms_ = context_tick_ms;
  ++voice_capture_work_request_count_;

  if (!capture_enabled_) {
    return;
  }

  std::lock_guard<std::mutex> lock(capture_mutex_);

  if (voice_capture_.capture_device_id == 0) {
    return;
  }

  const SDL_AudioDeviceID capture_dev =
      static_cast<SDL_AudioDeviceID>(voice_capture_.capture_device_id);
  const std::uint32_t bytes_available = SDL_GetQueuedAudioSize(capture_dev);
  if (bytes_available == 0) {
    return;
  }

  constexpr std::uint32_t kBytesPerSample = 2;
  const std::uint32_t samples_available =
      bytes_available / kBytesPerSample;
  if (samples_available == 0) {
    return;
  }

  if (voice_capture_.capture_temp_buffer.size() < samples_available) {
    voice_capture_.capture_temp_buffer.resize(samples_available);
  }

  const std::uint32_t bytes_dequeued = SDL_DequeueAudio(
      capture_dev,
      voice_capture_.capture_temp_buffer.data(),
      bytes_available);

  const std::uint32_t samples_dequeued = bytes_dequeued / kBytesPerSample;
  if (samples_dequeued == 0) {
    return;
  }

  std::int16_t* capture_data = voice_capture_.capture_temp_buffer.data();
  std::size_t capture_sample_count = samples_dequeued;
  if (voice_capture_.capture_conversion_stream != nullptr) {
    auto* const converter = static_cast<SDL_AudioStream*>(
        voice_capture_.capture_conversion_stream);
    if (SDL_AudioStreamPut(converter, capture_data,
                           static_cast<int>(bytes_dequeued)) != 0) {
      LogLine(0, ".\\SoundEngine.cpp", 0,
              "CaptureWork: SDL_AudioStreamPut failed: %s", SDL_GetError());
      SDL_AudioStreamClear(converter);
      return;
    }
    const int converted_bytes = SDL_AudioStreamAvailable(converter);
    if (converted_bytes < static_cast<int>(sizeof(std::int16_t))) {
      return;
    }
    const std::size_t converted_samples =
        static_cast<std::size_t>(converted_bytes) / sizeof(std::int16_t);
    voice_capture_.converted_capture_buffer.resize(converted_samples);
    const int received_bytes = SDL_AudioStreamGet(
        converter, voice_capture_.converted_capture_buffer.data(),
        static_cast<int>(converted_samples * sizeof(std::int16_t)));
    if (received_bytes <= 0) {
      return;
    }
    capture_data = voice_capture_.converted_capture_buffer.data();
    capture_sample_count =
        static_cast<std::size_t>(received_bytes) / sizeof(std::int16_t);
  }

  auto captured_samples = std::span<std::int16_t>(
      capture_data, capture_sample_count);

  const bool consumed_by_loopback =
      voice_chat_loopback_ != nullptr &&
      voice_chat_loopback_->ConsumeCapturedPcm(captured_samples);

  if (!consumed_by_loopback && capture_io_sink_) {
    capture_io_sink_->OnCaptureData(
        capture_data, capture_sample_count);
  }

  if (!consumed_by_loopback) {
    const auto* samples = capture_data;
    double sum_sq = 0.0;
    for (std::size_t i = 0; i < capture_sample_count; ++i) {
      const double normalized =
          static_cast<double>(samples[i]) * (1.0 / 32768.0);
      sum_sq += normalized * normalized;
    }
    const double rms = std::sqrt(
        sum_sq / static_cast<double>(capture_sample_count));
    microphone_signal_level_ = static_cast<int>(
        std::clamp(rms * 100.0, 0.0, 100.0));
  }
}

}
